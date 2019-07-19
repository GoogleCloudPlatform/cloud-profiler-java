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

#include "third_party/javaprofiler/display.h"

namespace google {
namespace javaprofiler {

int64 MethodInfo::Location(int line_number) {
  auto it = locations_.find(line_number);
  if (it == locations_.end()) {
    return kInvalidLocationId;
  }
  return it->second;
}

}  // namespace javaprofiler
}  // namespace google
