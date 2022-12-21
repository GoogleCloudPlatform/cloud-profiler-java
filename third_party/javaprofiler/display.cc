// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "third_party/javaprofiler/display.h"

#include <inttypes.h>
#include <cstring>

#include "third_party/javaprofiler/stacktrace_fixer.h"

namespace google {
namespace javaprofiler {

namespace {

// This method changes the standard class signature "Lfoo/bar;" format
// to a more readable "foo.bar" format.
bool CleanJavaSignature(char *signature_ptr) {
  size_t signature_length = strlen(signature_ptr);  // ugh!
  if (signature_length < 3) {  // I'm not going to even try.
    return false;
  }

  // Java classes start with L. It is faster to just replace this by a space.
  signature_ptr[0] = ' ';
  for (size_t i = 1; i < signature_length - 1; ++i) {
    if (signature_ptr[i] == '/') {
      signature_ptr[i] = '.';
    }
  }
  signature_ptr[signature_length - 1] = '\0';
  return true;
}

const char kFileUnknown[] = "UnknownFile";
const char kClassUnknown[] = "UnknownClass";
const char kMethodUnknown[] = "UnknownMethod";
const char kMethodIDUnknown[] = "UnknownMethodID";
// Since the method is unknown, associated signature is empty.
const char kSignatureUnknown[] = "";

void GetMethodName(jvmtiEnv *jvmti, jmethodID method_id,
                   std::string *method_name, std::string *signature) {
  jint error;
  JvmtiScopedPtr<char> signature_ptr(jvmti);
  JvmtiScopedPtr<char> name_ptr(jvmti);

  // Get method name, put it in name_ptr
  if ((error = jvmti->GetMethodName(method_id, name_ptr.GetRef(),
                                    signature_ptr.GetRef(),
                                    nullptr)) == JVMTI_ERROR_NONE) {
    *method_name = name_ptr.Get();
    *signature = signature_ptr.Get();
    return;
  }

  static int once = 0;
  if (!once) {
    once = 1;
    if (error == JVMTI_ERROR_INVALID_METHODID) {
      LOG(INFO) << "One of your monitoring interfaces "
                   "is having trouble resolving its stack traces. "
                   "GetMethodName on a jmethodID "
                << method_id
                << " involved in a stack trace resulted in an "
                   "INVALID_METHODID error which usually "
                   "indicates its declaring class has been unloaded.";
    } else {
      LOG(ERROR) << "Unexpected JVMTI error " << error << " in GetMethodName";
    }
  }
  if (error == JVMTI_ERROR_INVALID_METHODID) {
    *method_name = kMethodIDUnknown;
  } else {
    *method_name = kMethodUnknown;
  }
  *signature = kSignatureUnknown;
}

void GetClassAndFileName(jvmtiEnv *jvmti, jmethodID method_id,
                         jclass declaring_class, std::string *file_name,
                         std::string *class_name) {
  JvmtiScopedPtr<char> source_name_ptr(jvmti);
  if (JVMTI_ERROR_NONE !=
      jvmti->GetSourceFileName(declaring_class, source_name_ptr.GetRef())) {
    *file_name = kFileUnknown;
  } else {
    *file_name = source_name_ptr.Get();
  }

  JvmtiScopedPtr<char> signature_ptr(jvmti);
  if (JVMTI_ERROR_NONE != jvmti->GetClassSignature(declaring_class,
                                                   signature_ptr.GetRef(),
                                                   nullptr)) {
    *class_name = kClassUnknown;
  } else {
    bool cleaned = CleanJavaSignature(signature_ptr.Get());
    if (cleaned) {
      // CleanJavaSignature prepends a ' ' character:
      //   - Java classes start with L and it is faster to replace it by a ' '
      //   than shift everything.
      *class_name = signature_ptr.Get() + 1;
    } else {
      *class_name = signature_ptr.Get();
    }
  }
}

void FillFieldsWithUnknown(std::string *file_name, std::string *class_name,
                           std::string *method_name, std::string *signature,
                           int *line_number) {
  *file_name = kFileUnknown;
  *class_name = kClassUnknown;
  *method_name = kMethodUnknown;
  *signature = kSignatureUnknown;
  if (line_number) {
    *line_number = 0;
  }
}

void FillMethodSignatureAndLine(jvmtiEnv *jvmti, const JVMPI_CallFrame &frame,
                                std::string *method_name,
                                std::string *signature, int *line_number) {
  GetMethodName(jvmti, frame.method_id, method_name, signature);

  // frame.lineno is actually a bci if it is a Java method; Asgct is piggy
  // backing on the structure field. For natives, this would be -1 and
  // GetLineNumber handles it.
  if (line_number) {
    *line_number = GetLineNumber(jvmti, frame.method_id, frame.lineno);
  }
}

}  // end namespace

jint GetLineNumber(jvmtiEnv *jvmti, jmethodID method, jlocation location) {
  jint entry_count;
  JvmtiScopedPtr<jvmtiLineNumberEntry> table_ptr_ctr(jvmti);
  jint line_number = -1;

  // Shortcut for native methods.
  if (location < 0) {
    return -1;
  }

  int jvmti_error =
      jvmti->GetLineNumberTable(method, &entry_count, table_ptr_ctr.GetRef());

  if (JVMTI_ERROR_NONE != jvmti_error || entry_count <= 0) {
    if (JVMTI_ERROR_ABSENT_INFORMATION == jvmti_error) {
      static bool no_debug_info = false;
      if (!no_debug_info) {
        LOG(INFO)
            << "No line number information was found in your bytecode."
               " Some monitoring interfaces may report -1 ""for line numbers.";
        no_debug_info = true;
      }
    }
    return -1;
  }

  jvmtiLineNumberEntry *table_ptr = table_ptr_ctr.Get();
  if (entry_count == 1) {
    return table_ptr[0].line_number;
  }
  if (location == 0) {
    // Return method first line.
    return table_ptr[0].line_number;
  }

  jlocation last_location = table_ptr[0].start_location;
  for (int l = 1; l < entry_count; l++) {
    // ... and if you see one that is in the right place for your
    // location, you've found the line number!
    if ((location < table_ptr[l].start_location) &&
        (location >= last_location)) {
      line_number = table_ptr[l - 1].line_number;
      return line_number;
    }
    last_location = table_ptr[l].start_location;
  }

  if (location >= last_location) {
    return table_ptr[entry_count - 1].line_number;
  }

  return -1;
}

bool GetStackFrameElements(JNIEnv *jni, jvmtiEnv *jvmti,
                           const JVMPI_CallFrame &frame, std::string *file_name,
                           std::string *class_name, std::string *method_name,
                           std::string *signature, int *line_number) {
  if (!jvmti) {
    FillFieldsWithUnknown(file_name, class_name, method_name, signature,
                          line_number);
    return false;
  }

  jclass declaring_class = nullptr;
  if (JVMTI_ERROR_NONE !=
      jvmti->GetMethodDeclaringClass(frame.method_id, &declaring_class)) {
    *file_name = kFileUnknown;
    *class_name = kClassUnknown;
    FillMethodSignatureAndLine(jvmti, frame, method_name, signature,
                               line_number);
    return true;
  }

  ScopedLocalRef<jclass> declaring_class_managed(jni, declaring_class);
  return GetStackFrameElements(jvmti, frame, declaring_class, file_name,
                               class_name, method_name, signature, line_number);
}

bool GetStackFrameElements(jvmtiEnv *jvmti, const JVMPI_CallFrame &frame,
                           jclass declaring_class, std::string *file_name,
                           std::string *class_name, std::string *method_name,
                           std::string *signature, int *line_number) {
  if (!jvmti) {
    FillFieldsWithUnknown(file_name, class_name, method_name, signature,
                          line_number);
    return false;
  }

  GetClassAndFileName(jvmti, frame.method_id, declaring_class, file_name,
                      class_name);

  FillMethodSignatureAndLine(jvmti, frame, method_name, signature, line_number);
  return true;
}

}  // namespace javaprofiler
}  // namespace google
