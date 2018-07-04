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

#include "src/uploader.h"

#include <chrono>  // NOLINT(build/c++11)

namespace cloud {
namespace profiler {

string ProfilePath(const string& prefix, const string& profile_type) {
  using std::chrono::system_clock;
  int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                          system_clock::now().time_since_epoch())
                          .count();
  return prefix + profile_type + "_" + std::to_string(timestamp) + ".pb.gz";
}

}  // namespace profiler
}  // namespace cloud
