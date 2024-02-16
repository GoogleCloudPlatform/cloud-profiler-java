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

#ifndef CLOUD_PROFILER_AGENT_JAVA_WORKER_H_
#define CLOUD_PROFILER_AGENT_JAVA_WORKER_H_

#include <atomic>
#include <memory>
#include <mutex>  // NOLINT

#include "src/globals.h"
#include "src/threads.h"
#include "src/throttler.h"

namespace cloud {
namespace profiler {

class Worker {
 public:
  Worker(jvmtiEnv *jvmti, ThreadTable *threads)
      : jvmti_(jvmti), threads_(threads), stopping_() {}

  // This type is neither copyable nor movable.
  Worker(const Worker &) = delete;
  Worker &operator=(const Worker &) = delete;

  void Start(JNIEnv *jni);
  void Stop();

  static void EnableProfiling();
  static void DisableProfiling();

 private:
  static void ProfileThread(jvmtiEnv *jvmti_env, JNIEnv *jni_env, void *arg);

  jvmtiEnv *jvmti_;
  ThreadTable *threads_;
  std::unique_ptr<Throttler> throttler_;
  std::mutex mutex_;  // Held by the worker thread while it's running.
  std::atomic<bool> stopping_;
  static std::atomic<bool> enabled_;
};

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_WORKER_H_
