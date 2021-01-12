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
#include "third_party/javaprofiler/heap_sampler.h"

DEFINE_bool(cprof_enabled, true,
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

  // Initialize the throttler here rather in the constructor, since the
  // constructor is invoked too early, before the heap profiler is initialized.
  throttler_ = FLAGS_cprof_profile_filename.empty()
                   ? std::unique_ptr<Throttler>(new APIThrottler(jni))
                   : std::unique_ptr<Throttler>(
                         new TimedThrottler(FLAGS_cprof_profile_filename));

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
  stopping_.store(true, std::memory_order_release);
  // Close the throttler which will initiate cancellation of WaitNext / Upload.
  throttler_->Close();
  // Wait till the worker thread is done.
  std::lock_guard<std::mutex> lock(mutex_);
}

namespace {

std::string Collect(Profiler *p, JNIEnv *env,
                    google::javaprofiler::NativeProcessInfo *native_info) {
  const char *profile_type = p->ProfileType();
  if (!p->Collect()) {
    LOG(ERROR) << "Failure: Could not collect " << profile_type << " profile";
    return "";
  }
  native_info->Refresh();
  return p->SerializeProfile(env, *native_info);
}

class JNILocalFrame {
 public:
  explicit JNILocalFrame(JNIEnv *jni_env) : jni_env_(jni_env) {
    // Put 100, it doesn't really matter: new spec implementations don't care.
    jni_env_->PushLocalFrame(100);
  }

  ~JNILocalFrame() { jni_env_->PopLocalFrame(nullptr); }

  // Not copyable or movable.
  JNILocalFrame(const JNILocalFrame &) = delete;
  JNILocalFrame &operator=(const JNILocalFrame &) = delete;

 private:
  JNIEnv *jni_env_;
};

}  // namespace

void Worker::EnableProfiling() { enabled_ = true; }

void Worker::DisableProfiling() { enabled_ = false; }

void Worker::ProfileThread(jvmtiEnv *jvmti_env, JNIEnv *jni_env, void *arg) {
  Worker *w = static_cast<Worker *>(arg);
  std::lock_guard<std::mutex> lock(w->mutex_);

  google::javaprofiler::NativeProcessInfo n("/proc/self/maps");

  while (w->throttler_->WaitNext()) {
    if (w->stopping_) {
      // The worker is exiting.
      break;
    }
    if (!enabled_) {
      // Skip the collection and upload steps when profiling is disabled.
      continue;
    }

    // There are a number of JVMTI functions the agent uses that return
    // local references. Normally, local references are freed when a JNI
    // call returns to Java. E.g. in the internal /profilez profiler
    // those would get freed when the C++ code returns back to the request
    // handler. But in case of the cloud agent the agent thread never exits
    // and so the local references keep accumulating. Adding an explicit
    // local frame around each profiling iteration fixes this.
    // Note: normally the various JNIHandles are properly lifetime managed
    // now (via b/133409114) and there should be no leaks; but leaving this in
    // so that, if ever JNI handle leaks do happen again, this will release the
    // handles automatically.
    JNILocalFrame local_frame(jni_env);
    std::string profile;
    std::string pt = w->throttler_->ProfileType();
    if (pt == kTypeCPU) {
      CPUProfiler p(w->jvmti_, w->threads_, w->throttler_->DurationNanos(),
                    FLAGS_cprof_cpu_sampling_period_msec * kNanosPerMilli);
      profile = Collect(&p, jni_env, &n);
    } else if (pt == kTypeWall) {
      // Note that the requested sampling period for the wall profiling may be
      // increased if the number of live threads is too large.
      WallProfiler p(w->jvmti_, w->threads_, w->throttler_->DurationNanos(),
                     FLAGS_cprof_wall_sampling_period_msec * kNanosPerMilli);
      profile = Collect(&p, jni_env, &n);
    } else if (pt == kTypeHeap) {
      if (!google::javaprofiler::HeapMonitor::Enabled()) {
        LOG(WARNING) << "Asked for a heap sampler but it is disabled";
        continue;
      }

      // Note: we do not force GC here, instead we rely on what was seen as
      // still live at the last GC; this means that technically:
      //   - Some objects might be dead now.
      //   - Some other objects might be sampled but not show up yet.
      // On the flip side, this allows the profile collection to not provoke a
      // GC.
      perftools::profiles::Builder::Marshal(
          *google::javaprofiler::HeapMonitor::GetHeapProfiles(
              jni_env, false /* force_gc */),
          &profile);
    } else {
      LOG(ERROR) << "Unknown profile type '" << pt << "', skipping the upload";
      continue;
    }
    if (profile.empty()) {
      LOG(ERROR) << "No profile bytes collected, skipping the upload";
      continue;
    }
    if (!w->throttler_->Upload(profile)) {
      LOG(ERROR) << "Error on profile upload, discarding the profile";
    }
  }
  LOG(INFO) << "Exiting the profiling loop";
}

}  // namespace profiler
}  // namespace cloud
