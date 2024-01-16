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

#ifndef CLOUD_PROFILER_AGENT_JAVA_NATIVE_H_
#define CLOUD_PROFILER_AGENT_JAVA_NATIVE_H_

#include <stdint.h>

#include <vector>

#include "third_party/javaprofiler/globals.h"

namespace google {
namespace javaprofiler {

// NativeProcessInfo maintains information about native libraries.
class NativeProcessInfo {
 public:
  // procmaps_filename contains the path of a file describing the memory
  // regions in the current process, in /proc/<pid>/maps format.
  explicit NativeProcessInfo(const std::string &procmaps_filename);

  struct Mapping {
    uint64_t start, limit;
    std::string name;
  };

  void Refresh();
  const std::vector<Mapping> &Mappings() const { return mappings_; }

 private:
  const std::string procmaps_filename_;
  std::vector<Mapping> mappings_;
  DISALLOW_COPY_AND_ASSIGN(NativeProcessInfo);
};

}  // namespace javaprofiler
}  // namespace google

#endif  // CLOUD_PROFILER_AGENT_JAVA_NATIVE_H_
