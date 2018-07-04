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

#ifndef CLOUD_PROFILER_AGENT_JAVA_THROTTLER_TIMED_H_
#define CLOUD_PROFILER_AGENT_JAVA_THROTTLER_TIMED_H_

#include <memory>
#include <random>

#include "src/clock.h"
#include "src/throttler.h"
#include "src/uploader.h"

namespace cloud {
namespace profiler {

// Throttler implementation that uses a local timer and uploader interface.
class TimedThrottler : public Throttler {
 public:
  // Creates a timed throttler where path specifies the prefix path at which to
  // store the collected profiles. The path may be a Google Cloud Storage path,
  // prefixed with "gs://".
  explicit TimedThrottler(const string& path);

  // Testing-only constructor.
  TimedThrottler(std::unique_ptr<ProfileUploader> uploader, Clock* clock,
                 bool fixed_seed);

  bool WaitNext() override;
  string ProfileType() override;
  int64_t DurationNanos() override;
  bool Upload(string profile) override;

 private:
  Clock* clock_;
  int64_t duration_cpu_ns_, duration_wall_ns_;
  int64_t interval_ns_;

  std::default_random_engine gen_;
  std::uniform_int_distribution<int64_t> dist_;
  struct timespec next_interval_;
  // Counts profile sets really (CPU + wall).
  int profile_count_;

  std::vector<std::pair<string, int64_t>> cur_;
  std::unique_ptr<ProfileUploader> uploader_;
};

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_THROTTLER_TIMED_H_
