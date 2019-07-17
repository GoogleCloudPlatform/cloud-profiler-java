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

#ifndef CLOUD_PROFILER_AGENT_JAVA_THROTTLER_H_
#define CLOUD_PROFILER_AGENT_JAVA_THROTTLER_H_

#include <memory>

#include "src/globals.h"

namespace cloud {
namespace profiler {

// Supported profile types.
constexpr char kTypeCPU[] = "cpu";
constexpr char kTypeWall[] = "wall";
constexpr char kTypeHeap[] = "heap";

// Iterator-like abstraction used to guide a profiling loop comprising of
// waiting for when the next profile may be collected and saving its data once
// gathered the data. The client pseudocode for using the interface is:
//
// Throttler* t = CreateThrottler();
// while (t->WaitNext()) {
//   if (!t->Upload(Collect(t->ProfileType(), t->DurationNanos()))) {
//     // Log a warning.
//   }
// }
class Throttler {
 public:
  virtual ~Throttler() {}

  // Waits until the next profiling session can be taken. Once the call returns,
  // the client should collect the profile as soon as possible and upload its
  // data using Upload() call. When false is returned, the client should exit
  // the profiling loop.
  virtual bool WaitNext() = 0;

  // Returns the profile type (one of kType* constants) that the client should
  // collect at this iteration. The return value is undefined when not preceded
  // by a successful call to WaitNext().
  virtual string ProfileType() = 0;

  // Returns the duration in nanoseconds of the profile that the client should
  // collect at this iteration. The return value is undefined when not preceded
  // by a successful call to WaitNext().
  virtual int64_t DurationNanos() = 0;

  // Upload the compressed profile proto bytes. Returns false on error.
  virtual bool Upload(string profile) = 0;

  // Closes the throttler by trying to cancel WaitNext() / Upload() in flight.
  // Those calls may return cancellation error. This method is thread-safe.
  virtual void Close() = 0;
};

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_THROTTLER_H_
