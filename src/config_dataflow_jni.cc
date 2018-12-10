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

#include <jni.h>

#include "src/worker.h"
#include "third_party/javaprofiler/stacktraces.h"

AGENTEXPORT
void JNICALL
Java_com_google_cloud_dataflow_worker_profiler_Profiler_enable(
    JNIEnv *, jclass) {
  cloud::profiler::Worker::EnableProfiling();
}

AGENTEXPORT
void JNICALL
Java_com_google_cloud_dataflow_worker_profiler_Profiler_disable(
    JNIEnv *, jclass) {
  cloud::profiler::Worker::DisableProfiling();
}

AGENTEXPORT jint JNICALL
Java_com_google_cloud_dataflow_worker_profiler_Profiler_registerAttribute(
    JNIEnv *env, jclass, jstring value) {
  const char *value_utf = env->GetStringUTFChars(value, nullptr);
  int ret = google::javaprofiler::AttributeTable::RegisterString(value_utf);
  env->ReleaseStringUTFChars(value, value_utf);
  return static_cast<jint>(ret);
}

AGENTEXPORT jint JNICALL
Java_com_google_cloud_dataflow_worker_profiler_Profiler_setAttribute(
    JNIEnv *env, jclass, jint attr) {
  int64_t ret = google::javaprofiler::Accessors::GetAttribute();
  google::javaprofiler::Accessors::SetAttribute(static_cast<int64_t>(attr));
  return static_cast<jint>(ret);
}

AGENTEXPORT jint JNICALL
Java_com_google_cloud_dataflow_worker_profiler_Profiler_getAttribute(
    JNIEnv *env, jclass) {
  int64_t ret = google::javaprofiler::Accessors::GetAttribute();
  return static_cast<jint>(ret);
}
