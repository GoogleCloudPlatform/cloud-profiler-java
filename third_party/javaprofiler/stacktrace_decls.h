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

// This file contains structures used for stacktraces.

#ifndef THIRD_PARTY_JAVAPROFILER_STACKTRACE_DECLS_H_
#define THIRD_PARTY_JAVAPROFILER_STACKTRACE_DECLS_H_

#include <jni.h>

namespace google {
namespace javaprofiler {

// To implement CPU profiling, we rely on an undocumented function called
// AsyncGetCallTrace in the Java virtual machine, and designed to get stack
// traces asynchronously.
// It uses the old JVMPI interface, so we must reconstruct the
// neccesary bits of that here.

// From the comments in the Sun VM, in hotspot/src/share/vm/prims/forte.cpp
//  Fields:
//    1) For Java frame (interpreted and compiled),
//       lineno    - bci of the method being executed or -1 if bci is
//                   not available
//       method_id - jmethodID of the method being executed
//    2) For JNI method
//       lineno    - (-3)
//       method_id - jmethodID of the method being executed
//    3) For native method:
//       lineno    - kNativeFrameLineNum (-99)
//       method_id - the PC for the frame.
typedef struct {
  jint lineno;
  jmethodID method_id;
} JVMPI_CallFrame;

// From the comments in the Sun VM, in hotspot/src/share/vm/prims/forte.cpp
// Fields:
//   env_id     - ID of thread which executed this trace.
//   num_frames - number of frames in the trace.
//                (< 0 indicates the frame is not walkable).
//   frames     - the JVMPI_CallFrames that make up this trace. Callee
//                followed by callers.
typedef struct {
  JNIEnv *env_id;
  jint num_frames;
  JVMPI_CallFrame *frames;
} JVMPI_CallTrace;

// Placeholder (fake) line number for native frames in ASGCT_CallFrame. A native
// frame contains this value in the lineno field. The method_id field contains
// the native PC, instead of a jmethodID. Native frames can be recorded without
// changing the layout of ASGCT_CallFrame this way.
const jint kNativeFrameLineNum = -99;

// Placeholder (fake) line number for call trace error frames. The method_id
// field contains a value from the CallTraceErrors enumeration defined below.
const jint kCallTraceErrorLineNum = -100;

enum CallTraceErrors {
  // 0 is reserved for native stack traces.  This includes JIT and GC threads.
  kNativeStackTrace = 0,
  // The JVMTI class load event is disabled (a prereq for AsyncGetCallTrace).
  kNoClassLoad = -1,
  // For traces in GC.
  kGcActive = -2,
  // We can't figure out what the top (non-Java) frame is.
  kUnknownNotJava = -3,
  // The frame is not Java and not walkable.
  kNotWalkableFrameNotJava = -4,
  // We can't figure out what the top Java frame is.
  kUnknownJava = -5,
  // The frame is Java and not walkable.
  kNotWalkableFrameJava = -6,
  // Unknown thread state (not in Java or native or the VM).
  kUnknownState = -7,
  // The JNIEnv is bad - this likely means the thread has exited.
  kThreadExit = -8,
  // The thread is being deoptimized, so the stack is borked.
  kDeopt = -9,
  // We're in a safepoint, and can't do reporting.
  kSafepoint = -10,
  // Reserving a few values in case AGCT uses new ones in the future.
  kReserved11 = -11,
  kReserved12 = -12,
  kReserved13 = -13,
  kReserved14 = -14,
  kReserved15 = -15,
  kReserved16 = -16,
  kReserved17 = -17,
  kReserved18 = -18,
  kReserved19 = -19,
  kReserved20 = -20,
  // In this case, we are reporting fewer frames than the number of
  // frames involved in collecting a stack trace.
  kJvmTooFewFrames = -21,
  // We collected nothing.
  kNoFrames = -22,
  // No JVM was attached, and our fallback to native tracing failed.
  kNoJvmAttachedAndNativeFailed = -23,
  // Other Java tracing error, and our fallback to native tracing failed.
  kTraceFailedAndNativeFailed = -24,
  // The Java agent is not registered, or does not contain the required
  // profiling support functions.
  kNoAgentTracingFunction = -25,
  // The client passed in a nullptr trace or ucontext argument.
  kNullArgument = -26,
};

// Maximum absolute value of the error code we expect
// AgentStackTrace::AsyncGetCallTrace() to return.
const jint kCallTraceErrors = 26;

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_STACKTRACE_H_
