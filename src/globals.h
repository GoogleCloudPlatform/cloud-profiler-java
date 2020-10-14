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

#ifndef CLOUD_PROFILER_AGENT_JAVA_GLOBALS_H_
#define CLOUD_PROFILER_AGENT_JAVA_GLOBALS_H_

#include <assert.h>
#include <dlfcn.h>
#include <jni.h>
#include <jvmti.h>
#include <stdint.h>

#include <unordered_map>

#include "third_party/javaprofiler/jvmti_error.h"
#include "third_party/javaprofiler/stacktraces.h"

#include <glog/logging.h>

#include <string>
using std::string;  // ALLOW_STD_STRING

#define FRIEND_TEST(A, B)

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &);              \
  void operator=(const TypeName &)

#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName();                                    \
  DISALLOW_COPY_AND_ASSIGN(TypeName)

#ifndef CLOUD_PROFILER_AGENT_VERSION
#define CLOUD_PROFILER_AGENT_VERSION "unknown"
#endif

namespace cloud {
namespace profiler {

// TODO: These constants/structures exist in the javaprofiler but will
// make cloud profiler hard to read right now as we move code around. It is
// easier to just re-use the constants this way to keep the code easy to read.
//
// Remove this when porting is done.
using google::javaprofiler::kDeopt;
using google::javaprofiler::kGcActive;
using google::javaprofiler::kNativeStackTrace;
using google::javaprofiler::kNoClassLoad;
using google::javaprofiler::kNotWalkableFrameJava;
using google::javaprofiler::kNotWalkableFrameNotJava;
using google::javaprofiler::kSafepoint;
using google::javaprofiler::kThreadExit;
using google::javaprofiler::kUnknownJava;
using google::javaprofiler::kUnknownNotJava;
using google::javaprofiler::kUnknownState;

using google::javaprofiler::kCallTraceErrorLineNum;
using google::javaprofiler::kMaxFramesToCapture;
using google::javaprofiler::kNativeFrameLineNum;
using google::javaprofiler::kNumCallTraceErrors;

using google::javaprofiler::JVMPI_CallFrame;
using google::javaprofiler::JVMPI_CallTrace;

// Gets us around -Wunused-parameter
#define IMPLICITLY_USE(x) (void)x;

#define AGENTEXPORT __attribute__((visibility("default"))) JNIEXPORT

// Wrap JVMTI functions in this in functions that expect a return
// value and require cleanup.
#define JVMTI_ERROR_CLEANUP_1(error, retval, cleanup) \
  {                                                   \
    int err;                                          \
    if ((err = (error)) != JVMTI_ERROR_NONE) {        \
      LOG(ERROR) << "JVMTI error " << err;            \
      cleanup;                                        \
      return (retval);                                \
    }                                                 \
  }

// Wrap JVMTI functions in this in functions that expect a return value.
#define JVMTI_ERROR_1(error, retval) \
  JVMTI_ERROR_CLEANUP_1(error, retval, /* nothing */)

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

// Things that should probably be user-configurable

// Duration of cpu profiles being collected
static const int kProfileDurationSeconds = 10;

// Length of the profiling interval
static const int kProfileWaitSeconds = 60;  // 1 minute

// Maximum number of profiles to generate (0 for unlimited)
static const int kProfileMaxCount = 0;

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_GLOBALS_H_
