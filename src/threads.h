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

#ifndef CLOUD_PROFILER_AGENT_JAVA_THREADS_H_
#define CLOUD_PROFILER_AGENT_JAVA_THREADS_H_

#include <time.h>
#include <mutex>  // NOLINT(build/c++11)
#include <utility>

#include "src/globals.h"

namespace cloud {
namespace profiler {

// ThreadTable keeps track of the thread IDs of the known active threads.
// It is meant to be updated from the OnThreadStart and OnThreadEnd callbacks.
// When configured to do so, it manages per thread CPU time timers and allows
// starting and stopping them to generate SIGPROF signal when certain amount of
// the CPU time expires.
class ThreadTable {
 public:
  explicit ThreadTable(bool use_timers)
      : use_timers_(use_timers), period_usec_() {}

  // Registers the current thread.
  void RegisterCurrent();
  // Unregisters the current thread.
  void UnregisterCurrent();
  // Returns the number of registered threads.
  int64_t Size() const;
  // Returns the IDs of all registered threads.
  std::vector<pid_t> Threads() const;
  // Starts per-thread timers.
  void StartTimers(int64_t period_usec);
  // Stops per-thread timers.
  void StopTimers();
  // Whether CPU time sampling is configured to use per-thread timers.
  bool UseTimers() const { return use_timers_; }

 private:
  mutable std::mutex thread_mutex_;
  // List of threads and associated timers. The timer ID is kInvalidTimer when
  // the timer usage is off or the timer creation failed for the thread.
  std::vector<std::pair<pid_t, timer_t>> threads_;
  // True when the timer usage is requested.
  bool use_timers_;
  // Non-zero when the thread timers have been started.
  int64_t period_usec_;

  DISALLOW_COPY_AND_ASSIGN(ThreadTable);
};

// Returns the thread ID of the current thread.
pid_t GetTid();

// Sends a signal to the specified thread.
bool TgKill(pid_t tid, int signum);

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_THREADS_H_
