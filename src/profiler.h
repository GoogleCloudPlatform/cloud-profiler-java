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

#ifndef CLOUD_PROFILER_AGENT_JAVA_PROFILER_H_
#define CLOUD_PROFILER_AGENT_JAVA_PROFILER_H_

#include <signal.h>

#include <atomic>

#include "src/threads.h"
#include "third_party/javaprofiler/stacktraces.h"

namespace cloud {
namespace profiler {

class SignalHandler {
 public:
  SignalHandler() {}

  struct sigaction SetAction(void (*sigaction)(int, siginfo_t *, void *));

  bool SetSigprofInterval(int64_t period_usec);

 private:
  DISALLOW_COPY_AND_ASSIGN(SignalHandler);
};

class Profiler {
 public:
  Profiler(jvmtiEnv *jvmti, ThreadTable *threads, int64_t duration_nanos,
           int64_t period_nanos)
      : threads_(threads),
        duration_nanos_(duration_nanos),
        period_nanos_(period_nanos),
        jvmti_(jvmti) {
    Reset();
  }
  virtual ~Profiler() {}

  // Collect performance data.
  // Implicitly does a Reset() before starting collection.
  virtual bool Collect() = 0;

  // Serialize the collected traces into a compressed serialized profile.proto
  string SerializeProfile(
      JNIEnv *jni, const google::javaprofiler::NativeProcessInfo &native_info);

  // Signal handler, which records the current stack trace into the profile.
  static void Handle(int signum, siginfo_t *info, void *context);

  // Reset internal state to support data collection.
  void Reset();

  // Migrate data from fixed internal table into growable data structure.
  // Returns number of entries extracted.
  int Flush() { return HarvestSamples(fixed_traces_, &aggregated_traces_); }

  // String description of the profile type
  virtual const char *ProfileType() = 0;

 protected:
  ThreadTable *threads_;
  SignalHandler handler_;
  int64_t duration_nanos_;
  int64_t period_nanos_;

 private:
  // Points to a fixed multiset of traces used during collection. This
  // is allocated on the first call to Reset(). Will be reused by
  // subsequent allocations. Cannot be deallocated as it could be in
  // use by other threads, triggered from a signal handler.
  static google::javaprofiler::AsyncSafeTraceMultiset *fixed_traces_;

  // Aggregated profile data, populated using data extracted from
  // fixed_traces.
  google::javaprofiler::TraceMultiset aggregated_traces_;
  jvmtiEnv *jvmti_;

  struct sigaction old_action_;

  // Number of samples where the stack aggregation failed.
  static std::atomic<int> unknown_stack_count_;

  DISALLOW_COPY_AND_ASSIGN(Profiler);
};

// CPUProfiler collects cpu profiles by setting up a CPU timer and
// collecting a sample each time it is triggered (via SIGPROF).
class CPUProfiler : public Profiler {
 public:
  using Profiler::Profiler;

  // Collect profiling data.
  bool Collect() override;

  const char *ProfileType() override { return "cpu"; }

 private:
  // Initiate data collection at a fixed interval
  bool Start();

  // Stop data collection
  void Stop();

  DISALLOW_COPY_AND_ASSIGN(CPUProfiler);
};

// WallProfiler collects wallclock profiles by explicitly sending
// SIGPROF to each thread in the thread table.
class WallProfiler : public Profiler {
 public:
  WallProfiler(jvmtiEnv *jvmti, ThreadTable *threads, int64_t duration_nanos,
               int64_t period_nanos);

  // Collect profiling data.
  bool Collect() override;

  // Compute effective period based on desired overhead parameters.
  static int64_t EffectivePeriodNanos(int64_t num_threads,
                                      int64_t max_threads_per_second,
                                      int64_t period_nanos,
                                      int64_t duration_nanos);

  const char *ProfileType() override { return "wall"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(WallProfiler);
};

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_PROFILER_H_
