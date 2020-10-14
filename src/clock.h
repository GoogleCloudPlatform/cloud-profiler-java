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

#ifndef CLOUD_PROFILER_AGENT_JAVA_CLOCK_H_
#define CLOUD_PROFILER_AGENT_JAVA_CLOCK_H_

// TODO: remove this whole file when this is moved to javaprofiler.
#include "third_party/javaprofiler/clock.h"

namespace cloud {
namespace profiler {

using google::javaprofiler::Clock;
using google::javaprofiler::kNanosPerMilli;
using google::javaprofiler::kNanosPerSecond;

inline struct timespec TimeAdd(const struct timespec t1,
                               const struct timespec t2) {
  return google::javaprofiler::TimeAdd(t1, t2);
}

inline bool TimeLessThan(const struct timespec &t1, const struct timespec &t2) {
  return google::javaprofiler::TimeLessThan(t1, t2);
}

inline struct timespec NanosToTimeSpec(int64_t nanos) {
  return google::javaprofiler::NanosToTimeSpec(nanos);
}

inline int64_t TimeSpecToNanos(const struct timespec &ts) {
  return google::javaprofiler::TimeSpecToNanos(ts);
}

inline Clock *DefaultClock() { return google::javaprofiler::DefaultClock(); }

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_CLOCK_H_
