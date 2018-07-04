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

#include <jvmti.h>
#include <string>

#include "third_party/javaprofiler/globals.h"
#include "third_party/javaprofiler/native.h"
#include "third_party/javaprofiler/stacktrace_decls.h"

#ifndef GOOGLE_JAVAPROFILER_DISPLAY_H_
#define GOOGLE_JAVAPROFILER_DISPLAY_H_

namespace google {
namespace javaprofiler {

// Walks the line number table and return the associated Java line number from a
// given method and location.
// Returns -1 on error or for native methods.
jint GetLineNumber(jvmtiEnv *jvmti, jmethodID method, jlocation location);

// Fill the file_name, class_name, method_name, and line_number parameters using
// the information provided by the frame and using the JVMTI environment.
// When unknown, it fills the parameters with: UnknownFile, UnknownClass,
// UnknownMethod, "", and -1.
bool GetStackFrameElements(jvmtiEnv *jvmti,
                           const JVMPI_CallFrame &frame,
                           string *file_name, string *class_name,
                           string *method_name, string *signature,
                           int *line_number);

// Fill the file_name, class_name, method_name, and line_number parameters using
// the information provided by the frame and using the JVMTI environment.
// This version of the method enables the user to provide the declaring class of
// the frame method. Compared to the version above, which must calculate it.
// When unknown, it fills the parameters with: UnknownFile, UnknownClass,
// UnknownMethod, "", and -1.
bool GetStackFrameElements(jvmtiEnv *jvmti, const JVMPI_CallFrame &frame,
                           jclass declaring_class, string *file_name,
                           string *class_name, string *method_name,
                           string *signature, int *line_number);

}  // namespace javaprofiler
}  // namespace google

#endif  // GOOGLE_JAVAPROFILER_DISPLAY_H_
