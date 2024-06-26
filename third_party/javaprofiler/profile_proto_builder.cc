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

#include "third_party/javaprofiler/display.h"
#include "third_party/javaprofiler/stacktrace_fixer.h"
#include "third_party/javaprofiler/profile_proto_builder.h"

using std::string;
using std::unique_ptr;
using std::vector;

namespace google {
namespace javaprofiler {

namespace {

// Hashes a string using an initial hash value of h.
size_t HashString(const string &str, size_t h) {
  hash<string> hash_string;
  return 31U * h + hash_string(str);
}

const int kCount = 0;
const int kMetric = 1;

}  // namespace

ProfileProtoBuilder::ProfileProtoBuilder(JNIEnv *jni_env, jvmtiEnv *jvmti_env,
                                         ProfileFrameCache *native_cache,
                                         int64_t sampling_rate,
                                         const SampleType &count_type,
                                         const SampleType &metric_type,
                                         bool skip_top_native_frames,
                                         vector<string> skip_frames)
    : sampling_rate_(sampling_rate),
      jni_env_(jni_env),
      jvmti_env_(jvmti_env),
      method_info_cache_(jni_env, jvmti_env),
      native_cache_(native_cache),
      location_builder_(&builder_),
      skip_top_native_frames_(skip_top_native_frames)
      {
  AddSampleType(count_type);
  AddSampleType(metric_type);
  builder_.mutable_profile()->set_default_sample_type(
      builder_.StringId(metric_type.type.c_str()));

  SetPeriodType(metric_type);
}

void ProfileProtoBuilder::AddTraces(const ProfileStackTrace *traces,
                                    int num_traces) {
  if (native_cache_) {
    native_cache_->ProcessTraces(traces, num_traces);
  }

  for (int i = 0; i < num_traces; ++i) {
    AddTrace(traces[i], 1);
  }
}

void ProfileProtoBuilder::AddTraces(const ProfileStackTrace *traces,
                                    const int32_t *counts, int num_traces) {
  if (native_cache_) {
    native_cache_->ProcessTraces(traces, num_traces);
  }

  for (int i = 0; i < num_traces; ++i) {
    AddTrace(traces[i], counts[i]);
  }
}

void ProfileProtoBuilder::AddArtificialTrace(const string &name, int count,
                                             int sampling_rate) {
  perftools::profiles::Location *location = location_builder_.LocationFor(
      name, name, "", 0, -1, 0);

  auto profile = builder_.mutable_profile();
  auto sample = profile->add_sample();
  sample->add_location_id(location->id());
  // Move count * sampling rate to 64-bit.
  InitSampleValues(sample, count, static_cast<int64_t>(count) * sampling_rate);
}

void ProfileProtoBuilder::UnsampleMetrics() {
  auto profile = builder_.mutable_profile();

  for (int i = 0; i < profile->sample_size(); ++i) {
    auto sample = profile->mutable_sample(i);

    auto count = sample->value(kCount);
    auto metric_value = sample->value(kMetric);
    double ratio = CalculateSamplingRatio(sampling_rate_, count, metric_value);

    sample->set_value(kCount, static_cast<double>(count) * ratio);
    sample->set_value(kMetric, static_cast<double>(metric_value) * ratio);
  }
}

bool ProfileProtoBuilder::SkipFrame(
    const string &function_name) const {
  bool result = false;
  return result;
}

int ProfileProtoBuilder::SkipTopNativeFrames(const JVMPI_CallTrace &trace) {
  if (!skip_top_native_frames_) {
    return 0;
  }

  // Skip until we see the first Java frame.
  for (int i = 0; i < trace.num_frames; i++) {
    if (trace.frames[i].lineno != kNativeFrameLineNum) {
      return i;
    }
  }

  // All are native is weird for Java heap samples but do nothing in this
  // case.
  return 0;
}

unique_ptr<perftools::profiles::Profile>
    ProfileProtoBuilder::CreateSampledProto() {
#ifndef STANDALONE_BUILD
  builder_.AddCurrentMappings();
#endif
  builder_.Finalize();
  return unique_ptr<perftools::profiles::Profile>(builder_.Consume());
}

unique_ptr<perftools::profiles::Profile>
    ProfileProtoBuilder::CreateUnsampledProto() {
#ifndef STANDALONE_BUILD
  builder_.AddCurrentMappings();
#endif
  UnsampleMetrics();
  builder_.Finalize();
  return unique_ptr<perftools::profiles::Profile>(builder_.Consume());
}

void ProfileProtoBuilder::AddSampleType(const SampleType &sample_type) {
  auto sample_type_proto = builder_.mutable_profile()->add_sample_type();
  sample_type_proto->set_type(builder_.StringId(sample_type.type.c_str()));
  sample_type_proto->set_unit(builder_.StringId(sample_type.unit.c_str()));
}

void ProfileProtoBuilder::SetPeriodType(const SampleType &metric_type) {
  auto period_type = builder_.mutable_profile()->mutable_period_type();
  period_type->set_type(builder_.StringId(metric_type.type.c_str()));
  period_type->set_unit(builder_.StringId(metric_type.unit.c_str()));
}

void ProfileProtoBuilder::UpdateSampleValues(
    perftools::profiles::Sample *sample, int64_t count, int64_t size) {
  sample->set_value(kCount, sample->value(kCount) + count);
  sample->set_value(kMetric, sample->value(kMetric) + size);
}

void ProfileProtoBuilder::InitSampleValues(perftools::profiles::Sample *sample,
                                           int64_t count, int64_t metric) {
  sample->add_value(count);
  sample->add_value(metric);
}

void ProfileProtoBuilder::AddLabels(const TraceAndLabels &trace_and_labels,
                                    perftools::profiles::Sample *sample) {
  for (const auto &label : trace_and_labels.labels) {
    auto new_label = sample->add_label();
    new_label->set_key(builder_.StringId(label.key.c_str()));

    if (label.is_string_label) {
      new_label->set_str(builder_.StringId(label.str_label.c_str()));
    } else {
      new_label->set_num(label.num_label.value);
      // Note that if the unit is not defined in the label, it will be empty and
      // thus end up being a 0 as, by definition, "" has 0 as a string Id.
      new_label->set_num_unit(builder_.StringId(label.num_label.unit.c_str()));
    }
  }
}

void ProfileProtoBuilder::AddTrace(const ProfileStackTrace &profile_trace,
                                   int32_t count) {
  const TraceAndLabels &trace_and_labels = profile_trace.trace_and_labels;
  auto sample = trace_samples_.SampleFor(trace_and_labels);
  auto metric_value = profile_trace.metric_value;

  if (sample != nullptr) {
    UpdateSampleValues(sample, count, metric_value);
    return;
  }

  auto profile = builder_.mutable_profile();
  sample = profile->add_sample();
  trace_samples_.Add(trace_and_labels, sample);

  AddLabels(trace_and_labels, sample);

  InitSampleValues(sample, count, metric_value);

  const JVMPI_CallTrace *trace = trace_and_labels.trace;
  int first_frame = SkipTopNativeFrames(*trace);

  for (int i = first_frame; i < trace->num_frames; ++i) {
    auto &jvm_frame = trace->frames[i];

    if (jvm_frame.lineno == kNativeFrameLineNum) {
      AddNativeInfo(jvm_frame, profile, sample);
    } else {
      AddJavaInfo(jvm_frame, profile, sample);
    }
  }
}

void ProfileProtoBuilder::AddJavaInfo(
    const JVMPI_CallFrame &jvm_frame,
    perftools::profiles::Profile *profile,
    perftools::profiles::Sample *sample) {
  if (!jvm_frame.method_id) {
    perftools::profiles::Location *location = location_builder_.LocationFor(
        "", "[Unknown method]", "", 0, 0, 0);
    sample->add_location_id(location->id());
    return;
  }

  MethodInfo *method_info = method_info_cache_.Method(jvm_frame.method_id);

  int64_t line_number = method_info->LineNumber(jvm_frame);

  perftools::profiles::Location *location =
      location_builder_.LocationFor(method_info->ClassName(),
                                    method_info->MethodName(),
                                    method_info->FileName(),
                                    method_info->StartLine(),
                                    line_number,
                                    0);

  sample->add_location_id(location->id());
}

MethodInfoCache::MethodInfoCache(JNIEnv *jni_env, jvmtiEnv *jvmti_env)
    : jni_env_(jni_env), jvmti_env_(jvmti_env) {
}

MethodInfo *MethodInfoCache::Method(jmethodID method_id) {
  auto it = methods_.find(method_id);
  if (it != methods_.end()) {
    return it->second.get();
  }

  string file_name;
  string class_name;
  string method_name;
  string signature;
  int start_line = 0;

  // Set lineno to zero to get method first line.
  JVMPI_CallFrame jvm_frame = { 0, method_id };
  GetStackFrameElements(jni_env_, jvmti_env_, jvm_frame, &file_name,
                        &class_name, &method_name, &signature, &start_line);

  FixMethodParameters(&signature);
  string full_method_name = class_name + "." + method_name + signature;

  unique_ptr<MethodInfo> unique_method(
      new MethodInfo(jvmti_env_, full_method_name, class_name, file_name,
                     start_line));

  auto method_ptr = unique_method.get();
  methods_[method_id] = std::move(unique_method);
  return method_ptr;
}

void ProfileProtoBuilder::AddNativeInfo(const JVMPI_CallFrame &jvm_frame,
                                        perftools::profiles::Profile *profile,
                                        perftools::profiles::Sample *sample) {
  if (!native_cache_) {
    int64_t address = reinterpret_cast<int64_t>(jvm_frame.method_id);
    perftools::profiles::Location *location = location_builder_.LocationFor(
        "", "[Unknown non-Java frame]", "", 0, kNativeFrameLineNum, address);
    sample->add_location_id(location->id());
    return;
  }

  string function_name = native_cache_->GetFunctionName(jvm_frame);

  if (SkipFrame(function_name)) {
    return;
  }

  perftools::profiles::Location *location =
      native_cache_->GetLocation(jvm_frame, &location_builder_);

  sample->add_location_id(location->id());
}

void ContentionProfileProtoBuilder::MultiplyBySamplingRate() {
  auto profile = builder_.mutable_profile();

  for (int i = 0; i < profile->sample_size(); ++i) {
    auto sample = profile->mutable_sample(i);

    auto count = sample->value(kCount);
    auto metric_value = sample->value(kMetric);

    sample->set_value(kCount, static_cast<double>(count) * sampling_rate_);
    sample->set_value(kMetric,
                      static_cast<double>(metric_value) * sampling_rate_);
  }
}

size_t LocationBuilder::LocationInfoHash::operator()(
    const LocationInfo &info) const {

  unsigned int h = 1;

  hash<string> hash_string;
  hash<int> hash_int;

  h = 31U * h + hash_string(info.class_name);
  h = 31U * h + hash_string(info.function_name);
  h = 31U * h + hash_string(info.file_name);
  h = 31U * h + hash_int(info.line_number);
  h = 31U * h + hash_int(info.address);

  return h;
}

bool LocationBuilder::LocationInfoEquals::operator()(
    const LocationInfo &info1, const LocationInfo &info2) const {
  return info1.class_name == info2.class_name &&
      info1.function_name == info2.function_name &&
      info1.file_name == info2.file_name &&
      info1.line_number == info2.line_number &&
      info1.address == info2.address;
}

perftools::profiles::Location *LocationBuilder::LocationFor(
    const string &class_name, const string &function_name,
    const string &file_name, int start_line, int line_number,
    int64_t address) {
  auto profile = builder_->mutable_profile();

  // Adjust address by -1, so that the address is within the call instruction
  // instead of at the start of the return instruction, as per
  // go/new-z-page#ret-addr-fixup
  //
  // We use 0 (NULL) to indicate the absence of a valid address, and so
  // do not do this where it would be invalid i.e. leading to -1
  address = (address > 0) ? address - 1 : 0;

  LocationInfo info {
      class_name,
      function_name,
      file_name,
      line_number,
      address
  };

  if (locations_.count(info) > 0) {
    return locations_.find(info)->second;
  }

  auto location_id = profile->location_size() + 1;
  perftools::profiles::Location *location = profile->add_location();
  location->set_id(location_id);
  location->set_address(address);

  locations_[info] = location;

  // If this is a native frame with no symbolization, we don't want to add a
  // line number to the profile.
  if (line_number != kNativeFrameLineNum) {
    auto line = location->add_line();

    string simplified_name = function_name;
    SimplifyFunctionName(&simplified_name);
    auto function_id =
        builder_->FunctionId(simplified_name.c_str(), function_name.c_str(),
                            file_name.c_str(), start_line);

    line->set_function_id(function_id);
    line->set_line(line_number);
  }

  return location;
}

bool SampleLabel::operator==(const SampleLabel &other) const {
  if (other.key != key) {
    return false;
  }

  if (other.is_string_label != is_string_label) {
    return false;
  }

  if (other.is_string_label) {
    return other.str_label == str_label;
  }
  return other.num_label == num_label;
}

size_t SampleLabel::Hash(size_t current_hash_value) const {
  size_t result = HashString(key, current_hash_value);

  result = 31U * result + (is_string_label ? 1 : 0);

  if (is_string_label) {
    return HashString(str_label, result);
  } else {
    return num_label.Hash(result);
  }
}

size_t NumLabelValue::Hash(size_t current_hash_value) const {
  hash<int> hash_int;
  current_hash_value = 31U * current_hash_value + hash_int(value);
  return HashString(unit, current_hash_value);
}

size_t TraceSamples::TraceHash::operator()(
    const TraceAndLabels &trace_values) const {
  unsigned int hash = 1;
  const JVMPI_CallTrace *trace = trace_values.trace;
  for (int f = 0; f < trace->num_frames; f++) {
    {
      int len = sizeof(jint);
      char *arr = reinterpret_cast<char *>(&trace->frames[f].lineno);
      for (int i = 0; i < len; i++) {
        hash = 31U * hash + arr[i];
      }
    }
    {
      int len = sizeof(jmethodID);
      char *arr = reinterpret_cast<char *>(&trace->frames[f].method_id);
      for (int i = 0; i < len; i++) {
        hash = 31U * hash + arr[i];
      }
    }
  }

  for (const SampleLabel &label : trace_values.labels) {
    hash = label.Hash(hash);
  }

  return hash;
}

bool TraceSamples::TraceEquals::operator()(
    const TraceAndLabels &trace_values1,
    const TraceAndLabels &trace_values2) const {
  if (trace_values1.labels != trace_values2.labels) {
    return false;
  }

  const JVMPI_CallTrace *trace1 = trace_values1.trace;
  const JVMPI_CallTrace *trace2 = trace_values2.trace;
  if (trace1->num_frames != trace2->num_frames) {
    return false;
  }

  for (int i = 0; i < trace1->num_frames; ++i) {
    if (trace1->frames[i].method_id != trace2->frames[i].method_id) {
      return false;
    }

    if (trace1->frames[i].lineno != trace2->frames[i].lineno) {
      return false;
    }
  }

  return true;
}

perftools::profiles::Sample *TraceSamples::SampleFor(
    const TraceAndLabels &trace) const {
  auto found = traces_.find(trace);
  if (found == traces_.end()) {
    return nullptr;
  }
  return found->second;
}

void TraceSamples::Add(const TraceAndLabels &trace,
                       perftools::profiles::Sample *sample) {
  traces_[trace] = sample;
}

double CalculateSamplingRatio(int64_t rate, int64_t count,
                              int64_t metric_value) {
  if (rate <= 1 || count < 1 || metric_value < 1) {
    return 1.0;
  }

  double m = static_cast<double>(metric_value);
  double c = static_cast<double>(count);
  double r = static_cast<double>(rate);

  double size = m / c;

  return 1 / (1 - exp(-size / r));
}

unique_ptr<ProfileProtoBuilder> ProfileProtoBuilder::ForHeap(
    JNIEnv *jni_env, jvmtiEnv *jvmti_env, int64_t sampling_rate,
    ProfileFrameCache *cache) {
  // Cache can be nullptr because the heap sampler can be using a JVMTI
  // Java-only stackframe gatherer.
  return unique_ptr<ProfileProtoBuilder>(
      new HeapProfileProtoBuilder(jni_env, jvmti_env, sampling_rate, cache,
                                  true, {}));
}

unique_ptr<ProfileProtoBuilder> ProfileProtoBuilder::ForCpu(
    JNIEnv *jni_env, jvmtiEnv *jvmti_env, int64_t duration_ns,
    int64_t sampling_rate, ProfileFrameCache *cache) {
  CHECK (cache != nullptr)
      << "CPU profiles may have native frames, cache must be provided";
  return unique_ptr<ProfileProtoBuilder>(
      new CpuProfileProtoBuilder(jni_env, jvmti_env, duration_ns,
                                 sampling_rate, cache, false, {}));
}

unique_ptr<ProfileProtoBuilder> ProfileProtoBuilder::ForContention(
    JNIEnv *jni_env, jvmtiEnv *jvmti_env, int64_t sampling_rate,
    int64_t duration_nanos, ProfileFrameCache *cache) {
  CHECK (cache != nullptr)
      << "Contention profiles may have native frames, cache must be provided";
  return unique_ptr<ProfileProtoBuilder>(new ContentionProfileProtoBuilder(
      jni_env, jvmti_env, sampling_rate, duration_nanos, cache, false, {}));
}

}  // namespace javaprofiler
}  // namespace google
