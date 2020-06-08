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

#ifndef THIRD_PARTY_JAVAPROFILER_ACCESSORS_H_
#define THIRD_PARTY_JAVAPROFILER_ACCESSORS_H_

#include "third_party/javaprofiler/globals.h"
#include "third_party/javaprofiler/tags.h"

namespace google {
namespace javaprofiler {

// Accessors for a JNIEnv for this thread.
class Accessors {
 public:
  static void SetCurrentJniEnv(JNIEnv *env) { env_ = env; }
  static JNIEnv *CurrentJniEnv() { return env_; }

  static void SetAttribute(int64 value) { attr_ = value; }
  static int64 GetAttribute() { return attr_; }

  // Allocates the current thread's tags storage which can be later retrieved by
  // GetTags(). If the tags storage is already allocated, asserts an error.
  static void InitTags();
  // Deallocates the current thread's tags storage. No tag can be
  // set for the current thread after this function is called.
  static void DestroyTags();
  // Both GetTags() and GetMutableTags() return the tags storage of the current
  // thread. If InitTags() is not called before, GetTags() returns a constant
  // reference to an empty Tags instance and GetMutableTags() returns nullptr.
  // For all Java threads, InitTags() is called inside the callback function of
  // onThreadStart. However, non-Java threads do not trigger the callback,
  // which leaves InitTags() not called.
  static const Tags &GetTags() {
    return tags_ == nullptr ? Tags::Empty() : *tags_;
  }
  static Tags *GetMutableTags() { return tags_; }

  // AllocateAndCopyTags() and ApplyAndDeleteTags() are used to propagate tags
  // between threads. The usage should be like:
  //
  // Tags *tags_copy = AllocateAndCopyTags(); // Make a copy of tags storage of
  // // current thread.
  // ... // Pass tags_copy into another thread.
  // ApplyAndDeleteTags(tags_copy); // Overwrite the tags storage of current
  // //thread with the information stored in tags_copy.
  // // If ApplyAndDeleteTags() is never called, users must explicitly reclaim
  // // the memory reserved for tags_copy by "delete tags_copy".

  // Copies the thread-local tags. Returns the pointer of newly allocated Tags
  // on success; otherwise, return nullptr.
  static Tags *AllocateAndCopyTags();
  // Overrides the thread-local tags with the tags passed in and releases the
  // memory pointed by tags.
  static void ApplyAndDeleteTags(Tags *tags);

  template <class FunctionType>
  static inline FunctionType GetJvmFunction(const char *function_name) {
    // get handle to library
    static void *handle = dlopen("libjvm.so", RTLD_LAZY);
    if (handle == NULL) {
      return NULL;
    }

    // get address of function, return null if not found
    return bit_cast<FunctionType>(dlsym(handle, function_name));
  }

 private:
  // This is subtle and potentially dangerous, read this carefully.
  //
  // In glibc, TLS access is not signal-async-safe, as they can call malloc for
  // lazy initialization. The initial-exec TLS mode avoids this potential
  // allocation, with the limitation that there is a fixed amount of space to
  // hold all TLS variables referenced in the module. This should be OK for the
  // cloud profiler agent, which is relatively small.
  //
  // In environments where the TLS access is async-signal-safe, the
  // global-dynamic TLS model can be used. For example, it is async-signal-safe
  // in musl / Alpine, see
  // https://wiki.musl-libc.org/design-concepts.html#Thread-local-storage.
#if defined(JAVAPROFILER_GLOBAL_DYNAMIC_TLS) || defined(ALPINE)
  static __thread JNIEnv *env_ __attribute__((tls_model("global-dynamic")));
  static __thread int64 attr_ __attribute__((tls_model("global-dynamic")));
  static __thread Tags *tags_ __attribute__((tls_model("global-dynamic")));
#else
  static __thread JNIEnv *env_ __attribute__((tls_model("initial-exec")));
  static __thread int64 attr_ __attribute__((tls_model("initial-exec")));
  static __thread Tags *tags_ __attribute__((tls_model("initial-exec")));
#endif
};

}  // namespace javaprofiler
}  // namespace google
#endif  // THIRD_PARTY_JAVAPROFILER_ACCESSORS_H_
