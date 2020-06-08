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

#ifndef THIRD_PARTY_JAVAPROFILER_PROFILE_TEST_H__
#define THIRD_PARTY_JAVAPROFILER_PROFILE_TEST_H__

#include <jvmti.h>

#include "third_party/javaprofiler/profile_proto_builder.h"
#include "third_party/javaprofiler/stacktrace_decls.h"

namespace google {
namespace javaprofiler {

class TestProfileFrameCache : public ProfileFrameCache {
  void ProcessTraces(const ProfileStackTrace *traces, int num_traces) override {
  }

  perftools::profiles::Location *GetLocation(
      const JVMPI_CallFrame &jvm_frame,
      LocationBuilder *location_builder) override {
    return &nop_;
  }

  std::string GetFunctionName(const JVMPI_CallFrame &jvm_frame) { return ""; }

 private:
  perftools::profiles::Location nop_;
};

class JvmProfileTestLib {
 public:
  static jmethodID GetDroppedFrameMethodId() {
    return reinterpret_cast<jmethodID>(13);
  }

  static struct jvmtiInterface_1_ GetDispatchTable();

  /**
   * Returns a jthread object that can be used by the various JvmProfileTestLib
   * methods. It is not a real jthread object but is an identifier to a "fake"
   * thread that is mocked to be at a given point in the code.
   * ie. GetStackTrace will return something sane.
   *
   * thread_id should be an integer with 0 < thread_id < N;
   * N being the value returned by GetMaxThreads.
   *
   * implementation dependent on the test framework.
   * A return of 0 signifies the thread_id is not supported.
   */
  static jthread GetThread(int thread_id);

  /**
   * Returns the number of threads that are supported by GetThread and
   * subsequent methods that would take a thread as argument to fake
   * various threads in action.
   */
  static int GetMaxThreads();

  // Various call counters for testing purposes.
  static std::atomic<int> line_number_call_count;
  static std::atomic<int> method_declaring_class_call_count;
  static std::atomic<int> method_name_call_count;
};

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_PROFILE_TEST_H__
