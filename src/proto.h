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

#ifndef CLOUD_PROFILER_AGENT_JAVA_PROTO_H_
#define CLOUD_PROFILER_AGENT_JAVA_PROTO_H_

#include "src/profiler.h"
#include "perftools/profiles/proto/builder.h"

namespace cloud {
namespace profiler {

// Generates a CPU profile in a compressed serialized profile.proto
// from a collection of java stack traces, symbolized using the jvmti.
// Data in traces will be cleared.
string SerializeAndClearJavaCpuTraces(
    jvmtiEnv *jvmti, const google::javaprofiler::NativeProcessInfo &native_info,
    const char *profile_type, int64_t duration_nanos, int64_t period_nanos,
    google::javaprofiler::TraceMultiset *traces, int64_t unknown_count);

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_PROTO_H_
