// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/throttler_api.h"

#include <algorithm>
#include <chrono>  // NOLINT
#include <sstream>

#include "src/clock.h"
#include "src/cloud_env.h"
#include "src/globals.h"
#include "src/pem_roots.h"
#include "src/string.h"
#include "google/devtools/cloudprofiler/v2/profiler.grpc.pb.h"
#include "google/protobuf/duration.pb.h"  // NOLINT
#include "google/rpc/error_details.pb.h"  // NOLINT
#include "grpc/grpc_security.h"
#include "grpc/support/log.h"
#include "grpc/support/string_util.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/support/channel_arguments.h"
#include "third_party/javaprofiler/heap_sampler.h"

// API curated profiling configuration.
DEFINE_string(cprof_api_address, "cloudprofiler.googleapis.com",
              "API server address");
DEFINE_string(cprof_deployment_labels, "",
              "comma-separated list of name=value deployment labels; "
              "names must be in dns-label-like-format");
DEFINE_string(cprof_profile_labels, "",
              "comma-separated list of name=value profile labels; "
              "names must be in dns-label-like-format");
DEFINE_bool(cprof_use_insecure_creds_for_testing, false,
            "use insecure channel creds, for testing only");

namespace cloud {
namespace profiler {

namespace api = google::devtools::cloudprofiler::v2;

namespace {

// Initial value for backoffs where the duration is not server-guided.
const int64_t kBackoffNanos = 60 * kNanosPerSecond;  // 1 minute
// Backoff envelope exponential growth factor.
const float kBackoffFactor = 1.3;
// Cap the backoff envelope at 1 hour.
const int64_t kMaxBackoffNanos = 60 * 60 * kNanosPerSecond;
// Name of the optional trailing metadata with the server-guided backoff.
const char kRetryInfoMetadata[] = "google.rpc.retryinfo-bin";
// Standard zone name label key.
const char kZoneNameLabel[] = "zone";
// Standard language label key.
const char kLanguageLabel[] = "language";
// Standard service version label key.
const char kServiceVersionLabel[] = "version";
// Range of random number
const int64_t kRandomRange = 65536;

// Routes GRPC logging through cloud profiler logger.
// Otherwise GRPC would log to stderr.
void GRPCLog(gpr_log_func_args* args) {
  switch (args->severity) {
    case GPR_LOG_SEVERITY_DEBUG:
      // Discard GRPC debug messages
      break;
    case GPR_LOG_SEVERITY_INFO:
      LOG(INFO) << "GRPC: " << args->file << ":" << args->line << " "
                << args->message;
      break;
    case GPR_LOG_SEVERITY_ERROR:
    default:
      LOG(ERROR) << "GRPC: " << args->file << ":" << args->line << " "
                 << args->message;
  }
}

// Overrides the default SSL roots. Otherwise gRPC calls would fail if a SSL
// roots file can't be found neither at a location specified by
// GRPC_DEFAULT_SSL_ROOTS_FILE_PATH nor at "/usr/share/grpc/roots.pem".
grpc_ssl_roots_override_result OverrideSSLRoots(char** pem_root_certs) {
  // Must gpr_strdup the value as gRPC runtime takes the ownership.
  *pem_root_certs = gpr_strdup(kPemRootCerts);
  return GRPC_SSL_ROOTS_OVERRIDE_OK;
}

// Creates the profiler gRPC API stub. Returns nullptr on error.
std::unique_ptr<api::grpc::ProfilerService::StubInterface>
NewProfilerServiceStub(const std::string& addr) {
  std::shared_ptr<grpc::ChannelCredentials> creds;
  if (FLAGS_cprof_use_insecure_creds_for_testing) {
    creds = grpc::InsecureChannelCredentials();
  } else {
    grpc_set_ssl_roots_override_callback(&OverrideSSLRoots);
    creds = grpc::GoogleDefaultCredentials();
    if (creds == nullptr) {
      LOG(ERROR) << "Failed to get Google default credentials";
      return nullptr;
    }
  }

  grpc::ChannelArguments channel_arguments;
  channel_arguments.SetUserAgentPrefix(
      "gcloud-java-profiler/" + std::string(CLOUD_PROFILER_AGENT_VERSION));

  std::shared_ptr<grpc::ChannelInterface> ch =
      grpc::CreateCustomChannel(addr, creds, channel_arguments);
  if (ch == nullptr) {
    LOG(ERROR) << "Failed to create gRPC channel";
    return nullptr;
  }

  std::unique_ptr<api::grpc::ProfilerService::StubInterface> stub =
      api::grpc::ProfilerService::NewStub(ch);
  if (stub == nullptr) {
    LOG(ERROR) << "Failed to initialize profiler service";
  }

  return stub;
}

std::string DebugString(const grpc::Status& st) {
  std::ostringstream os;
  os << st.error_code() << " (" << st.error_message() << ")";  // NOLINT
  return os.str();
}

// Attempts to read the backoff delay information from the server trailing
// metadata. Should only be used when a call failed with ABORTED error as only
// then the backoff info may be returned. Returns false if there was any
// unexpected error parsing the returned retry information proto.
bool AbortedBackoffDuration(const grpc::ClientContext& ctx,
                            int64_t* backoff_ns) {
  *backoff_ns = 0;
  auto md = ctx.GetServerTrailingMetadata();
  auto it = md.find(kRetryInfoMetadata);
  if (it != md.end() && !it->second.empty()) {
    google::rpc::RetryInfo ri;
    if (!ri.ParseFromArray(it->second.data(), it->second.size())) {
      return false;
    }
    const google::protobuf::Duration& delay = ri.retry_delay();
    *backoff_ns = kNanosPerSecond * delay.seconds() + delay.nanos();
    return true;
  }
  return false;
}

// Initializes deployment information from environment properties and label
// string in "name1=val1,name2=val2,..." format. Returns false on error.
bool InitializeDeployment(CloudEnv* env, const std::string& labels,
                          api::Deployment* d) {
  std::string project_id = env->ProjectID();
  if (project_id.empty()) {
    LOG(ERROR) << "Project ID is unknown";
    return false;
  }
  d->set_project_id(project_id);

  std::string service = env->Service();
  if (service.empty()) {
    LOG(ERROR) << "Deployment service name is not configured";
    return false;
  }
  if (!IsValidServiceName(service)) {
    LOG(ERROR)
        << "Deployment service name '" << service
        << "' does not match pattern '^[a-z]([-a-z0-9_.]{0,253}[a-z0-9])?$'";
    return false;
  }
  d->set_target(service);

  std::map<std::string, std::string> label_kvs;
  if (!ParseKeyValueList(labels, &label_kvs)) {
    LOG(ERROR) << "Failed to parse deployment labels '" << labels << "'";
    return false;
  }

  std::string service_version = env->ServiceVersion();
  if (!service_version.empty()) {
    label_kvs[kServiceVersionLabel] = service_version;
  }

  std::string zone_name = env->ZoneName();
  if (!zone_name.empty()) {
    label_kvs[kZoneNameLabel] = zone_name;
  }

  label_kvs[kLanguageLabel] = "java";
  for (const auto& kv : label_kvs) {
    (*d->mutable_labels())[kv.first] = kv.second;
  }

  LOG(INFO) << "Initialized deployment: project_id=" << project_id
            << ", service=" << service
            << ", service_version=" << service_version
            << ", zone_name=" << zone_name;
  return true;
}

bool AddProfileLabels(api::Profile* p, const std::string& labels) {
  std::map<std::string, std::string> label_kvs;
  if (!ParseKeyValueList(labels, &label_kvs)) {
    LOG(ERROR) << "Failed to parse profile labels '" << labels << "'";
    return false;
  }

  for (const auto& kv : label_kvs) {
    (*p->mutable_labels())[kv.first] = kv.second;
  }
  return true;
}

}  // namespace

// Returns true if the service name matches the regex
// "^[a-z]([-a-z0-9_.]{0,253}[a-z0-9])?$", and false otherwise.
bool IsValidServiceName(std::string s) {
  if (s.length() < 1 || s.length() > 255) {
    return false;
  }
  if (s[0] < 'a' || s[0] > 'z') {
    return false;
  }
  if ((s.back() < 'a' || s.back() > 'z') &&
      (s.back() < '0' || s.back() > '9')) {
    return false;
  }
  for (size_t i = 1; i < s.length() - 1; ++i) {
    char c = s[i];
    if ((c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '.' && c != '-' &&
        c != '_') {
      return false;
    }
  }
  return true;
}

APIThrottler::APIThrottler(JNIEnv* jni)
    : APIThrottler(DefaultCloudEnv(), DefaultClock(), nullptr, jni) {}

APIThrottler::APIThrottler(
    CloudEnv* env, Clock* clock,
    std::unique_ptr<google::devtools::cloudprofiler::v2::grpc::ProfilerService::
                        StubInterface>
        stub,
    JNIEnv* jni)
    : env_(env),
      clock_(clock),
      stub_(std::move(stub)),
      types_({api::CPU, api::WALL}),
      java_version_(JavaVersion(jni)),
      creation_backoff_envelope_ns_(kBackoffNanos),
      closed_(false) {
  grpc_init();
  gpr_set_log_function(GRPCLog);

  LOG(INFO) << "Java version: " << java_version_;

  // Create a random number generator.
  gen_ = std::default_random_engine(clock_->Now().tv_nsec / 1000);
  dist_ = std::uniform_int_distribution<int64_t>(0, kRandomRange);

  if (!stub_) {  // Set in tests
    LOG(INFO) << "Will use profiler service " << FLAGS_cprof_api_address
              << " to create and upload profiles";
    stub_ = NewProfilerServiceStub(FLAGS_cprof_api_address);
  }

  if (google::javaprofiler::HeapMonitor::Enabled()) {
    LOG(INFO) << "Heap allocation sampling supported for this JDK";
    types_.push_back(api::HEAP);
  }
}

std::string APIThrottler::JavaVersion(JNIEnv* jni) {
  const std::string kUnknownVersion = "unknown_version";

  jclass system_class = jni->FindClass("java/lang/System");
  if (system_class == nullptr) {
    return kUnknownVersion;
  }
  jmethodID get_property_method = jni->GetStaticMethodID(
      system_class, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
  if (get_property_method == nullptr) {
    return kUnknownVersion;
  }
  jstring jstr = reinterpret_cast<jstring>(jni->CallStaticObjectMethod(
      system_class, get_property_method, jni->NewStringUTF("java.version")));
  if (jstr == nullptr) {
    return kUnknownVersion;
  }
  // Copy the returned value and release the memory allocated by JNI.
  const char* s = jni->GetStringUTFChars(jstr, nullptr);
  std::string ret = std::string(s);
  jni->ReleaseStringUTFChars(jstr, s);
  return ret;
}

void APIThrottler::SetProfileTypes(const std::vector<api::ProfileType>& types) {
  types_ = types;
}

bool APIThrottler::WaitNext() {
  if (stub_ == nullptr) {
    LOG(ERROR) << "Profiler API is not initialized, stop profiling";
    return false;
  }

  api::CreateProfileRequest req;
  for (const auto& type : types_) {
    req.add_profile_type(type);
  }
  if (!InitializeDeployment(env_, FLAGS_cprof_deployment_labels,
                            req.mutable_deployment())) {
    LOG(ERROR) << "Failed to initialize deployment, stop profiling";
    return false;
  }
  req.set_parent("projects/" + req.deployment().project_id());

  while (true) {
    LOG(INFO) << "Creating a new profile via profiler service";

    profile_.Clear();
    ResetClientContext();

    // The system clock is used here directly, because clock_->now() returns
    // CLOCK_MONOTONIC, not CLOCK_REALTIME time.
    // The API server sets a 1 hour server-side timeout. All agents should set
    // a corresponding 1 hour timeout for CreateProfile requests.
    ctx_->set_deadline(std::chrono::system_clock::now() +
                       std::chrono::hours{1});

    grpc::Status st = stub_->CreateProfile(ctx_.get(), req, &profile_);
    if (st.ok()) {
      LOG(INFO) << "Profile created: " << ProfileType() << " "
                << profile_.name();
      // Reset the backoff envelope to the base on success.
      creation_backoff_envelope_ns_ = kBackoffNanos;
      break;
    }
    if (closed_) {
      return false;
    }
    OnCreationError(st);
  }

  return true;
}

std::string APIThrottler::ProfileType() {
  api::ProfileType pt = profile_.profile_type();
  switch (pt) {
    case api::CPU:
      return kTypeCPU;
    case api::WALL:
      return kTypeWall;
    case api::HEAP:
      return kTypeHeap;
    default:
      const std::string& pt_name = api::ProfileType_Name(pt);
      LOG(ERROR) << "Unsupported profile type " << pt_name;
      return "unsupported-" + pt_name;
  }
}

int64_t APIThrottler::DurationNanos() {
  auto d = profile_.duration();
  return d.seconds() * kNanosPerSecond + d.nanos();
}

bool APIThrottler::Upload(std::string profile) {
  LOG(INFO) << "Uploading " << profile.size() << " bytes of '" << ProfileType()
            << "' profile data";

  if (!AddProfileLabels(&profile_, FLAGS_cprof_profile_labels)) {
    LOG(ERROR) << "Failed to add profile labels, won't upload the profile";
    return false;
  }

  api::UpdateProfileRequest req;
  *req.mutable_profile() = profile_;

  req.mutable_profile()->set_profile_bytes(std::move(profile));
  ResetClientContext();

  // The system clock is used here directly, because clock_->now() returns
  // CLOCK_MONOTONIC, not CLOCK_REALTIME time.
  // The API server sets a 20 second server-side timeout. All agents should set
  // a corresponding 20 second timeout for UpdateProfile requests.
  ctx_->set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds{20});
  grpc::Status st = stub_->UpdateProfile(ctx_.get(), req, &profile_);

  if (!st.ok()) {
    // TODO: Recognize and retry transient errors.
    LOG(ERROR) << "Profile bytes upload failed: " << DebugString(st);
    return false;
  }

  return true;
}

void APIThrottler::OnCreationError(const grpc::Status& st) {
  if (st.error_code() == grpc::StatusCode::ABORTED) {
    int64_t backoff_ns;
    if (AbortedBackoffDuration(*ctx_, &backoff_ns)) {
      if (backoff_ns > 0) {
        LOG(INFO) << "Got ABORTED, will retry after backing off for "
                  << backoff_ns / kNanosPerMilli << "ms";
        clock_->SleepFor(NanosToTimeSpec(backoff_ns));
        return;
      }
    }
  }

  LOG(WARNING) << "Failed to create profile, will retry: " << DebugString(st);

  double random_factor = static_cast<double>(dist_(gen_)) / kRandomRange;
  clock_->SleepFor(
      NanosToTimeSpec(creation_backoff_envelope_ns_ * random_factor));
  creation_backoff_envelope_ns_ = std::min(
      static_cast<int64_t>(creation_backoff_envelope_ns_ * kBackoffFactor),
      kMaxBackoffNanos);
}

void APIThrottler::ResetClientContext() {
  std::lock_guard<std::mutex> lock(ctx_mutex_);
  ctx_.reset(new grpc::ClientContext());  // NOLINT
  ctx_->AddMetadata("x-goog-api-client",
                    "gccl/" + std::string(CLOUD_PROFILER_AGENT_VERSION) +
                        " gl-java/" + java_version_);

  if (closed_) {
    ctx_->TryCancel();
  }
}

void APIThrottler::Close() {
  std::lock_guard<std::mutex> lock(ctx_mutex_);
  closed_ = true;
  if (ctx_) {
    ctx_->TryCancel();
  }
}

}  // namespace profiler
}  // namespace cloud
