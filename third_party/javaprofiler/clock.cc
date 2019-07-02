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

#include "third_party/javaprofiler/clock.h"

namespace google {
namespace javaprofiler {

namespace {
  Clock DefaultClockInstance;
}

bool AlmostThere(Clock* clock, const struct timespec& finish,
                 const struct timespec& lap) {
  const int64_t kMarginLaps = 2;

  struct timespec now = clock->Now();
  struct timespec laps = {lap.tv_sec * kMarginLaps, lap.tv_nsec * kMarginLaps};

  return TimeLessThan(finish, TimeAdd(now, laps));
}

Clock* DefaultClock() {
  return &DefaultClockInstance;
}

}  // namespace javaprofiler
}  // namespace google
