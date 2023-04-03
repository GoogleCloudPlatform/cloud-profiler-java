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

#include <iostream>
#include <memory>
#include <ostream>

#include "third_party/javaprofiler/accessors.h"
#include "third_party/javaprofiler/heap_sampler.h"
#include "third_party/javaprofiler/profile_proto_builder.h"
#include "third_party/javaprofiler/stacktrace_decls.h"
#include "third_party/javaprofiler/stacktraces.h"

namespace {

using google::javaprofiler::JVMPI_CallFrame;

std::vector<JVMPI_CallFrame>
TransformFrames(jvmtiFrameInfo *stack_frames, int count) {
  std::vector<JVMPI_CallFrame> frames(count);

  for (int i = 0; i < count; i++) {
    // Note that technically this is not the line number; it is the location but
    // our CPU profiler piggy-backs on JVMPI_CallFrame and uses lineno as a
    // jlocation as well...
    frames[i].lineno = stack_frames[i].location;
    frames[i].method_id = stack_frames[i].method;
  }

  return frames;
}

std::unique_ptr<std::vector<JVMPI_CallFrame>> GetTrace(JNIEnv *jni) {
  std::unique_ptr<std::vector<JVMPI_CallFrame>> trace_result = nullptr;

// Cannot use the fast stacktrace for standalone builds as their standard
// libraries do not support getcontext.

  return trace_result;
}

std::unique_ptr<std::vector<JVMPI_CallFrame>> GetTraceUsingJvmti(
    JNIEnv *jni, jvmtiEnv *jvmti, jthread thread) {
  jint count = 0;
  jvmtiFrameInfo stack_frames[google::javaprofiler::kMaxFramesToCapture];

  jvmtiError err =
      jvmti->GetStackTrace(thread, 0, google::javaprofiler::kMaxFramesToCapture,
                           stack_frames, &count);

  if (err != JVMTI_ERROR_NONE || count <= 0) {
    return nullptr;
  }

  return std::unique_ptr<std::vector<JVMPI_CallFrame>>(
      new std::vector<JVMPI_CallFrame>(TransformFrames(stack_frames, count)));
}

static jstring GetClassName(JNIEnv *jni_env, jobject object,
                            jclass object_klass) {
  jclass cls = jni_env->FindClass("java/lang/Class");
  jmethodID get_name_id =
      jni_env->GetMethodID(cls, "getName", "()Ljava/lang/String;");
  jstring name_obj = static_cast<jstring>(
      jni_env->CallObjectMethod(object_klass, get_name_id));
  return name_obj;
}

static jlong GetThreadId(JNIEnv *jni_env, jthread thread) {
  jclass thread_class = jni_env->FindClass("java/lang/Thread");
  jmethodID get_id_method_id = jni_env->GetMethodID(thread_class,
                                                    "getId", "()J");
  jlong thread_id = jni_env->CallLongMethod(thread, get_id_method_id);
  return thread_id;
}

extern "C" JNIEXPORT void SampledObjectAlloc(jvmtiEnv *jvmti_env,
                                             JNIEnv *jni_env, jthread thread,
                                             jobject object,
                                             jclass object_klass, jlong size) {
  if (!google::javaprofiler::HeapMonitor::Enabled()) {
    return;
  }

  if (!google::javaprofiler::HeapMonitor::HasAllocationInstrumentation() &&
      !google::javaprofiler::HeapMonitor::HasGarbageInstrumentation()) {
    google::javaprofiler::HeapMonitor::AddSample(jni_env, thread, object,
                                                 object_klass, size, nullptr, 0,
                                                 0);
    return;
  }
  jstring class_name = GetClassName(jni_env, object, object_klass);
  const char *name_str = jni_env->GetStringUTFChars(class_name, NULL);
  int name_len = strlen(name_str);
  jbyte *name_bytes = const_cast<jbyte *>(
      reinterpret_cast<const jbyte *>(name_str));

  jlong thread_id = GetThreadId(jni_env, thread);

  google::javaprofiler::HeapMonitor::AddSample(jni_env, thread, object,
                                               object_klass, size, name_bytes,
                                               name_len, thread_id);
  // Invoke all functions in HeapMonitor::alloc_inst_functions_
  google::javaprofiler::HeapMonitor::InvokeAllocationInstrumentationFunctions(
      thread_id, name_bytes, name_len, size, 0);
  jni_env->ReleaseStringUTFChars(class_name, name_str);
}

extern "C" JNIEXPORT void GarbageCollectionFinish(jvmtiEnv *jvmti_env) {
  google::javaprofiler::HeapMonitor::NotifyGCWaitingThread();
}

}  // namespace

namespace google {
namespace javaprofiler {

const HeapEventStorage::GcCallback HeapMonitor::gc_callback_ =
    [](const HeapObjectTrace &elem) {
  HeapMonitor::InvokeGarbageInstrumentationFunctions(elem.ThreadId(),
                                                     elem.Name(),
                                                     elem.NameLength(),
                                                     elem.Size(),
                                                     0);
};

std::atomic<HeapMonitor *> HeapMonitor::heap_monitor_;

std::atomic<jvmtiEnv *> HeapMonitor::jvmti_;
std::atomic<int> HeapMonitor::sampling_interval_;
std::atomic<bool> HeapMonitor::use_jvm_trace_;
std::vector<AllocationInstrumentationFunction>
    HeapMonitor::alloc_inst_functions_;
std::vector<GarbageInstrumentationFunction>
    HeapMonitor::gc_inst_functions_;

HeapEventStorage::HeapEventStorage(jvmtiEnv *jvmti,
                                   ProfileFrameCache *cache,
                                   int max_garbage_size,
                                   GcCallback gc_callback)
    : peak_profile_size_(0),
      max_garbage_size_(max_garbage_size),
      cur_garbage_pos_(0),
      jvmti_(jvmti), cache_(cache), gc_callback_(gc_callback) {
}

void HeapEventStorage::Add(JNIEnv *jni, jthread thread, jobject object,
                           jclass klass, jlong size,
                           const std::vector<JVMPI_CallFrame> &&frames,
                           jbyte *name, jint name_len, jlong thread_id) {
  jweak weak_ref = jni->NewWeakGlobalRef(object);
  if (jni->ExceptionCheck()) {
    LOG(WARNING) << "Failed to create NewWeakGlobalRef, skipping heap sample";
    return;
  }

  HeapObjectTrace live_object(weak_ref, size, std::move(frames), name, name_len,
                              thread_id);

  // Only now lock and get things done quickly.
  std::lock_guard<std::mutex> lock(storage_lock_);
  newly_allocated_objects_.push_back(std::move(live_object));
}

void HeapEventStorage::AddToGarbage(HeapObjectTrace &&obj) {
  if (garbage_objects_.size() >= max_garbage_size_) {
    garbage_objects_[cur_garbage_pos_] = std::move(obj);
    cur_garbage_pos_ = (cur_garbage_pos_ + 1) % max_garbage_size_;
  } else {
    garbage_objects_.push_back(std::move(obj));
  }
}

void HeapEventStorage::MoveLiveObjects(
    JNIEnv *env, std::vector<HeapObjectTrace> *objects,
    std::vector<HeapObjectTrace> *still_live_objects) {
  for (auto &elem : *objects) {
    if (elem.IsLive(env)) {
      still_live_objects->push_back(std::move(elem));
    } else {
      gc_callback_(elem);
      elem.DeleteWeakReference(env);
      AddToGarbage(std::move(elem));
    }
  }
}

int64_t HeapEventStorage::ProfileSize(
    const std::vector<HeapObjectTrace> &live_objects) const {
  int64_t total = 0;
  for (auto &trace : live_objects) {
    total += trace.Size();
  }
  return total;
}

void HeapEventStorage::CompactSamples(JNIEnv *env) {
  std::lock_guard<std::mutex> lock(storage_lock_);

  std::vector<HeapObjectTrace> still_live;

  MoveLiveObjects(env, &newly_allocated_objects_, &still_live);
  MoveLiveObjects(env, &live_objects_, &still_live);

  // Live objects are the objects still alive.
  live_objects_ = std::move(still_live);
  // Newly allocated objects is now reset, those still alive are now in
  // live_objects.
  newly_allocated_objects_.clear();

  // Update peak profile if needed.

  int64_t curr_profile_size = ProfileSize(live_objects_);

  if (curr_profile_size > peak_profile_size_) {
    peak_profile_size_ = curr_profile_size;

    peak_objects_.clear();
    for (auto &object : live_objects_) {
      peak_objects_.push_back(object.Copy());
    }
  }
}

HeapEventStorage::StackTraceArrayBuilder::StackTraceArrayBuilder(
    std::size_t objects_size)
    : objects_size_(objects_size),
      stack_trace_data_(
          new google::javaprofiler::ProfileStackTrace[objects_size_]),
      call_trace_data_(new JVMPI_CallTrace[objects_size_]) {
}

void HeapEventStorage::StackTraceArrayBuilder::AddTrace(
    HeapObjectTrace &object) {
  std::vector<JVMPI_CallFrame> &frames = object.Frames();

  call_trace_data_[curr_trace_] = {nullptr,
                                   static_cast<int>(frames.size()),
                                   frames.data()};

  stack_trace_data_[curr_trace_] = {&call_trace_data_[curr_trace_],
                                    object.Size()};

  // Add the size as a label to help post-processing filtering.
  stack_trace_data_[curr_trace_].trace_and_labels.AddLabel(
      "bytes", object.Size(), "bytes");

  curr_trace_++;
}

std::unique_ptr<perftools::profiles::Profile> HeapEventStorage::ConvertToProto(
    ProfileProtoBuilder *builder, std::vector<HeapObjectTrace> &objects) {
  StackTraceArrayBuilder stack_trace_builder(objects.size());
  for (int i = 0; i < objects.size(); ++i) {
    stack_trace_builder.AddTrace(objects[i]);
  }

  builder->AddTraces(stack_trace_builder.GetStackTraceData(), objects.size());
  return builder->CreateProto();
}

std::unique_ptr<perftools::profiles::Profile>
HeapEventStorage::GetPeakHeapProfiles(JNIEnv *env, int sampling_interval) {
  auto builder =
      ProfileProtoBuilder::ForHeap(env, jvmti_, sampling_interval, cache_);

  std::lock_guard<std::mutex> lock(storage_lock_);
  return ConvertToProto(builder.get(), peak_objects_);
}

std::unique_ptr<perftools::profiles::Profile> HeapEventStorage::GetProfiles(
    JNIEnv *env, int sampling_interval, bool force_gc, bool get_live) {
  auto builder =
      ProfileProtoBuilder::ForHeap(env, jvmti_, sampling_interval, cache_);

  if (force_gc) {
    if (jvmti_->ForceGarbageCollection() != JVMTI_ERROR_NONE) {
      LOG(WARNING) << "Failed to force GC, returning empty heap profile proto";
      return builder->CreateProto();
    }

    CompactSamples(env);
  }

  {
    std::lock_guard<std::mutex> lock(storage_lock_);

    if (get_live) {
      return ConvertToProto(builder.get(), live_objects_);
    }

    return ConvertToProto(builder.get(), garbage_objects_);
  }
}

bool HeapMonitor::CreateGCWaitingThread(jvmtiEnv* jvmti, JNIEnv* jni) {
  jclass cls = jni->FindClass("java/lang/Thread");
  jmethodID constructor = jni->GetMethodID(cls, "<init>", "()V");
  jobject thread = jni->NewGlobalRef(jni->NewObject(cls, constructor));
  if (thread == nullptr) {
    LOG(WARNING) << "Failed to construct the GC waiting thread";
    return false;
  }

  if (jvmti->RunAgentThread(thread, GCWaitingThread, nullptr,
                            JVMTI_THREAD_MIN_PRIORITY) != JVMTI_ERROR_NONE) {
    LOG(WARNING) << "Failed to start the GC waiting thread";
    return false;
  }

  return true;
}

bool HeapMonitor::Supported(jvmtiEnv *jvmti) {
  jvmtiCapabilities caps;
  memset(&caps, 0, sizeof(caps));
  if (jvmti->GetPotentialCapabilities(&caps) != JVMTI_ERROR_NONE) {
    LOG(WARNING) << "Failed to get potential capabilities, disabling the heap "
                 << "sampling monitor";
    return false;
  }

  // If ever this was run with a JDK before JDK11, it would not set this bit as
  // it was added at the end of the structure. Therefore this is a cheap way to
  // check for a runtime "are we running with JDK11+".
  if (!caps.can_generate_sampled_object_alloc_events ||
      !caps.can_generate_garbage_collection_events) {
    // Provide more debug information that this will fail: JVMTI_VERSION and
    // sizeof is really lower level but helps figure out compilation
    // environments.
    LOG(WARNING) << "Capabilites not set up: Sampled: "
                 << caps.can_generate_sampled_object_alloc_events
                 << "; GC Collection: "
                 << caps.can_generate_garbage_collection_events
                 << "; Size of capabilities: " << sizeof(jvmtiCapabilities)
                 << "; JVMTI_VERSION: " << JVMTI_VERSION;
    return false;
  }
  return true;
}

void HeapMonitor::AddSample(JNIEnv *jni_env, jthread thread, jobject object,
                            jclass object_klass, jlong size, jbyte *name,
                            jint name_len, jlong thread_id) {
  auto trace = use_jvm_trace_.load()
    ? GetTrace(jni_env)
    : GetTraceUsingJvmti(jni_env, jvmti_.load(), thread);
  if (trace == nullptr) {
    return;
  }

  GetInstance()->storage_.Add(jni_env, thread, object, object_klass, size,
                              std::move(*trace), name, name_len, thread_id);
}

void HeapMonitor::InvokeAllocationInstrumentationFunctions(jlong thread_id,
                                              jbyte *name,
                                              int name_length,
                                              int size,
                                              jlong gcontext) {
  for (auto fn : GetInstance()->alloc_inst_functions_) {
    fn(thread_id, name, name_length, size, gcontext);
  }
}

void HeapMonitor::AddAllocationInstrumentation(
    AllocationInstrumentationFunction fn) {
  GetInstance()->alloc_inst_functions_.push_back(fn);
}

bool HeapMonitor::HasAllocationInstrumentation() {
  return !GetInstance()->alloc_inst_functions_.empty();
}

bool HeapMonitor::HasGarbageInstrumentation() {
  return !GetInstance()->gc_inst_functions_.empty();
}

void HeapMonitor::InvokeGarbageInstrumentationFunctions(jlong thread_id,
                                                        jbyte *name,
                                                        int name_length,
                                                        int size,
                                                        jlong gcontext) {
  for (auto fn : GetInstance()->gc_inst_functions_) {
    fn(thread_id, name, name_length, size, gcontext);
  }
}

void HeapMonitor::AddGarbageInstrumentation(
    GarbageInstrumentationFunction fn) {
  GetInstance()->gc_inst_functions_.push_back(fn);
}

void HeapMonitor::ShutdownGCWaitingThread() {
  this->NotifyGCWaitingThreadInternal(GcEvent::SHUTDOWN);
  this->WaitForShutdown();
}

void HeapMonitor::WaitForShutdown() {
  // Here we wait for the GC thread to process the shutdown event.
  // We can't use JNI to call "Thread.join" on the thread as FindClass
  // crashes if called in VM shutdown.

  std::unique_lock<std::mutex> lock(gc_waiting_mutex_);

  // If we are woken up without having been notified, just go back to sleep.
  gc_waiting_cv_.wait(lock, [this] { return gc_thread_shutdown; } );
}

void HeapMonitor::AddCallback(jvmtiEventCallbacks *callbacks) {
  callbacks->SampledObjectAlloc = &SampledObjectAlloc;
  callbacks->GarbageCollectionFinish = &GarbageCollectionFinish;
}

// Currently, we enable once and forget about it.
bool HeapMonitor::Enable(jvmtiEnv *jvmti, JNIEnv* jni, int sampling_interval,
                         bool use_jvm_trace) {
  if (!Supported(jvmti)) {
    LOG(WARNING) << "Heap sampling is not supported by the JVM, disabling the "
                 << " heap sampling monitor";
    return false;
  }

  jvmtiCapabilities caps;
  memset(&caps, 0, sizeof(caps));
  // Get line numbers, sample events, and filename for the tests.
  caps.can_get_line_numbers = 1;
  caps.can_get_source_file_name = 1;
  caps.can_generate_sampled_object_alloc_events = 1;
  caps.can_generate_garbage_collection_events = 1;

  if (jvmti->AddCapabilities(&caps) != JVMTI_ERROR_NONE) {
    LOG(WARNING) << "Failed to add capabilities, disabling the heap "
                 << "sampling monitor";
    return false;
  }

  if (jvmti->SetHeapSamplingInterval(sampling_interval) != JVMTI_ERROR_NONE) {
    LOG(WARNING) << "Failed to set the heap sampling interval, disabling the "
                 << "heap sampling monitor";
    return false;
  }

  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                      JVMTI_EVENT_SAMPLED_OBJECT_ALLOC,
                                      nullptr) != JVMTI_ERROR_NONE) {
    LOG(WARNING) << "Failed to enable sampled object alloc event, disabling the"
                 << " heap sampling monitor";
    return false;
  }

  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                      JVMTI_EVENT_GARBAGE_COLLECTION_FINISH,
                                      nullptr) != JVMTI_ERROR_NONE) {
    jvmti->SetEventNotificationMode(JVMTI_DISABLE,
                                    JVMTI_EVENT_SAMPLED_OBJECT_ALLOC,
                                    nullptr);
    LOG(WARNING) << "Failed to enable garbage collection finish event, "
                 << "disabling the heap sampling monitor";
    return false;
  }

  if (use_jvm_trace) {
    Asgct::SetAsgct(Accessors::GetJvmFunction<ASGCTType>("AsyncGetCallTrace"));
  }

  sampling_interval_.store(sampling_interval);
  use_jvm_trace_.store(use_jvm_trace);
  jvmti_.store(jvmti);

  // Ensure this is really a singleton i.e. don't recreate it if sampling is
  // re-enabled.
  if (heap_monitor_ == nullptr) {
    HeapMonitor* monitor = new HeapMonitor();
    if (!monitor->CreateGCWaitingThread(jvmti, jni)) {
      return false;
    }
    heap_monitor_.store(monitor);
  }

  return true;
}

void HeapMonitor::Disable() {
  // Already disabled case
  jvmtiEnv *jvmti = jvmti_.load();
  if (!jvmti) {
    return;
  }
  HeapMonitor* monitor = heap_monitor_.load();
  if (!monitor) {
    LOG(ERROR) << "heap monitor not loaded";
    return;
  }
  jvmti_.store(nullptr);

  jvmti->SetEventNotificationMode(JVMTI_DISABLE,
                                  JVMTI_EVENT_SAMPLED_OBJECT_ALLOC, nullptr);
  jvmti->SetEventNotificationMode(JVMTI_DISABLE,
                                  JVMTI_EVENT_GARBAGE_COLLECTION_FINISH,
                                  nullptr);
  // Notify the agent thread that we are done.
  monitor->ShutdownGCWaitingThread();
  heap_monitor_.store(nullptr);
}

std::unique_ptr<perftools::profiles::Profile> HeapMonitor::GetHeapProfiles(
    JNIEnv* env, bool force_gc) {
  // Note: technically this means that you cannot disable the sampler and then
  // get the profile afterwards; this could be changed if needed.
  if (jvmti_) {
    return GetInstance()->storage_.GetHeapProfiles(env, sampling_interval_,
                                                   force_gc);
  }
  return EmptyHeapProfile(env);
}

std::unique_ptr<perftools::profiles::Profile> HeapMonitor::GetPeakHeapProfiles(
    JNIEnv* env, bool force_gc) {
  // Note: technically this means that you cannot disable the sampler and then
  // get the profile afterwards; this could be changed if needed.
  if (jvmti_) {
    return GetInstance()->storage_.GetPeakHeapProfiles(env, sampling_interval_);
  }
  return EmptyHeapProfile(env);
}

std::unique_ptr<perftools::profiles::Profile>
HeapMonitor::GetGarbageHeapProfiles(JNIEnv* env, bool force_gc) {
  // Note: technically this means that you cannot disable the sampler and then
  // get the profile afterwards; this could be changed if needed.
  if (jvmti_) {
    return GetInstance()->storage_.GetGarbageHeapProfiles(
        env, sampling_interval_, force_gc);
  }
  return EmptyHeapProfile(env);
}

std::unique_ptr<perftools::profiles::Profile> HeapMonitor::EmptyHeapProfile(
    JNIEnv *jni_env) {
  return ProfileProtoBuilder::ForHeap(jni_env, jvmti_, sampling_interval_)
      ->CreateProto();
}

void HeapMonitor::NotifyGCWaitingThreadInternal(GcEvent event) {
  std::unique_lock<std::mutex> lock(gc_waiting_mutex_);
  gc_notify_events_.push_back(event);
  gc_waiting_cv_.notify_all();
}

HeapMonitor::GcEvent HeapMonitor::WaitForGC() {
  std::unique_lock<std::mutex> lock(gc_waiting_mutex_);

  // If we are woken up without having been notified, just go back to sleep.
  gc_waiting_cv_.wait(lock, [this] { return !gc_notify_events_.empty(); } );

  GcEvent result = gc_notify_events_.front();
  gc_notify_events_.pop_front();
  return result;
}

void HeapMonitor::GCWaitingThread(jvmtiEnv* jvmti_env, JNIEnv* jni_env,
                                  void* arg) {
  while( !Enabled()) {}
  GetInstance()->GCWaitingThreadRun(jni_env);
}

void HeapMonitor::GCWaitingThreadRun(JNIEnv* jni_env) {
  while (true) {
    GcEvent event = WaitForGC();

    // Was the heap monitor disabled?
    if (event == GcEvent::SHUTDOWN) {
      std::unique_lock<std::mutex> lock(gc_waiting_mutex_);
      gc_thread_shutdown = true;
      gc_waiting_cv_.notify_all();
      break;
    }

    CompactData(jni_env);
  }

  LOG(INFO) << "Heap sampling GC waiting thread finished";
}

void HeapMonitor::CompactData(JNIEnv* jni_env) {
  storage_.CompactSamples(jni_env);
}

}  // namespace javaprofiler
}  // namespace google
