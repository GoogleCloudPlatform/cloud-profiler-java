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

#include "third_party/javaprofiler/profile_test_lib.h"

#include "testing/base/public/gunit.h"

namespace google {
namespace javaprofiler {

static jvmtiError Allocate(jvmtiEnv* jvmti,
                           jlong size,
                           unsigned char** mem_ptr) {
  *mem_ptr = reinterpret_cast<unsigned char*>(malloc(size));
  return JVMTI_ERROR_NONE;
}

static jvmtiError Deallocate(jvmtiEnv* jvmti, unsigned char* mem) {
  free(mem);
  return JVMTI_ERROR_NONE;
}

static void CreateJvmtiString(jvmtiEnv* jvmti,
                              const string& name,
                              char** name_str) {
  unsigned char* name_str_u;
  jvmti->Allocate(name.size() + 1, &name_str_u);
  *name_str = reinterpret_cast<char*>(name_str_u);
  memcpy(*name_str, name.c_str(), name.size() + 1);
}

static jvmtiError GetMethodName(jvmtiEnv* jvmti, jmethodID method_id,
                                char** name_str, char** sig_str,
                                char** gsig_str) {
  switch (reinterpret_cast<uint64>(method_id)) {
    case 1:
      CreateJvmtiString(jvmti, "methodName", name_str);
      CreateJvmtiString(jvmti, "(I)B", sig_str);
      break;
    case 2:
      CreateJvmtiString(jvmti, "secondMethodName", name_str);
      CreateJvmtiString(jvmti, "()V", sig_str);
      break;
    case 3:
      CreateJvmtiString(jvmti, "thirdMethodName", name_str);
      CreateJvmtiString(jvmti, "()V", sig_str);
      break;
    default:
      ADD_FAILURE() << "Unknown method id in test.";
  }

  return JVMTI_ERROR_NONE;
}

static jvmtiError GetClassSignature(jvmtiEnv* jvmti, jclass declaring_class,
                                    char** sig_str, char** gsig_str) {
  switch (reinterpret_cast<uint64>(declaring_class)) {
    case 1:
      CreateJvmtiString(jvmti, "Lcom/google/SomeClass;", sig_str);
      break;
    case 2:
      CreateJvmtiString(jvmti, "Lcom/google/SecondClass;", sig_str);
      break;
    case 3:
      CreateJvmtiString(jvmti, "Lcom/google/ThirdClass;", sig_str);
      break;
    default:
      ADD_FAILURE() << "Unknown class id in test.";
  }

  return JVMTI_ERROR_NONE;
}

static jvmtiError GetMethodDeclaringClass(jvmtiEnv *env, jmethodID method,
                                          jclass *declaring_class) {
  *declaring_class = reinterpret_cast<jclass>(method);
  return JVMTI_ERROR_NONE;
}

static jvmtiError GetSourceFileName(jvmtiEnv *env, jclass klass,
                                    char **source_name_ptr) {
  if (source_name_ptr) {
    switch (reinterpret_cast<uint64>(klass)) {
      case 1:
        *source_name_ptr = strdup("SomeClass.java");
        break;
      case 2:
        *source_name_ptr = strdup("SecondClass.java");
        break;
      case 3:
        *source_name_ptr = strdup("ThirdClass.java");
        break;
      default:
        ADD_FAILURE() << "Unknown class id in test.";
    }
  }

  return JVMTI_ERROR_NONE;
}

static jvmtiLineNumberEntry fake_line_number_table[] = {
  { 30, 4 },
  { 60, 5 },
  { 90, 6 },
  { 120, 7 },
  { 150, 8 },
};

static jvmtiError GetLineNumberTable(jvmtiEnv *env, jmethodID method,
                                     jint *entry_count_ptr,
                                     jvmtiLineNumberEntry **table_ptr) {
  size_t table_size = arraysize(fake_line_number_table) *
      sizeof(jvmtiLineNumberEntry);
  env->Allocate(table_size, reinterpret_cast<unsigned char**>(table_ptr));

  *entry_count_ptr = arraysize(fake_line_number_table);
  std::memcpy(*table_ptr, fake_line_number_table, table_size);

  return JVMTI_ERROR_NONE;
}

struct jvmtiInterface_1_ JvmProfileTestLib::GetDispatchTable() {
  struct jvmtiInterface_1_ jvmti_dispatch_table;
  jvmti_dispatch_table.GetMethodName = &GetMethodName;
  jvmti_dispatch_table.GetMethodDeclaringClass = &GetMethodDeclaringClass;
  jvmti_dispatch_table.GetClassSignature = &GetClassSignature;
  jvmti_dispatch_table.GetSourceFileName = &GetSourceFileName;
  jvmti_dispatch_table.GetLineNumberTable = &GetLineNumberTable;
  jvmti_dispatch_table.Allocate = &Allocate;
  jvmti_dispatch_table.Deallocate = &Deallocate;
  return jvmti_dispatch_table;
}

}  // namespace javaprofiler
}  // namespace google
