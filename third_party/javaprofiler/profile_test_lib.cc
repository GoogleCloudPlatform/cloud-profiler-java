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

std::atomic<int> JvmProfileTestLib::line_number_call_count;
std::atomic<int> JvmProfileTestLib::method_declaring_class_call_count;
std::atomic<int> JvmProfileTestLib::method_name_call_count;

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

static void CreateJvmtiString(jvmtiEnv* jvmti, const std::string& name,
                              char** name_str) {
  unsigned char* name_str_u;
  jvmti->Allocate(name.size() + 1, &name_str_u);
  *name_str = reinterpret_cast<char*>(name_str_u);
  memcpy(*name_str, name.c_str(), name.size() + 1);
}

static jvmtiError GetMethodName(jvmtiEnv* jvmti, jmethodID method_id,
                                char** name_str, char** sig_str,
                                char** gsig_str) {
  JvmProfileTestLib::method_name_call_count++;

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
    case 4:
      CreateJvmtiString(jvmti, "fourthMethodName$$Lambda$42.42", name_str);
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
    case 4:
      CreateJvmtiString(jvmti, "Lcom/google/FourthClass;", sig_str);
      break;
    default:
      ADD_FAILURE() << "Unknown class id in test.";
  }

  return JVMTI_ERROR_NONE;
}

static jvmtiError GetMethodDeclaringClass(jvmtiEnv *env, jmethodID method,
                                          jclass *declaring_class) {
  JvmProfileTestLib::method_declaring_class_call_count++;
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
      case 4:
        *source_name_ptr = strdup("FourthClass.java");
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
  JvmProfileTestLib::line_number_call_count++;

  size_t table_size = arraysize(fake_line_number_table) *
      sizeof(jvmtiLineNumberEntry);
  env->Allocate(table_size, reinterpret_cast<unsigned char**>(table_ptr));

  *entry_count_ptr = arraysize(fake_line_number_table);
  std::memcpy(*table_ptr, fake_line_number_table, table_size);

  return JVMTI_ERROR_NONE;
}

static jvmtiError GetStackTrace(jvmtiEnv* env, jthread thread, jint start_depth,
                                jint max_frame_count,
                                jvmtiFrameInfo* frame_buffer, jint* count_ptr) {
  uint64 thread_num = reinterpret_cast<uint64>(thread);

  if (thread_num < 0 || thread_num >= JvmProfileTestLib::GetMaxThreads()) {
    *count_ptr = 0;
    return JVMTI_ERROR_NONE;
  }

  std::vector<jvmtiFrameInfo> frames;

  switch (thread_num) {
    case 0:
      frames.push_back({reinterpret_cast<jmethodID>(1), 30});
      frames.push_back({reinterpret_cast<jmethodID>(2), 64});
      break;
    case 1:
      frames.push_back({reinterpret_cast<jmethodID>(3), 128});
      break;
  }

  jint count = std::min(max_frame_count, (jint) frames.size());
  memcpy(frame_buffer, &frames[0], sizeof(*frame_buffer) * count);
  *count_ptr = count;

  return JVMTI_ERROR_NONE;
}

std::vector<JVMPI_CallFrame> CreateStackTrace(int i) {
  std::vector<JVMPI_CallFrame> frames;

  switch (i) {
    case 0:
      frames.push_back({30, reinterpret_cast<jmethodID>(1)});
      frames.push_back({64, reinterpret_cast<jmethodID>(2)});
      break;
    case 1:
      frames.push_back({128, reinterpret_cast<jmethodID>(3)});
      break;
    default:
      // Add nothing
      ;
  }

  return frames;
}

static jvmtiError ForceGarbageCollection(jvmtiEnv* env) {
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
  jvmti_dispatch_table.GetStackTrace = &GetStackTrace;
  jvmti_dispatch_table.ForceGarbageCollection = &ForceGarbageCollection;
  return jvmti_dispatch_table;
}

int JvmProfileTestLib::GetMaxThreads() { return 2; }

jthread JvmProfileTestLib::GetThread(int thread_id) {
  if (thread_id < 0 || thread_id >= GetMaxThreads()) {
    return 0;
  }

  return reinterpret_cast<jthread>(thread_id);
}

}  // namespace javaprofiler
}  // namespace google
