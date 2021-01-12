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

#include "src/cloud_env.h"

#include <cstdlib>
#include <sstream>
#include <string>

#include "src/clock.h"
#include "src/http.h"
#include "src/string.h"
#include "third_party/absl/flags/flag.h"

DEFINE_int32(cprof_gce_metadata_server_retry_count, 3,
             "Number of retries to Google Compute Engine metadata host");
DEFINE_int32(
    cprof_gce_metadata_server_retry_sleep_sec, 1,
    "Seconds to sleep between retries to Google Compute Engine metadata host");
DEFINE_string(cprof_gce_metadata_server_address, "169.254.169.254:80",
              "Google Compute Engine metadata host to use");
DEFINE_string(cprof_access_token_test_only, "",
              "override OAuth2 access token for testing");
DEFINE_string(cprof_project_id, "", "cloud project ID");
DEFINE_string(cprof_zone_name, "", "zone name");
DEFINE_string(cprof_service, "", "deployment service name");
DEFINE_string(cprof_service_version, "", "deployment service version");

DEFINE_string(cprof_target, "", "deprecated, use -cprof_service instead");

namespace cloud {
namespace profiler {

const char kTokenPath[] =
    "/computeMetadata/v1/instance/service-accounts/default/token?alt=text";
const char kProjectIDPath[] = "/computeMetadata/v1/project/project-id";
const char kZoneNamePath[] = "/computeMetadata/v1/instance/zone";

const char kNoData[] = "";

namespace {

std::string GceMetadataRequest(HTTPRequest* req, const std::string& path) {
  Clock* clock = DefaultClock();
  req->AddHeader("Metadata-Flavor", "Google");
  req->SetTimeout(2);  // seconds

  std::string url =
                  absl::GetFlag(FLAGS_cprof_gce_metadata_server_address) + path,
              resp;

  int retry_sleep_sec =
      absl::GetFlag(FLAGS_cprof_gce_metadata_server_retry_sleep_sec);
  int retry_count = absl::GetFlag(FLAGS_cprof_gce_metadata_server_retry_count);
  struct timespec retry_ts = NanosToTimeSpec(kNanosPerSecond * retry_sleep_sec);

  for (int i = 0; i <= retry_count; i++) {
    if (!req->DoGet(url, &resp)) {
      if (i < retry_count) {
        LOG(ERROR) << "Error making HTTP request for " << url
                   << " to the GCE metadata server. "
                   << "Will retry in " << retry_ts.tv_sec << "s";
        clock->SleepFor(retry_ts);
      } else {
        LOG(ERROR) << "Error making HTTP request for " << url
                   << " to the GCE metadata server.";
      }
      continue;
    }
    int resp_code = req->GetResponseCode();
    if (resp_code != kHTTPStatusOK) {
      LOG(ERROR) << "Request to the GCE metadata server for " << url
                 << " failed, status code: " << resp_code;
      return kNoData;
    }
    return resp;
  }
  LOG(ERROR) << "Unable to contact GCE metadata server for " << url << " after "
             << retry_count << " retries.";
  return kNoData;
}

const char* Getenv(const std::string& var) {
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 17) || ALPINE == 1
  return secure_getenv(var.c_str());
#else
  return __secure_getenv(var.c_str());
#endif
}

}  // namespace

CloudEnv::CloudEnv() {
  if (!absl::GetFlag(FLAGS_cprof_service).empty()) {
    service_ = FLAGS_cprof_service;
  } else if (!absl::GetFlag(FLAGS_cprof_target).empty()) {
    service_ = FLAGS_cprof_target;
  } else {
    for (const std::string& env_var : {"GAE_SERVICE", "K_SERVICE"}) {
      const char* val = Getenv(env_var);
      if (val != nullptr) {
        service_ = val;
        break;
      }
    }
  }

  if (!absl::GetFlag(FLAGS_cprof_service_version).empty()) {
    service_version_ = FLAGS_cprof_service_version;
  } else {
    for (const std::string& env_var : {"GAE_VERSION", "K_REVISION"}) {
      const char* val = Getenv(env_var);
      if (val != nullptr) {
        service_version_ = val;
        break;
      }
    }
  }

  if (!absl::GetFlag(FLAGS_cprof_project_id).empty()) {
    project_id_ = FLAGS_cprof_project_id;
    LOG(INFO) << "Using project ID '" << project_id_ << "' from flags";
  } else {
    const char* val = Getenv("GOOGLE_CLOUD_PROJECT");
    project_id_ = val == nullptr ? "" : val;
    if (!project_id_.empty()) {
      LOG(INFO) << "Using project ID '" << project_id_ << "' from environment";
    } else {
      LOG(INFO) << "Project ID is not set via flag or environment, "
                << "will get from the metadata server";
    }
  }

  if (!absl::GetFlag(FLAGS_cprof_zone_name).empty()) {
    zone_name_ = FLAGS_cprof_zone_name;
    LOG(INFO) << "Using zone name '" << zone_name_ << "' from flags";
  }
}

std::string CloudEnv::ProjectID() {
  HTTPRequest req;
  return ProjectID(&req);
}

std::string CloudEnv::ProjectID(HTTPRequest* req) {
  if (!project_id_.empty()) {
    return project_id_;
  }

  std::string resp = GceMetadataRequest(req, kProjectIDPath);
  if (resp == kNoData) {
    LOG(ERROR) << "Failed to read the project ID from the VM metadata";
    return resp;
  }

  project_id_ = resp;
  return project_id_;
}

std::string CloudEnv::ZoneName() {
  HTTPRequest req;
  return ZoneName(&req);
}

std::string CloudEnv::ZoneName(HTTPRequest* req) {
  if (!zone_name_.empty()) {
    return zone_name_;
  }

  std::string resp = GceMetadataRequest(req, kZoneNamePath);
  if (resp == kNoData) {
    LOG(ERROR) << "Failed to read the zone name";
    return kNoData;
  }

  std::vector<std::string> elems = Split(resp, '/');
  if (elems.empty() || elems.back().empty()) {
    LOG(ERROR) << "Failed to parse the zone name";
    return kNoData;
  }

  zone_name_ = elems.back();
  return zone_name_;
}

std::string CloudEnv::Oauth2AccessToken() {
  HTTPRequest req;
  return Oauth2AccessToken(&req);
}

std::string CloudEnv::Oauth2AccessToken(HTTPRequest* req) {
  if (!absl::GetFlag(FLAGS_cprof_access_token_test_only).empty()) {
    LOG(WARNING) << "Using access token from flags, test-only";
    return FLAGS_cprof_access_token_test_only;
  }

  // TODO: Cache the access token as it's valid for ~1 hour.
  std::string resp = GceMetadataRequest(req, kTokenPath);
  if (resp == kNoData) {
    LOG(ERROR) << "Failed to acquire an access token";
    return resp;
  }

  std::vector<std::string> lines = Split(resp, '\n');
  for (const std::string& line : lines) {
    std::vector<std::string> pair = Split(line, ' ');
    if (pair.size() != 2) {
      LOG(ERROR) << "'" << line << "'"
                 << " malformed: two tokens expected";
      continue;
    }
    if (pair[0] == "access_token") {
      return pair[1];
    }
  }
  LOG(ERROR) << "Could not parse access token out of '" << resp << "'";
  return kNoData;
}

std::string CloudEnv::Service() { return service_; }

std::string CloudEnv::ServiceVersion() { return service_version_; }

CloudEnv* DefaultCloudEnv() {
  // Deferred initialization to make sure the flags are parsed.
  static CloudEnv cloud_env;
  return &cloud_env;
}

}  // namespace profiler
}  // namespace cloud
