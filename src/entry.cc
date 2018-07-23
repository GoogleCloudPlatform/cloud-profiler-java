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

#include <limits.h>

#include <string>

#include "src/string.h"
#include "src/worker.h"
#include "third_party/javaprofiler/globals.h"
#include "third_party/javaprofiler/stacktraces.h"

DEFINE_bool(cprof_cpu_use_per_thread_timers, false,
            "when true, use per-thread CLOCK_THREAD_CPUTIME_ID timers; "
            "only profiles Java threads, non-Java threads will be missed");
DEFINE_bool(cprof_force_debug_non_safepoints, true,
            "when true, force DebugNonSafepoints flag by subscribing to the"
            "code generation events. This improves the accuracy of profiles,"
            "but may incur a bit of overhead.");

namespace cloud {
namespace profiler {

static Worker *worker;

// ThreadStart / ThreadEnd events may arrive after VMDeath event which destroys
// the worker, so managing the lifetime of the thread table is a bit tricky.
// Just make it a global singleton cleared up when the process exit.
static ThreadTable *threads;

static void JNICALL OnThreadStart(jvmtiEnv *jvmti_env, JNIEnv *jni_env,
                                  jthread thread) {
  IMPLICITLY_USE(jvmti_env);
  IMPLICITLY_USE(thread);
  google::javaprofiler::Accessors::SetCurrentJniEnv(jni_env);
  threads->RegisterCurrent();
}

static void JNICALL OnThreadEnd(jvmtiEnv *jvmti_env, JNIEnv *jni_env,
                                jthread thread) {
  IMPLICITLY_USE(jvmti_env);
  IMPLICITLY_USE(jni_env);
  IMPLICITLY_USE(thread);
  threads->UnregisterCurrent();
}

// This has to be here, or the VM turns off class loading events.
// And AsyncGetCallTrace needs class loading events to be turned on!
static void JNICALL OnClassLoad(jvmtiEnv *jvmti_env, JNIEnv *jni_env,
                                jthread thread, jclass klass) {
  IMPLICITLY_USE(jvmti_env);
  IMPLICITLY_USE(jni_env);
  IMPLICITLY_USE(thread);
  IMPLICITLY_USE(klass);
}

static void JNICALL OnCompiledMethodLoad(jvmtiEnv *jvmti_env, jmethodID method,
                                         jint code_size, const void *code_addr,
                                         jint map_length,
                                         const jvmtiAddrLocationMap *map,
                                         const void *compile_info) {
  // The callback is here to enable DebugNonSafepoints by default. See
  // https://stackoverflow.com/questions/37298962/how-can-jvmti-agent-set-a-jvm-flag-on-startup.
  IMPLICITLY_USE(jvmti_env);
  IMPLICITLY_USE(method);
  IMPLICITLY_USE(code_size);
  IMPLICITLY_USE(code_addr);
  IMPLICITLY_USE(map_length);
  IMPLICITLY_USE(map);
  IMPLICITLY_USE(compile_info);
}

// Calls GetClassMethods on a given class to force the creation of
// jmethodIDs of it.
void CreateJMethodIDsForClass(jvmtiEnv *jvmti, jclass klass) {
  jint method_count;
  google::javaprofiler::JvmtiScopedPtr<jmethodID> methods(jvmti);
  jvmtiError e = jvmti->GetClassMethods(klass, &method_count, methods.GetRef());
  if (e != JVMTI_ERROR_NONE && e != JVMTI_ERROR_CLASS_NOT_PREPARED) {
    // JVMTI_ERROR_CLASS_NOT_PREPARED is okay because some classes may
    // be loaded but not prepared at this point.
    google::javaprofiler::JvmtiScopedPtr<char> ksig(jvmti);
    JVMTI_ERROR((jvmti->GetClassSignature(klass, ksig.GetRef(), NULL)));
    LOG(ERROR) << "Failed to create method IDs for methods in class "
               << ksig.Get() << " with error " << e;
  }
}

void JNICALL OnVMInit(jvmtiEnv *jvmti, JNIEnv *jni_env, jthread thread) {
  IMPLICITLY_USE(thread);
  LOG(INFO) << "On VM init";
  // Forces the creation of jmethodIDs of the classes that had already
  // been loaded (eg java.lang.Object, java.lang.ClassLoader) and
  // OnClassPrepare() misses.
  jint class_count;
  google::javaprofiler::JvmtiScopedPtr<jclass> classes(jvmti);
  JVMTI_ERROR((jvmti->GetLoadedClasses(&class_count, classes.GetRef())));
  jclass *class_list = classes.Get();
  for (int i = 0; i < class_count; ++i) {
    jclass klass = class_list[i];
    CreateJMethodIDsForClass(jvmti, klass);
  }
  worker->Start(jni_env);
}

void JNICALL OnClassPrepare(jvmtiEnv *jvmti_env, JNIEnv *jni_env,
                            jthread thread, jclass klass) {
  IMPLICITLY_USE(jni_env);
  IMPLICITLY_USE(thread);
  // We need to do this to "prime the pump", as it were -- make sure
  // that all of the methodIDs have been initialized internally, for
  // AsyncGetCallTrace.  I imagine it slows down class loading a mite,
  // but honestly, how fast does class loading have to be?
  CreateJMethodIDsForClass(jvmti_env, klass);
}

void JNICALL OnVMDeath(jvmtiEnv *jvmti_env, JNIEnv *jni_env) {
  IMPLICITLY_USE(jvmti_env);
  IMPLICITLY_USE(jni_env);
  LOG(INFO) << "On VM death";
  worker->Stop();
  delete worker;
  worker = NULL;
}

static bool PrepareJvmti(jvmtiEnv *jvmti) {
  LOG(INFO) << "Prepare JVMTI";

  // Set the list of permissions to do the various internal VM things
  // we want to do.
  jvmtiCapabilities caps;

  memset(&caps, 0, sizeof(caps));
  caps.can_generate_all_class_hook_events = 1;

  caps.can_get_source_file_name = 1;
  caps.can_get_line_numbers = 1;
  caps.can_get_bytecodes = 1;
  caps.can_get_constant_pool = 1;
  if (FLAGS_cprof_force_debug_non_safepoints) {
    caps.can_generate_compiled_method_load_events = 1;
  }

  jvmtiCapabilities all_caps;
  int error;

  if (JVMTI_ERROR_NONE ==
      (error = jvmti->GetPotentialCapabilities(&all_caps))) {
    // This makes sure that if we need a capability, it is one of the
    // potential capabilities.  The technique isn't wonderful, but it
    // is compact and as likely to be compatible between versions as
    // anything else.
    char *has = reinterpret_cast<char *>(&all_caps);
    const char *should_have = reinterpret_cast<const char *>(&caps);
    for (int i = 0; i < sizeof(all_caps); i++) {
      if ((should_have[i] != 0) && (has[i] == 0)) {
        return false;
      }
    }

    // This adds the capabilities.
    if ((error = jvmti->AddCapabilities(&caps)) != JVMTI_ERROR_NONE) {
      LOG(ERROR) << "Failed to add capabilities with error " << error;
      return false;
    }
  }
  return true;
}

static bool RegisterJvmti(jvmtiEnv *jvmti) {
  // Create the list of callbacks to be called on given events.
  jvmtiEventCallbacks *callbacks = new jvmtiEventCallbacks();
  memset(callbacks, 0, sizeof(jvmtiEventCallbacks));

  callbacks->ThreadStart = &OnThreadStart;
  callbacks->ThreadEnd = &OnThreadEnd;
  callbacks->VMInit = &OnVMInit;
  callbacks->VMDeath = &OnVMDeath;
  callbacks->ClassLoad = &OnClassLoad;
  callbacks->ClassPrepare = &OnClassPrepare;

  std::vector<jvmtiEvent> events = {
      JVMTI_EVENT_CLASS_LOAD, JVMTI_EVENT_CLASS_PREPARE,
      JVMTI_EVENT_THREAD_END, JVMTI_EVENT_THREAD_START,
      JVMTI_EVENT_VM_DEATH,   JVMTI_EVENT_VM_INIT,
  };

  if (FLAGS_cprof_force_debug_non_safepoints) {
    callbacks->CompiledMethodLoad = &OnCompiledMethodLoad;
    events.push_back(JVMTI_EVENT_COMPILED_METHOD_LOAD);
  }

  JVMTI_ERROR_1(
      (jvmti->SetEventCallbacks(callbacks, sizeof(jvmtiEventCallbacks))),
      false);

  // Enable the callbacks to be triggered when the events occur.
  // Events are enumerated in jvmstatagent.h
  for (int i = 0; i < events.size(); i++) {
    JVMTI_ERROR_1(
        (jvmti->SetEventNotificationMode(JVMTI_ENABLE, events[i], NULL)),
        false);
  }

  return true;
}

static void ParseArguments(const char *options) {
  // Split agent options to command line argument style data structure.
  if (options == nullptr) {
    options = "";
  }
  std::vector<string> split_options = Split(options, ',');

  std::vector<char *> argv_vector;
  argv_vector.push_back(const_cast<char *>("cprof_java_agent"));
  for (const string &split_option : split_options) {
    argv_vector.push_back(const_cast<char *>(split_option.c_str()));
  }

  int argc = argv_vector.size();
  char **argv = &argv_vector[0];

#ifdef STANDALONE_BUILD
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging("cprof");
#else
  InitGoogle(argv[0], &argc, &argv, true);
#endif
}

jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
  IMPLICITLY_USE(reserved);
  int err;
  jvmtiEnv *jvmti;

  ParseArguments(options);  // Initializes logger -- do not log before this call
  LOG(INFO) << "Profiler agent loaded";

  google::javaprofiler::Accessors::Init();
  google::javaprofiler::AttributeTable::Init();

  if ((err = (vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION))) !=
      JNI_OK) {
    LOG(ERROR) << "JNI Error " << err;
    return 1;
  }

  if (!PrepareJvmti(jvmti)) {
    LOG(ERROR) << "Failed to initialize JVMTI.  Continuing...";
    return 0;
  }

  // The process exit will free the memory. See comments to the variable on why.
  // Initialize before registering the JVMTI callbacks to avoid the unlikely
  // race of getting thread events before the thread table is born.
  threads = new ThreadTable(FLAGS_cprof_cpu_use_per_thread_timers);

  if (!RegisterJvmti(jvmti)) {
    LOG(ERROR) << "Failed to enable JVMTI events.  Continuing...";
    // We fail hard here because we may have failed in the middle of
    // registering callbacks, which will leave the system in an
    // inconsistent state.
    return 1;
  }

  google::javaprofiler::Asgct::SetAsgct(
      google::javaprofiler::Accessors::GetJvmFunction<
      google::javaprofiler::ASGCTType>("AsyncGetCallTrace"));

  worker = new Worker(jvmti, threads);
  return 0;
}

void JNICALL Agent_OnUnload(JavaVM *vm) {
  IMPLICITLY_USE(vm);
  google::javaprofiler::Accessors::Destroy();
}

}  // namespace profiler
}  // namespace cloud

AGENTEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
  return cloud::profiler::Agent_OnLoad(vm, options, reserved);
}

AGENTEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
  return cloud::profiler::Agent_OnUnload(vm);
}

extern "C" AGENTEXPORT
jboolean JNICALL
Java_com_google_cloud_profiler_Profiler_isEnabled(
    JNIEnv *, jclass) {
  return cloud::profiler::Worker::IsProfilingEnabled();
}

extern "C" AGENTEXPORT
void JNICALL
Java_com_google_cloud_profiler_Profiler_enable(
    JNIEnv *, jclass) {
  cloud::profiler::Worker::EnableProfiling();
}

extern "C" AGENTEXPORT
void JNICALL
Java_com_google_cloud_profiler_Profiler_disable(
    JNIEnv *, jclass) {
  cloud::profiler::Worker::DisableProfiling();
}

extern "C" AGENTEXPORT
jbyteArray JNICALL
Java_com_google_cloud_profiler_Profiler_collect(
    JNIEnv *env, jclass, jstring type, jlong duration, jlong sampling_period) {
  const char *value_utf = env->GetStringUTFChars(type, nullptr);
  string pt(value_utf);
  env->ReleaseStringUTFChars(type, value_utf);

  if (pt != "cpu" && pt != "wall") {
    env->ThrowNew(env->FindClass("java/lang/RuntimeException"),
                  "unknown profiling type");
    return nullptr;
  }

  if (duration <= 0 || duration > 300 || sampling_period <= 0) {
    env->ThrowNew(env->FindClass("java/lang/RuntimeException"),
                  "bad arguments");
    return nullptr;
  }

  string profile = cloud::profiler::worker->CollectProfile(pt, duration, sampling_period);

  const auto output = env->NewByteArray(profile.length());
  env->SetByteArrayRegion(output, 0, profile.length(), reinterpret_cast<const jbyte *>(profile.data()));
  return output;
}
