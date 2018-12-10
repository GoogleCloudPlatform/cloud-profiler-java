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

using std::string;

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &);              \
  void operator=(const TypeName &)

#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName();                                    \
  DISALLOW_COPY_AND_ASSIGN(TypeName)

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

  void AbandonBecauseOfError() { ref_ = NULL; }

 private:
  jvmtiEnv *jvmti_;
  T *ref_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(JvmtiScopedPtr);
};

// Accessors for a JNIEnv for this thread.
class Accessors {
 public:
  static void SetCurrentJniEnv(JNIEnv *env) { env_ = env; }

  static JNIEnv *CurrentJniEnv() { return env_; }

  static void Init() {}

  static void Destroy() {}

  static void SetAttribute(int64_t value) { attr_ = value; }

  static int64_t GetAttribute() { return attr_; }

  template <class FunctionType>
  static inline FunctionType GetJvmFunction(const char *function_name) {
    FunctionType result = bit_cast<FunctionType>(dlsym(RTLD_DEFAULT, function_name));
    if (result) {
      return result;
    }

    // get handle to library
#ifdef __APPLE__
    static void *handle = dlopen("libjvm.dylib", RTLD_LAZY);
#else
    static void *handle = dlopen("libjvm.so", RTLD_LAZY);
#endif
    if (handle == NULL) {
      return NULL;
    }

    // get address of function, return null if not found
    return bit_cast<FunctionType>(dlsym(handle, function_name));
  }

 private:
  // This is dangerous. TLS accesses are by default not async safe, as
  // they can call malloc for lazy initialization. The initial-exec
  // TLS mode avoids this potential allocation, with the limitation
  // that there is a fixed amount of space to hold all TLS variables
  // referenced in the module. This should be OK for the cloud
  // profiler agent, which is relatively small. We do provide a way
  // to override the TLS model for compilation environments where the
  // TLS access is async-safe.
#ifdef JAVAPROFILER_GLOBAL_DYNAMIC_TLS
  static __thread JNIEnv *env_ __attribute__((tls_model("global-dynamic")));
  static __thread int64_t attr_ __attribute__((tls_model("global-dynamic")));
#else
  static __thread JNIEnv *env_ __attribute__((tls_model("initial-exec")));
  static __thread int64_t attr_ __attribute__((tls_model("initial-exec")));
#endif
};

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_GLOBALS_H_
