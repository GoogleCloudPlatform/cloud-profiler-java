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

#include "src/proto.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>

#include <map>
#include <string>

#include "perftools/profiles/proto/builder.h"
#include "third_party/javaprofiler/display.h"
#include "third_party/javaprofiler/stacktrace_fixer.h"

namespace cloud {
namespace profiler {

// Encodes a set of java stack traces into a CPU profile, symbolized using
// the jvmti.
class ProfileProtoBuilder {
 public:
  ProfileProtoBuilder(
      jvmtiEnv *jvmti,
      const google::javaprofiler::NativeProcessInfo &native_info)
      : jvmti_(jvmti), native_info_(native_info) {
    for (const auto &it : google::javaprofiler::AttributeTable::GetStrings()) {
      builder_.StringId(it.c_str());
    }
  }

  // Populate the profile with a set of traces
  void Populate(JNIEnv *jni, const char *profile_type,
                const google::javaprofiler::TraceMultiset &traces,
                int64_t duration_ns, int64_t period_ns);
  void AddArtificialSample(const std::string &name, int64_t count,
                           int64_t weight);
  int64_t TotalCount() const;
  int64_t TotalWeight() const;

  std::string Emit() {
    std::string out;
    builder_.Emit(&out);
    return out;
  }
  void Encode(perftools::profiles::Profile *p) {
    p->Swap(builder_.mutable_profile());
  }

 private:
  void AddSample(const std::vector<uint64_t> &locations, int64_t count,
                 int64_t weight, int64_t attr);
  uint64_t LocationID(JNIEnv *jni,
                      const google::javaprofiler::JVMPI_CallFrame &frame);
  uint64_t LocationID(uint64_t address);
  uint64_t LocationID(const std::string &name);
  uint64_t LocationID(const std::string &class_name,
                      const std::string &method_name,
                      const std::string &signature,
                      const std::string &file_name, int line_number);

  jvmtiEnv *jvmti_;
  int64_t total_count_ = 0;
  int64_t total_weight_ = 0;
  perftools::profiles::Builder builder_;

  typedef std::tuple<uint64_t, int> Line;
  class LineHasher {
   public:
    size_t operator()(const Line &f) const {
      size_t hash = std::get<0>(f);
      hash = hash + ((hash << 8) ^ std::get<1>(f));
      return static_cast<size_t>(hash);
    }
  };

  std::unordered_map<Line, uint64_t, LineHasher> line_map_;
  std::unordered_map<uint64_t, uint64_t> address_location_;

  const google::javaprofiler::NativeProcessInfo &native_info_;
  DISALLOW_COPY_AND_ASSIGN(ProfileProtoBuilder);
};

namespace {

std::string CallTraceErrorToName(int64_t err) {
  switch (err) {
    case kNativeStackTrace:
      return "[Native code]";
    case kNoClassLoad:
      return "[No class load event]";
    case kGcActive:
      return "[GC active]";
    case kUnknownNotJava:
    case kNotWalkableFrameNotJava:
      return "[Unknown non-Java frame]";
    case kUnknownJava:
    case kNotWalkableFrameJava:
      return "[Unknown Java frame]";
    case kUnknownState:
      return "[Unknown state]";
    case kThreadExit:
      return "[Thread exiting]";
    case kDeopt:
      return "[Deopt]";
    case kSafepoint:
      return "[Safepoint]";
    default:
      return "[Unknown]";
  }
}

}  // namespace

void ProfileProtoBuilder::AddArtificialSample(const std::string &name,
                                              int64_t count, int64_t weight) {
  AddSample({LocationID(name)}, count, weight, 0);
}

int64_t ProfileProtoBuilder::TotalCount() const { return total_count_; }

int64_t ProfileProtoBuilder::TotalWeight() const { return total_weight_; }

uint64_t ProfileProtoBuilder::LocationID(
    JNIEnv *jni, const google::javaprofiler::JVMPI_CallFrame &frame) {
  if (frame.lineno == google::javaprofiler::kNativeFrameLineNum) {
    return LocationID(reinterpret_cast<uint64_t>(frame.method_id));
  }

  if (frame.lineno == google::javaprofiler::kCallTraceErrorLineNum) {
    return LocationID(
        CallTraceErrorToName(reinterpret_cast<size_t>(frame.method_id)));
  }

  std::string method_name, class_name, file_name, signature;
  int line_number = 0;
  google::javaprofiler::GetStackFrameElements(jni, jvmti_, frame, &file_name,
                                              &class_name, &method_name,
                                              &signature, &line_number);
  google::javaprofiler::FixMethodParameters(&signature);

  return LocationID(class_name, method_name, signature, file_name, line_number);
}

uint64_t ProfileProtoBuilder::LocationID(uint64_t address) {
  uint64_t location_id = address_location_[address];
  if (location_id != 0) {
    return location_id;
  }

  perftools::profiles::Profile *profile = builder_.mutable_profile();
  location_id = profile->location_size() + 1;
  address_location_[address] = location_id;

  perftools::profiles::Location *loc = profile->add_location();
  loc->set_id(location_id);
  loc->set_address(address);

  return location_id;
}

uint64_t ProfileProtoBuilder::LocationID(const std::string &name) {
  return LocationID("", name, "", "", 0);
}

uint64_t ProfileProtoBuilder::LocationID(const std::string &class_name,
                                         const std::string &method_name,
                                         const std::string &signature,
                                         const std::string &file_name,
                                         int line_number) {
  perftools::profiles::Profile *profile = builder_.mutable_profile();

  std::string frame_name;
  if (!class_name.empty()) {
    frame_name = class_name + ".";
  }
  frame_name += method_name;

  if (!signature.empty()) {
    frame_name += signature;
  }

  std::string simplified_name = frame_name;
  ::google::javaprofiler::SimplifyFunctionName(&simplified_name);

  uint64_t function_id = builder_.FunctionId(
      simplified_name.c_str(), frame_name.c_str(), file_name.c_str(), 0);

  uint64_t location_id = profile->location_size() + 1;
  Line function_line(function_id, line_number);
  auto inserted = line_map_.insert(std::make_pair(function_line, location_id));
  if (!inserted.second) {
    return inserted.first->second;
  }

  perftools::profiles::Location *loc = profile->add_location();
  loc->set_id(location_id);
  perftools::profiles::Line *line = loc->add_line();
  line->set_function_id(function_id);
  line->set_line(line_number);

  return location_id;
}

void ProfileProtoBuilder::Populate(
    JNIEnv *jni, const char *profile_type,
    const google::javaprofiler::TraceMultiset &traces, int64_t duration_ns,
    int64_t period_ns) {
  perftools::profiles::Profile *profile = builder_.mutable_profile();

  profile->mutable_period_type()->set_type(builder_.StringId(profile_type));
  profile->mutable_period_type()->set_unit(builder_.StringId("nanoseconds"));
  profile->set_period(period_ns);
  perftools::profiles::ValueType *sample_type = profile->add_sample_type();
  sample_type->set_type(builder_.StringId("sample"));
  sample_type->set_unit(builder_.StringId("count"));

  sample_type = profile->add_sample_type();
  sample_type->set_type(builder_.StringId(profile_type));
  sample_type->set_unit(builder_.StringId("nanoseconds"));

  profile->set_duration_nanos(duration_ns);

  for (const auto &trace : traces) {
    int64_t count = trace.second;
    if (count != 0) {
      std::vector<uint64_t> locations;
      for (const auto &frame : trace.first.frames) {
        locations.push_back(LocationID(jni, frame));
      }
      AddSample(locations, count, count * period_ns, trace.first.attr);
    }
  }

  for (const auto &mapping : native_info_.Mappings()) {
    perftools::profiles::Mapping *m = profile->add_mapping();
    m->set_id(profile->mapping_size());
    m->set_memory_start(mapping.start);
    m->set_memory_limit(mapping.limit);
    m->set_filename(builder_.StringId(mapping.name.c_str()));
  }
}

void ProfileProtoBuilder::AddSample(const std::vector<uint64_t> &locations,
                                    int64_t count, int64_t weight,
                                    int64_t attr) {
  perftools::profiles::Profile *profile = builder_.mutable_profile();

  perftools::profiles::Sample *sample = profile->add_sample();
  sample->add_value(count);
  total_count_ += count;
  sample->add_value(weight);
  total_weight_ += weight;

  for (const auto &location : locations) {
    sample->add_location_id(location);
  }

  if (attr != 0) {
    perftools::profiles::Label *label = sample->add_label();
    label->set_key(builder_.StringId("attr"));
    label->set_str(attr);
  }
}

std::string SerializeAndClearJavaCpuTraces(
    JNIEnv *env, jvmtiEnv *jvmti,
    const google::javaprofiler::NativeProcessInfo &native_info,
    const char *profile_type, int64_t duration_ns, int64_t period_ns,
    google::javaprofiler::TraceMultiset *traces, int64_t unknown_count) {
  ProfileProtoBuilder b(jvmti, native_info);
  b.Populate(env, profile_type, *traces, duration_ns, period_ns);
  b.AddArtificialSample("[Unknown]", unknown_count, unknown_count * period_ns);
  LOG(INFO) << "Collected a profile: total count=" << b.TotalCount()
            << ", weight=" << b.TotalWeight();

  traces->Clear();  // Release traces before binary encoding to reuse memory
  return b.Emit();
}

}  // namespace profiler
}  // namespace cloud
