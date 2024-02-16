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

#ifndef THIRD_PARTY_JAVAPROFILER_GLOBALS_H_
#define THIRD_PARTY_JAVAPROFILER_GLOBALS_H_

#include <assert.h>
#include <dlfcn.h>
#include <jvmti.h>

#include "third_party/javaprofiler/jvmti_error.h"

#include <glog/logging.h>
#include <string>

using std::hash;
using std::string;

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &);              \
  void operator=(const TypeName &)
#endif

// Short version: reinterpret_cast produces undefined behavior in many
// cases where memcpy doesn't.
template <class Dest, class Source>
inline Dest bit_cast(const Source &source) {
  // Compile time assertion: sizeof(Dest) == sizeof(Source)
  // A compile error here means your Dest and Source have different sizes.
  typedef char VerifySizesAreEqual[sizeof(Dest) == sizeof(Source) ? 1 : -1]
      __attribute__((unused));

  Dest dest;
  memcpy(&dest, &source, sizeof(dest));
  return dest;
}

namespace google {
namespace javaprofiler {

template <class T>
class JvmtiScopedPtr {
 public:
  explicit JvmtiScopedPtr(jvmtiEnv *jvmti) : jvmti_(jvmti), ref_(NULL) {}

  // This type is neither copyable nor movable.
  JvmtiScopedPtr(const JvmtiScopedPtr&) = delete;
  JvmtiScopedPtr& operator=(const JvmtiScopedPtr&) = delete;

  JvmtiScopedPtr(jvmtiEnv *jvmti, T *ref) : jvmti_(jvmti), ref_(ref) {}

  ~JvmtiScopedPtr() {
    if (NULL != ref_) {
      JVMTI_ERROR(jvmti_->Deallocate((unsigned char *)ref_));
    }
  }

  T **GetRef() {
    assert(ref_ == NULL);
    return &ref_;
  }

  T *Get() { return ref_; }

 private:
  jvmtiEnv *jvmti_;
  T *ref_;
};

template <class T>
class ScopedLocalRef {
 public:
  ScopedLocalRef(JNIEnv *jni, T ref) : jni_(jni), ref_(ref) {}

  // This type is neither copyable nor movable.
  ScopedLocalRef(const ScopedLocalRef&) = delete;
  ScopedLocalRef& operator=(const ScopedLocalRef&) = delete;

  ~ScopedLocalRef() {
    if (NULL != ref_) {
      jni_->DeleteLocalRef(ref_);
    }
  }

  T Get() { return ref_; }

 private:
  JNIEnv *jni_;
  T ref_;
};

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_GLOBALS_H_
