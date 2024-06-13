/*
 * Copyright 2019 Google LLC
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

#include "third_party/javaprofiler/method_info.h"

#include <cstdint>

#include "third_party/javaprofiler/display.h"
#include "third_party/javaprofiler/stacktrace_decls.h"

namespace google {
namespace javaprofiler {

int64_t MethodInfo::LineNumber(const JVMPI_CallFrame &frame) {
  // lineno is actually the BCI of the frame.
  int bci = frame.lineno;

  auto it = line_numbers_.find(bci);
  if (it != line_numbers_.end()) {
    return it->second;
  }

  int line_number = GetLineNumber(jvmti_env_, frame.method_id, bci);

  line_numbers_[bci] = line_number;
  return line_number;
}

}  // namespace javaprofiler
}  // namespace google
