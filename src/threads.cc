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

#include "src/threads.h"

#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace cloud {
namespace profiler {

namespace {

const timer_t kInvalidTimer = reinterpret_cast<timer_t>(-1LL);

timer_t CreateTimer(pid_t tid) {
#ifdef ALPINE
  // Per thread timers are not available on Alpine.
  return kInvalidTimer;
#else
  struct sigevent sevp = {};
  sevp.sigev_notify = SIGEV_THREAD_ID;
  sevp._sigev_un._tid = tid;
  sevp.sigev_signo = SIGPROF;
  timer_t timer = kInvalidTimer;
  int err = timer_create(CLOCK_THREAD_CPUTIME_ID, &sevp, &timer);
  if (err) {
    LOG(ERROR) << "Failed to create timer: " << err;
    return kInvalidTimer;
  }
  return timer;
#endif
}

bool SetTimer(timer_t timer, int64_t period_usec) {
  struct itimerspec its = {};
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = period_usec * 1000;
  its.it_value = its.it_interval;
  int err = timer_settime(timer, 0, &its, nullptr);
  if (err) {
    LOG(ERROR) << "Failed to set timer: " << err;
    return false;
  }
  return true;
}

void DeleteTimer(timer_t timer) {
  int err = timer_delete(timer);
  if (err) {
    LOG(ERROR) << "Failed to delete timer: " << err;
  }
}

}  // namespace

void ThreadTable::RegisterCurrent() {
  pid_t tid = GetTid();
  timer_t timer = kInvalidTimer;
  if (use_timers_) {
    timer = CreateTimer(tid);
  }
  std::lock_guard<std::mutex> lock(thread_mutex_);
  threads_.push_back({tid, timer});
  if (timer != kInvalidTimer && period_usec_ > 0) {
    SetTimer(timer, period_usec_);
  }
}

void ThreadTable::UnregisterCurrent() {
  pid_t tid = GetTid();
  std::lock_guard<std::mutex> lock(thread_mutex_);
  for (auto i = threads_.begin(); i != threads_.end(); ++i) {
    if (i->first == tid) {
      if (i->second != kInvalidTimer) {
        DeleteTimer(i->second);
      }
      threads_.erase(i);
      return;
    }
  }
}

int64_t ThreadTable::Size() const {
  std::lock_guard<std::mutex> lock(thread_mutex_);
  return threads_.size();
}

std::vector<pid_t> ThreadTable::Threads() const {
  std::vector<pid_t> tids;
  std::lock_guard<std::mutex> lock(thread_mutex_);
  for (const auto& t : threads_) {
    tids.push_back(t.first);
  }
  return tids;
}

void ThreadTable::StartTimers(int64_t period_usec) {
  std::lock_guard<std::mutex> lock(thread_mutex_);
  period_usec_ = period_usec;
  for (const auto& t : threads_) {
    SetTimer(t.second, period_usec);
  }
}

void ThreadTable::StopTimers() { StartTimers(0); }

pid_t GetTid() { return syscall(__NR_gettid); }

bool TgKill(pid_t tid, int signum) {
  return syscall(__NR_tgkill, getpid(), tid, signum) == 0;
}

}  // namespace profiler
}  // namespace cloud
