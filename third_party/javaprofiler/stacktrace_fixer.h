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

// This file contains headers for the classes used to format the
// various monitoring information that the agent exports.

#ifndef THIRD_PARTY_JAVAPROFILER_STACKTRACE_FIXER_H_
#define THIRD_PARTY_JAVAPROFILER_STACKTRACE_FIXER_H_

#include "third_party/javaprofiler/globals.h"

namespace google {
namespace javaprofiler {
  // Simplifies the name of a function to make it more human readable, and group
  // related functions under a single name.
  void SimplifyFunctionName(string *name);

  // Fix the parameter signature from a JVM type signature to a pretty-print
  // one.
  void FixMethodParameters(string *signature);

  // Convert class name format from "pkg/name/class" to "pkg.name.class".
  void FixPath(string *s);

  // Pretty-prints a JVM type signature.
  void PrettyPrintSignature(string *s);
}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_STACKTRACE_FIXER_H_
