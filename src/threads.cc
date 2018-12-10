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

pid_t GetTid() { 
#ifdef __APPLE__
  return syscall(SYS_thread_selfid);
#else
  return syscall(__NR_gettid); 
#endif
}

#ifndef __APPLE__
bool TgKill(pid_t tid, int signum) {
  return syscall(__NR_tgkill, getpid(), tid, signum) == 0;
}
#endif

timer_t CreateTimer(pid_t tid) {
#ifdef __APPLE__
  LOG(ERROR) << "Timers aren't supported on OSX";
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
#ifdef __APPLE__
  return false;
#else
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
#endif
}

void DeleteTimer(timer_t timer) {
#ifndef __APPLE__
  int err = timer_delete(timer);
  if (err) {
    LOG(ERROR) << "Failed to delete timer: " << err;
  }
#endif
}

}  // namespace

void ThreadTable::RegisterCurrent() {
  pid_t tid = GetTid();
  timer_t timer = kInvalidTimer;
  if (use_timers_) {
    timer = CreateTimer(tid);
  }
  std::lock_guard<std::mutex> lock(thread_mutex_);
  threads_.push_back({pthread_self(), tid, timer});
  if (timer != kInvalidTimer && period_usec_ > 0) {
    SetTimer(timer, period_usec_);
  }
}

void ThreadTable::UnregisterCurrent() {
  pid_t tid = GetTid();
  std::lock_guard<std::mutex> lock(thread_mutex_);
  for (auto i = threads_.begin(); i != threads_.end(); ++i) {
    if (i->tid == tid) {
      if (i->timer != kInvalidTimer) {
        DeleteTimer(i->timer);
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

void ThreadTable::StartTimers(int64_t period_usec) {
  std::lock_guard<std::mutex> lock(thread_mutex_);
  period_usec_ = period_usec;
  for (const auto& t : threads_) {
    SetTimer(t.timer, period_usec);
  }
}

void ThreadTable::StopTimers() { StartTimers(0); }

int64_t ThreadTable::SignalAllExceptSelf(int signal) {
  std::lock_guard<std::mutex> lock(thread_mutex_);

  pid_t my_id = GetTid();
  int64_t count = 0;
  for (const auto& t : threads_) {
    if (t.tid != my_id) {
#ifdef __APPLE__
      pthread_kill(t.pthread, signal);
#else
      TgKill(t->tid, signal);
#endif
      count++;
    }
  }
  return count;
}

}  // namespace profiler
}  // namespace cloud
