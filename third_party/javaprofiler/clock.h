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

#ifndef GOOGLE_JAVAPROFILER_CLOCK_H_
#define GOOGLE_JAVAPROFILER_CLOCK_H_

#include <stdint.h>
#include <time.h>

#include "third_party/javaprofiler/globals.h"

namespace google {
namespace javaprofiler {

constexpr int64_t kNanosPerSecond = 1000 * 1000 * 1000;
constexpr int64_t kNanosPerMilli = 1000 * 1000;

inline struct timespec TimeAdd(const struct timespec t1,
                               const struct timespec t2) {
  struct timespec t = {t1.tv_sec + t2.tv_sec, t1.tv_nsec + t2.tv_nsec};
  if (t.tv_nsec > kNanosPerSecond) {
    t.tv_sec += t.tv_nsec / kNanosPerSecond;
    t.tv_nsec = t.tv_nsec % kNanosPerSecond;
  }
  return t;
}

inline bool TimeLessThan(const struct timespec &t1, const struct timespec &t2) {
  return (t1.tv_sec < t2.tv_sec) ||
         (t1.tv_sec == t2.tv_sec && t1.tv_nsec < t2.tv_nsec);
}

inline struct timespec NanosToTimeSpec(int64_t nanos) {
  time_t seconds = nanos / kNanosPerSecond;
  int32_t nano_seconds = nanos % kNanosPerSecond;
  return timespec{seconds, nano_seconds};
}

inline int64_t TimeSpecToNanos(const struct timespec &ts) {
  return kNanosPerSecond * ts.tv_sec + ts.tv_nsec;
}

// Clock interface that can be mocked for tests. The default implementation
// delegates to the system and so is thread-safe.
class Clock {
 public:
  virtual ~Clock() {}

  // Returns the current time.
  virtual struct timespec Now() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now;
  }

  // Blocks the current thread until the specified point in time.
  virtual void SleepUntil(struct timespec ts) {
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) > 0) {
    }
  }

  // Blocks the current thread for the specified duration.
  virtual void SleepFor(struct timespec ts) {
    while (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts) > 0) {
    }
  }
};

// Determines if there is time for another lap before reaching the finish line.
// Uses a margin of multiple laps to ensure to not overrun the finish line.
bool AlmostThere(Clock *clock, const struct timespec &finish,
                 const struct timespec &lap);

Clock *DefaultClock();

}  // namespace javaprofiler
}  // namespace google

#endif  // GOOGLE_JAVAPROFILER_CLOCK_H
