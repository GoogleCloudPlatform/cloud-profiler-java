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

#include "third_party/javaprofiler/accessors.h"

namespace google {
namespace javaprofiler {

__thread JNIEnv *Accessors::env_;
__thread int64 Accessors::attr_;
__thread Tags *Accessors::tags_;

void Accessors::InitTags() {
  assert(tags_ == nullptr);
  tags_ = new Tags();
}

void Accessors::DestroyTags() {
  const Tags *tags_tmp = tags_;
  tags_ = nullptr;
  // The thread local storage tags_ is set to null before deallocation to avoid
  // getting the destructed object in the signal handler. The compiler barrier
  // below makes sure that "delete tags_tmp" will not be scheduled before "tags_
  // = nullptr".
  __asm__ __volatile__("" : : : "memory");
  delete tags_tmp;
}

Tags *Accessors::AllocateAndCopyTags() {
  return tags_ == nullptr ? nullptr : new Tags(*tags_);
}

void Accessors::ApplyAndDeleteTags(Tags *tags) {
  if (tags_ != nullptr) {
    *tags_ = std::move(*tags);
  }
  delete tags;
}

}  // namespace javaprofiler
}  // namespace google
