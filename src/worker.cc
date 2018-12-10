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

#include "src/worker.h"

#include "src/clock.h"
#include "src/profiler.h"
#include "src/throttler_api.h"
#include "src/throttler_timed.h"

DEFINE_bool(cprof_enabled, false,
            "when unset, unconditionally disable the profiling");
DEFINE_string(
    cprof_profile_filename, "",
    "when set to a path, store profiles locally at the specified prefix");
DEFINE_int32(cprof_cpu_sampling_period_msec, 10,
             "sampling period for CPU time profiling, in milliseconds");
DEFINE_int32(cprof_wall_sampling_period_msec, 100,
             "sampling period for wall time profiling, in milliseconds");

namespace cloud {
namespace profiler {

std::atomic<bool> Worker::enabled_;

void Worker::Start(JNIEnv *jni) {
  jclass cls = jni->FindClass("java/lang/Thread");
  jmethodID constructor = jni->GetMethodID(cls, "<init>", "()V");
  jobject thread = jni->NewGlobalRef(jni->NewObject(cls, constructor));
  if (thread == nullptr) {
    LOG(ERROR) << "Failed to construct cloud profiler worker thread";
    return;
  }

  // Pass 'this' as the arg to access members from the worker thread.
  jvmtiError err = jvmti_->RunAgentThread(thread, ProfileThread, this,
                                          JVMTI_THREAD_MIN_PRIORITY);
  if (err) {
    LOG(ERROR) << "Failed to start cloud profiler worker thread";
    return;
  }

  enabled_ = FLAGS_cprof_enabled;
}

void Worker::Stop() {
  // Signal the worker thread to exit and wait until it does.
  stopping_.store(true, std::memory_order_release);
  std::lock_guard<std::mutex> lock(mutex_);
}

namespace {

string Collect(Profiler *p,
               google::javaprofiler::NativeProcessInfo *native_info) {
  const char *profile_type = p->ProfileType();
  if (!p->Collect()) {
    LOG(ERROR) << "Failure: Could not collect " << profile_type << " profile";
    return "";
  }
  native_info->Refresh();
  return p->SerializeProfile(*native_info);
}

}  // namespace

bool Worker::IsProfilingEnabled() {
  return enabled_;
}

void Worker::EnableProfiling() {
  enabled_ = true;
}

void Worker::DisableProfiling() {
  enabled_ = false;
}

void Worker::ProfileThread(jvmtiEnv *jvmti_env, JNIEnv *jni_env, void *arg) {
  Worker *w = static_cast<Worker *>(arg);
  google::javaprofiler::NativeProcessInfo n("/proc/self/maps");

  while (!enabled_ && !w->stopping_) {
    sleep(30);
  }

  if (w->stopping_) {
    return;
  }

  std::unique_ptr<Throttler> t =
      FLAGS_cprof_profile_filename.empty()
          ? std::unique_ptr<Throttler>(new APIThrottler())
          : std::unique_ptr<Throttler>(
                new TimedThrottler(FLAGS_cprof_profile_filename));

  while (t->WaitNext()) {
    std::lock_guard<std::mutex> lock(w->mutex_);
    if (w->stopping_) {
      // The worker is exiting.
      break;
    }
    if (!enabled_) {
      // Skip the collection and upload steps when profiling is disabled.
      continue;
    }
    string profile = w->CollectProfileLocked(&n,
                                             t->ProfileType(),
                                             t->DurationNanos(),
                                             t->ProfileType() == kTypeCPU ?
                                             FLAGS_cprof_cpu_sampling_period_msec * kNanosPerMilli :
                                             FLAGS_cprof_wall_sampling_period_msec * kNanosPerMilli);
    if (profile.empty()) {
      LOG(ERROR) << "No profile bytes collected, skipping the upload";
      continue;
    }
    if (!t->Upload(profile)) {
      LOG(ERROR) << "Error on profile upload, discarding the profile";
    }
  }
  LOG(INFO) << "Exiting the profiling loop";
}

string Worker::CollectProfile(string pt, int64_t duration, int64_t sampling_period_nanos) {
  google::javaprofiler::NativeProcessInfo n("/proc/self/maps");

  std::lock_guard<std::mutex> lock(mutex_);
  return CollectProfileLocked(&n, pt, duration, sampling_period_nanos);
}

string Worker::CollectProfileLocked(google::javaprofiler::NativeProcessInfo *n,
                                    string pt, int64_t duration, int64_t sampling_period_nanos) {
  string profile;
  if (pt == kTypeCPU) {
    CPUProfiler p(jvmti_, threads_, duration, sampling_period_nanos);
    profile = Collect(&p, n);
  } else if (pt == kTypeWall) {
    // Note that the requested sampling period for the wall profiling may be
    // increased if the number of live threads is too large.
    WallProfiler p(jvmti_, threads_, duration, sampling_period_nanos);
    profile = Collect(&p, n);
  } else {
    LOG(ERROR) << "Unknown profile type '" << pt << "', skipping the upload";
    return "";
  }

  return profile;
}


}  // namespace profiler
}  // namespace cloud
