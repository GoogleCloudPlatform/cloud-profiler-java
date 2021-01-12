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

#include "src/profiler.h"

#include <errno.h>
#include <execinfo.h>
#include <sys/time.h>
#include <sys/ucontext.h>

#include <cstdlib>
#include <cstring>

#include "src/clock.h"
#include "src/globals.h"
#include "src/proto.h"
#include "third_party/absl/flags/flag.h"
#include "third_party/javaprofiler/accessors.h"

DEFINE_int32(cprof_wall_num_threads_cutoff, 4096,
             "Do not take wall profiles if more than this # of threads exist.");
DEFINE_int32(cprof_wall_max_threads_per_sec, 160,
             "Max total # of threads to wake up per second in wall profiling.");
// Off by default since it may cause rare crashes, b/27615794.
DEFINE_bool(cprof_record_native_stack, false,
            "Whether to unwind native stack and put atop of the Java one.");

namespace cloud {
namespace profiler {

using google::javaprofiler::AlmostThere;

google::javaprofiler::AsyncSafeTraceMultiset *Profiler::fixed_traces_ = nullptr;
std::atomic<int> Profiler::unknown_stack_count_;

namespace {

// Helper class to store and reset errno when in a signal handler.
class ErrnoRaii {
 public:
  ErrnoRaii() { stored_errno_ = errno; }
  ~ErrnoRaii() { errno = stored_errno_; }

 private:
  int stored_errno_;

  DISALLOW_COPY_AND_ASSIGN(ErrnoRaii);
};

}  // namespace

void Profiler::Handle(int signum, siginfo_t *info, void *context) {
  IMPLICITLY_USE(signum);
  IMPLICITLY_USE(info);
  ErrnoRaii err_storage;  // stores and resets errno

  JVMPI_CallTrace trace;
  JVMPI_CallFrame frames[kMaxFramesToCapture];

  JNIEnv *env = google::javaprofiler::Accessors::CurrentJniEnv();
  trace.frames = frames;
  trace.env_id = env;
  trace.num_frames = 0;
  int attr = google::javaprofiler::Accessors::GetAttribute();

  if (env != nullptr) {
    // This is a java thread.
    google::javaprofiler::ASGCTType asgct =
        google::javaprofiler::Asgct::GetAsgct();
    (*asgct)(&trace, kMaxFramesToCapture, context);

    if (trace.num_frames < 0) {
      // Did not get a valid java trace.
      trace.frames[0] =
          JVMPI_CallFrame{kCallTraceErrorLineNum,
                          reinterpret_cast<jmethodID>(trace.num_frames)};
      trace.num_frames = 1;
      if (!fixed_traces_->Add(attr, &trace)) {
        unknown_stack_count_++;
      }
      return;
    }

    if (frames[0].lineno >= 0) {
      // Leaf is a java frame, return java trace.
      if (!fixed_traces_->Add(attr, &trace)) {
        unknown_stack_count_++;
      }
      return;
    }
  }

  // Collect native trace on top of java trace.
  // Skip top two frames, which include this function and the signal
  // handler.
  const int kFramesToSkip = 2;
  if (absl::GetFlag(FLAGS_cprof_record_native_stack) &&
      (kMaxFramesToCapture + kFramesToSkip - trace.num_frames > 0)) {
    void *raw_callstack[kMaxFramesToCapture + kFramesToSkip];
    int stack_len =
        backtrace(&raw_callstack[0],
                  kMaxFramesToCapture + kFramesToSkip - trace.num_frames);
    if (stack_len > kFramesToSkip) {
      stack_len -= kFramesToSkip;
      // Shift java frames to make room for native frames.
      if (trace.num_frames > 0) {
        for (int i = trace.num_frames; i > 0; i--) {
          trace.frames[stack_len + i - 1] = trace.frames[i - 1];
        }
      }
      void **callstack = &raw_callstack[kFramesToSkip];
      for (int i = 0; i < stack_len; i++) {
        trace.frames[i] = JVMPI_CallFrame{kNativeFrameLineNum,
                                          static_cast<jmethodID>(callstack[i])};
      }
      trace.num_frames += stack_len;
    }
  }

  if (trace.num_frames == 0) {
    // When FLAGS_cprof_record_native_stack is off and the thread is not a Java
    // thread (so the "if (env != nullptr)" condition above fell through), we
    // end up with zero frames which won't be properly visualized by the rest of
    // the toolchain as pprof will discard such samples. Record the program
    // counter in such case to provide at least some clue into where the time is
    // being spent. The alternative would be to mark such samples as erroneous
    // but it appears even having just the shared object name is more useful.
#ifdef __aarch64__
    uint64_t pc = static_cast<ucontext_t *>(context)->uc_mcontext.pc;
#else
    uint64_t pc =
        static_cast<ucontext_t *>(context)->uc_mcontext.gregs[REG_RIP];
#endif
    trace.frames[0] =
        JVMPI_CallFrame{kNativeFrameLineNum, reinterpret_cast<jmethodID>(pc)};
    ++trace.num_frames;
  }

  if (!fixed_traces_->Add(attr, &trace)) {
    unknown_stack_count_++;
  }
}

// This method schedules the SIGPROF timer to go off every specified interval.
// seconds, usec microseconds.
bool SignalHandler::SetSigprofInterval(int64_t period_usec) {
  static struct itimerval timer;
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = period_usec;
  timer.it_value = timer.it_interval;
  if (setitimer(ITIMER_PROF, &timer, 0) == -1) {
    LOG(ERROR) << "Scheduling profiler interval failed with error " << errno;
    return false;
  }
  return true;
}

struct sigaction SignalHandler::SetAction(void (*action)(int, siginfo_t *,
                                                         void *)) {
  struct sigaction sa;
  sa.sa_handler = NULL;
  sa.sa_sigaction = action;
  sa.sa_flags = SA_RESTART | SA_SIGINFO;

  sigemptyset(&sa.sa_mask);

  struct sigaction old_handler;
  if (sigaction(SIGPROF, &sa, &old_handler) != 0) {
    LOG(ERROR) << "Scheduling profiler action failed with error " << errno;
    return old_handler;
  }

  return old_handler;
}

void Profiler::Reset() {
  if (fixed_traces_ == nullptr) {
    fixed_traces_ = new google::javaprofiler::AsyncSafeTraceMultiset();
  } else {
    fixed_traces_->Reset();
  }
  unknown_stack_count_ = 0;

  if (absl::GetFlag(FLAGS_cprof_record_native_stack)) {
    // When native stack collection requested, gather a single backtrace before
    // setting up the signal handler, to avoid running internal initialization
    // within backtrace from the signal handler.
    void *raw_callstack[1];
    backtrace(&raw_callstack[0], 1);
  }

  // old_action_ is stored, but never used.  This is in case of future
  // refactorings that need it.
  old_action_ = handler_.SetAction(&Profiler::Handle);
}

std::string Profiler::SerializeProfile(
    JNIEnv *jni, const google::javaprofiler::NativeProcessInfo &native_info) {
  return SerializeAndClearJavaCpuTraces(
      jni, jvmti_, native_info, ProfileType(), duration_nanos_, period_nanos_,
      &aggregated_traces_, unknown_stack_count_);
}

bool CPUProfiler::Collect() {
  Reset();

  if (!Start()) {
    return false;
  }

  Clock *clock = DefaultClock();
  // Flush the async table every 100 ms
  struct timespec flush_interval = {0, 100 * 1000 * 1000};  // 100 millisec
  struct timespec finish_line =
      TimeAdd(clock->Now(), NanosToTimeSpec(duration_nanos_));

  // Sleep until finish_line, but wakeup periodically to flush the
  // internal tables.
  while (!AlmostThere(clock, finish_line, flush_interval)) {
    clock->SleepFor(flush_interval);
    Flush();
  }
  clock->SleepUntil(finish_line);
  Stop();
  // Delay to allow last signals to be processed.
  clock->SleepUntil(TimeAdd(finish_line, flush_interval));
  Flush();
  return true;
}

bool CPUProfiler::Start() {
  int period_usec = period_nanos_ / 1000;
  if (threads_->UseTimers()) {
    threads_->StartTimers(period_usec);
    return true;
  } else {
    return handler_.SetSigprofInterval(period_usec);
  }
}

void CPUProfiler::Stop() {
  if (threads_->UseTimers()) {
    threads_->StopTimers();
  } else {
    handler_.SetSigprofInterval(0);
  }
  // Breaks encapsulation, but whatever.
  signal(SIGPROF, SIG_IGN);
}

WallProfiler::WallProfiler(jvmtiEnv *jvmti, ThreadTable *threads,
                           int64_t duration_nanos, int64_t period_nanos)
    : Profiler(jvmti, threads, duration_nanos,
               EffectivePeriodNanos(
                   period_nanos, threads->Size(),
                   absl::GetFlag(FLAGS_cprof_wall_max_threads_per_sec),
                   duration_nanos)) {}

int64_t WallProfiler::EffectivePeriodNanos(int64_t period_nanos,
                                           int64_t num_threads,
                                           int64_t max_threads_per_second,
                                           int64_t duration_nanos) {
  if (num_threads * kNanosPerSecond > max_threads_per_second * period_nanos) {
    // Current of threads too large for requested period.
    period_nanos = num_threads * kNanosPerSecond / max_threads_per_second;
  }

  int64_t frequency = duration_nanos / period_nanos;
  if (frequency == 0) {
    // If the period is too large, we'll collect a single sample,
    // which will represent the whole profile duration.
    period_nanos = duration_nanos;
  } else {
    // Round period off to ensure duration is a multiple of period.
    period_nanos = duration_nanos / frequency;
  }

  return period_nanos;
}

bool WallProfiler::Collect() {
  Reset();
  pid_t my_tid = GetTid();

  Clock *clock = DefaultClock();
  struct timespec profile_period = {0, period_nanos_};
  struct timespec finish_line =
      TimeAdd(clock->Now(), NanosToTimeSpec(duration_nanos_));

  // Send signals to all threads to wakeup and report themselves. Stop
  // after we reach the finish line.
  struct timespec next = clock->Now();

  int64_t count = 0;
  const int kFlushPeriod = 128;  // Flush table every 128 samples
  while (TimeLessThan(next, finish_line)) {
    if (count > kFlushPeriod) {
      count = 0;
      // Periodically flush the internal tables.
      Flush();
    }
    clock->SleepUntil(next);
    std::vector<pid_t> threads = threads_->Threads();
    if (threads.size() > absl::GetFlag(FLAGS_cprof_wall_num_threads_cutoff)) {
      LOG(WARNING) << "Aborting wall profiling due to too many threads. "
                   << "Got " << threads.size() << " threads. "
                   << "Want up to "
                   << absl::GetFlag(FLAGS_cprof_wall_num_threads_cutoff);
      return false;  // Too many threads, abort
    }
    count += threads.size();
    for (pid_t tid : threads) {
      if (tid != my_tid) {
        // Skip profiler worker thread.
        TgKill(tid, SIGPROF);
      }
    }
    next = TimeAdd(next, profile_period);
  }
  // Delay to allow last signals to be processed.
  clock->SleepUntil(TimeAdd(next, profile_period));
  signal(SIGPROF, SIG_IGN);
  Flush();
  return true;
}

}  // namespace profiler
}  // namespace cloud
