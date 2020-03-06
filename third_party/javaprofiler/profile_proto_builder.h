/*
 * Copyright 2018 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef THIRD_PARTY_JAVAPROFILER_PROFILE_PROTO_BUILDER_H__
#define THIRD_PARTY_JAVAPROFILER_PROFILE_PROTO_BUILDER_H__

#include <jvmti.h>
#include <link.h>

#include <cmath>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "perftools/profiles/proto/builder.h"
#include "third_party/javaprofiler/method_info.h"
#include "third_party/javaprofiler/stacktrace_decls.h"

namespace google {
namespace javaprofiler {

struct ProfileStackTrace {
  JVMPI_CallTrace *trace;
  jint metric_value;
};

// Store proto sample objects for specific stack traces and label values.
class TraceSamples {
 public:
  perftools::profiles::Sample *SampleFor(
      const JVMPI_CallTrace *trace, const std::list<int64> &label_values) const;
  void Add(const JVMPI_CallTrace *trace, const std::list<int64> &label_values,
           perftools::profiles::Sample *sample);

 private:
  struct TraceAndValues {
    const JVMPI_CallTrace *trace;
    // Values for each label type in ProfileProtoBuilder::label_types_.
    const std::list<int64> label_values;
  };

  struct TraceHash {
    size_t operator()(const TraceAndValues &trace_values) const;
  };

  struct TraceEquals {
    bool operator()(const TraceAndValues &trace_values1,
                    const TraceAndValues &trace_values2) const;
  };

  std::unordered_map<TraceAndValues, perftools::profiles::Sample *,
      TraceHash, TraceEquals> traces_;
};

// Store locations previously seen so that the profile is only
// modified for new locations.
class LocationBuilder {
 public:
  explicit LocationBuilder(perftools::profiles::Builder *builder)
      : builder_(builder) {}

  // Return an existing or new location matching the given parameters,
  // modifying the profile as needed to add new function and location
  // information.
  perftools::profiles::Location *LocationFor(const std::string &class_name,
                                             const std::string &function_name,
                                             const std::string &file_name,
                                             int line_number);

 private:
  struct LocationInfo {
    std::string class_name;
    std::string function_name;
    std::string file_name;
    int line_number;
  };

  struct LocationInfoHash {
    size_t operator()(const LocationInfo &info) const;
  };

  struct LocationInfoEquals {
    bool operator()(const LocationInfo &info1, const LocationInfo &info2) const;
  };

  perftools::profiles::Builder *builder_;

  std::unordered_map<LocationInfo, perftools::profiles::Location *,
      LocationInfoHash, LocationInfoEquals> locations_;
};

// Remember traces and use the information to create locations with native
// information if supported.
class ProfileFrameCache {
 public:
  virtual void ProcessTraces(const ProfileStackTrace *traces,
                             int num_traces) = 0;

  virtual perftools::profiles::Location *GetLocation(
      const JVMPI_CallFrame &jvm_frame, LocationBuilder *location_builder) = 0;

  virtual std::string GetFunctionName(const JVMPI_CallFrame &jvm_frame) = 0;

  virtual ~ProfileFrameCache() {}
};

// Create profile protobufs from traces obtained from JVM profiling.
class ProfileProtoBuilder {
 public:
  virtual ~ProfileProtoBuilder() {}

  // Add traces to the proto. The traces array must not be deleted
  // before calling CreateProto.
  void AddTraces(const ProfileStackTrace *traces, int num_traces);

  // Add traces to the proto, where each trace has a defined count
  // of occurrences. The traces array must not be deleted
  // before calling CreateProto.
  void AddTraces(const ProfileStackTrace *traces,
                 const int32 *counts,
                 int num_traces);

  // Add traces to the proto, and append traces[i].metric_value as the label
  // value corresponding to the last label_type. The traces array must not be
  // deleted before calling CreateProto.
  void AddTracesAppendingMetricAsLabel(const ProfileStackTrace *traces,
                                       int num_traces);

  // Add traces to the proto, where each trace has a defined count of
  // occurrences, and append traces[i].metric_value as the label
  // value corresponding to the last label_type. The traces array must not be
  // deleted before calling CreateProto.
  void AddTracesAppendingMetricAsLabel(const ProfileStackTrace *traces,
                                       const int32 *counts,
                                       int num_traces);

  // Add a "fake" trace with a single frame. Used to represent JVM
  // tasks such as JIT compilation and GC.
  void AddArtificialTrace(const std::string &name, int count,
                          int sampling_rate);

  // Build the proto. Calling any other method on the class after calling
  // this has undefined behavior.
  virtual std::unique_ptr<perftools::profiles::Profile> CreateProto() = 0;

  // Create a heap profile.
  // jvmti_env can be null as well but then all calls to AddTraces will return
  // unknown.
  // ForHeap/ForNativeHeap is the only case where we accept a null cache since
  // the heap profiles can be using JVMTI's GetStackTrace and remain in pure
  // Java land frames. Other ForX methods will fail an assertion when attempting
  // a nullptr cache.
  static std::unique_ptr<ProfileProtoBuilder> ForHeap(
      JNIEnv *jni_env, jvmtiEnv *jvmti_env, int64 sampling_rate,
      ProfileFrameCache *cache = nullptr);

  static std::unique_ptr<ProfileProtoBuilder> ForNativeHeap(
      JNIEnv *jni_env, jvmtiEnv *jvmti_env, int64 sampling_rate,
      ProfileFrameCache *cache = nullptr);

  static std::unique_ptr<ProfileProtoBuilder> ForCpu(JNIEnv *jni_env,
                                                     jvmtiEnv *jvmti_env,
                                                     int64 duration_ns,
                                                     int64 sampling_rate,
                                                     ProfileFrameCache *cache);

  static std::unique_ptr<ProfileProtoBuilder> ForContention(
      JNIEnv *jni_env, jvmtiEnv *jvmti_env, int64 sampling_rate,
      ProfileFrameCache *cache);

 protected:
  struct SampleType {
    SampleType(const std::string &type_in, const std::string &unit_in)
        : type(type_in), unit(unit_in) {}

    std::string type;
    std::string unit;
  };

  // Create the profile proto builder, if native_cache is nullptr, then no
  // information about native frames can be provided. The proto buffer will then
  // contain "Unknown native method" frames.
  ProfileProtoBuilder(JNIEnv *env, jvmtiEnv *jvmti_env,
                      ProfileFrameCache *native_cache, int64 sampling_rate,
                      const SampleType &count_type,
                      const SampleType &metric_type,
                      const std::list<SampleType> &label_types);

  // An implementation must decide how many frames to skip in a trace.
  virtual int SkipTopNativeFrames(const JVMPI_CallTrace &trace) = 0;

  // Build the proto, unsampling the sample metrics. Calling any other method
  // on the class after calling this has undefined behavior.
  std::unique_ptr<perftools::profiles::Profile> CreateUnsampledProto();

  // Build the proto, without normalizing the sampled metrics. Calling any
  // other method on the class after calling this has undefined behavior.
  std::unique_ptr<perftools::profiles::Profile> CreateSampledProto();

  perftools::profiles::Builder builder_;
  int64 sampling_rate_ = 0;

 private:
  // Track progress through a stack as we traverse it, in order to determine
  // how processing should proceed based on the context of a frame.
  class StackState {
   public:
    StackState() {
    }

    // Notify the state that we are visiting a Java frame.
    void JavaFrame() {
      in_jni_helpers_ = false;
    }

    // Notify the state that we are visiting a native frame.
    void NativeFrame(const std::string &function_name) {
      if (StartsWith(function_name, "JavaCalls::call_helper")) {
        in_jni_helpers_ = true;
      }
    }

    // Should we skip the current frame?
    bool SkipFrame() const {
      return in_jni_helpers_;
    }

   private:
    // We don't add native frames that are just "helper" native code for
    // dispatching to JNI code. We determine this by detecting a native
    // JavaCalls::call_helper frame, then skipping until the we see a Java
    // frame again.
    // TODO: Support a "complete detail" mode to override this.
    bool in_jni_helpers_ = false;

    static bool StartsWith(const std::string &s, const std::string &prefix) {
      return s.find(prefix) == 0;
    }
  };

  void AddSampleType(const SampleType &sample_type);
  void SetPeriodType(const SampleType &metric_type);
  void InitSampleValues(perftools::profiles::Sample *sample, int64 count,
                        int64 metric, const std::list<int64> &label_values);
  void UpdateSampleValues(perftools::profiles::Sample *sample, int64 count,
                          int64 size);
  void AddTrace(const ProfileStackTrace &trace, int32 count,
                bool append_metric_value_as_label);
  void AddJavaInfo(const JVMPI_CallFrame &jvm_frame,
                   perftools::profiles::Profile *profile,
                   perftools::profiles::Sample *sample,
                   StackState *stack_state);
  void AddNativeInfo(const JVMPI_CallFrame &jvm_frame,
                     perftools::profiles::Profile *profile,
                     perftools::profiles::Sample *sample,
                     StackState *stack_state);
  void UnsampleMetrics();

  MethodInfo *Method(jmethodID id);
  int64 Location(MethodInfo *method, const JVMPI_CallFrame &frame);

  JNIEnv *jni_env_;
  jvmtiEnv *jvmti_env_;

  // Caching jmethodID resolution:
  //   - This allows a one-time calculation of a given jmethodID during proto
  //     creation.
  //   - The cache reduces the number of JVMTI calls to symbolize the stacks.
  //   - jmethodIDs are never invalid per se or re-used: if ever the jmethodID's
  //     class is unloaded, the JVMTI calls will return an error code that
  //     caught by the various JVMTI calls done.
  // Though it would theoretically be possible to cache the jmethodID for the
  // lifetime of the program, this just implementation keeps the cache live
  // during this proto creation. The reason is that jmethodIDs might become
  // stale/unloaded and there would need to be extra work to determine
  // cache-size management.
  std::unordered_map<jmethodID, std::unique_ptr<MethodInfo>> methods_;
  ProfileFrameCache *native_cache_;
  TraceSamples trace_samples_;
  LocationBuilder location_builder_;
  const std::list<SampleType> label_types_;
};

// Computes the ratio to use to scale heap data to unsample it.
// Accounts for the probability of it appearing in the
// collected data based on exponential samples. heap profiles rely
// on a poisson process to determine which samples to collect, based
// on the desired average collection rate R. The probability of a
// sample of size S to appear in that profile is 1-exp(-S/R).
double CalculateSamplingRatio(int64 rate, int64 count, int64 metric_value);

class CpuProfileProtoBuilder : public ProfileProtoBuilder {
 public:
  CpuProfileProtoBuilder(JNIEnv *jni_env, jvmtiEnv *jvmti_env,
                         int64 duration_ns, int64 sampling_rate,
                         ProfileFrameCache *cache)
      : ProfileProtoBuilder(
            jni_env, jvmti_env, cache, sampling_rate,
            ProfileProtoBuilder::SampleType("samples", "count"),
            ProfileProtoBuilder::SampleType("cpu", "nanoseconds"),
            std::list<ProfileProtoBuilder::SampleType>()) {
    builder_.mutable_profile()->set_duration_nanos(duration_ns);
    builder_.mutable_profile()->set_period(sampling_rate);
  }

  std::unique_ptr<perftools::profiles::Profile> CreateProto() override {
    return CreateSampledProto();
  }

 protected:
  int SkipTopNativeFrames(const JVMPI_CallTrace &trace) override { return 0; }
};

class HeapProfileProtoBuilder : public ProfileProtoBuilder {
 public:
  HeapProfileProtoBuilder(JNIEnv *jni_env, jvmtiEnv *jvmti_env,
                          int64 sampling_rate, ProfileFrameCache *cache)
      : ProfileProtoBuilder(
            jni_env, jvmti_env, cache, sampling_rate,
            ProfileProtoBuilder::SampleType("inuse_objects", "count"),
            ProfileProtoBuilder::SampleType("inuse_space", "bytes"),
            std::list<ProfileProtoBuilder::SampleType>({
                ProfileProtoBuilder::SampleType("bytes", "bytes")
            })) {}

  std::unique_ptr<perftools::profiles::Profile> CreateProto() override {
    return CreateUnsampledProto();
  }

 protected:
  // Depending on the JDK or how we got the frames (e.g. internal JVM or
  // JVMTI), we might have native frames on top of the Java frames
  // (basically where the JVM internally allocated the object).
  // Returns the first Java frame index or 0 if none were found.
  int SkipTopNativeFrames(const JVMPI_CallTrace &trace) override {
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
};

class NativeHeapProfileProtoBuilder : public HeapProfileProtoBuilder {
 public:
  NativeHeapProfileProtoBuilder(JNIEnv *jni_env, jvmtiEnv *jvmti_env,
                                int64 sampling_rate, ProfileFrameCache *cache)
      : HeapProfileProtoBuilder(jni_env, jvmti_env, sampling_rate, cache) {}

 protected:
  // In cases of long native frames, we really only want to skip the
  // frames_to_skip first frames, which would be something akin to:
  //   absl::base_internal::MallocHook::InvokeNewHookSlow
  //   absl::base_internal::MallocHook::InvokeNewHook
  //   calloc
  int SkipTopNativeFrames(const JVMPI_CallTrace &trace) override {
    // If frames_to_skip changes, change the number of frames checked against
    // kNativeFrameLineNum below to check the correct number of frames.
    const int frames_to_skip = 3;
    return (trace.num_frames >= frames_to_skip
            && trace.frames[0].lineno == kNativeFrameLineNum
            && trace.frames[1].lineno == kNativeFrameLineNum
            && trace.frames[2].lineno == kNativeFrameLineNum)
        ? frames_to_skip : 0;
  }
};

class ContentionProfileProtoBuilder : public ProfileProtoBuilder {
 public:
  ContentionProfileProtoBuilder(JNIEnv *jni_env, jvmtiEnv *jvmti_env,
                                int64 sampling_rate, ProfileFrameCache *cache)
      : ProfileProtoBuilder(
            jni_env, jvmti_env, cache, sampling_rate,
            ProfileProtoBuilder::SampleType("contentions", "count"),
            ProfileProtoBuilder::SampleType("delay", "microseconds"),
            std::list<ProfileProtoBuilder::SampleType>()) {
    builder_.mutable_profile()->set_period(sampling_rate);
  }

  std::unique_ptr<perftools::profiles::Profile> CreateProto() {
    MultiplyBySamplingRate();
    builder_.Finalize();
    return std::unique_ptr<perftools::profiles::Profile>(builder_.Consume());
  }

 protected:
  int SkipTopNativeFrames(const JVMPI_CallTrace &trace) override { return 0; }

 private:
  // Multiply the (count, metric) values by the sampling_rate.
  void MultiplyBySamplingRate();
};

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_PROFILE_PROTO_BUILDER_H__
