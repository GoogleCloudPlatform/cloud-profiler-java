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

#include "third_party/javaprofiler/heap_sampler.h"
#include "third_party/javaprofiler/profile_proto_builder.h"

namespace {

std::unique_ptr<std::vector<google::javaprofiler::JVMPI_CallFrame>>
TransformFrames(jvmtiFrameInfo *stack_frames, int count) {
  auto frames =
      std::unique_ptr<std::vector<google::javaprofiler::JVMPI_CallFrame>>(
          new std::vector<google::javaprofiler::JVMPI_CallFrame>(count));

  for (int i = 0; i < count; i++) {
    // Note that technically this is not the line number; it is the location but
    // our CPU profiler piggy-backs on JVMPI_CallFrame and uses lineno as a
    // jlocation as well...
    (*frames)[i].lineno = stack_frames[i].location;
    (*frames)[i].method_id = stack_frames[i].method;
  }

  return frames;
}

extern "C" JNIEXPORT void SampledObjectAlloc(jvmtiEnv *jvmti_env,
                                             JNIEnv *jni_env, jthread thread,
                                             jobject object,
                                             jclass object_klass, jlong size) {
  google::javaprofiler::HeapMonitor::AddSample(jni_env, thread, object,
                                               object_klass, size);
}

extern "C" JNIEXPORT void GarbageCollectionFinish(jvmtiEnv *jvmti_env) {
  google::javaprofiler::HeapMonitor::NotifyGCWaitingThread();
}

}  // namespace

namespace google {
namespace javaprofiler {
std::atomic<jvmtiEnv *> HeapMonitor::jvmti_;
std::atomic<int> HeapMonitor::sampling_interval_;

HeapEventStorage::HeapEventStorage(jvmtiEnv *jvmti, ProfileFrameCache *cache,
                                   int max_garbage_size)
    : max_garbage_size_(max_garbage_size),
      cur_garbage_pos_(0),
      jvmti_(jvmti), cache_(cache) {
}

void HeapEventStorage::Add(JNIEnv *jni, jthread thread, jobject object,
                           jclass klass, jlong size) {
  const int kMaxFrames = 128;
  jint count = 0;
  jvmtiFrameInfo stack_frames[kMaxFrames];
  jvmtiError err =
      jvmti_->GetStackTrace(thread, 0, kMaxFrames, stack_frames, &count);

  if (err == JVMTI_ERROR_NONE && count > 0) {
    auto frames = TransformFrames(stack_frames, count);

    jweak weak_ref = jni->NewWeakGlobalRef(object);
    if (jni->ExceptionCheck()) {
      LOG(WARNING) << "Failed to create NewWeakGlobalRef, skipping heap sample";
      return;
    }

    auto live_object = std::unique_ptr<HeapObjectTrace>(
        new HeapObjectTrace(weak_ref, size, std::move(frames)));

    // Only now lock and get things done quickly.
    std::lock_guard<std::mutex> lock(storage_lock_);
    newly_allocated_objects_.push_back(std::move(live_object));
  }
}

void HeapEventStorage::AddToGarbage(std::unique_ptr<HeapObjectTrace> obj) {
  if (garbage_objects_.size() >= max_garbage_size_) {
    garbage_objects_[cur_garbage_pos_] = std::move(obj);
    cur_garbage_pos_ = (cur_garbage_pos_ + 1) % max_garbage_size_;
  } else {
    garbage_objects_.push_back(std::move(obj));
  }
}

void HeapEventStorage::MoveLiveObjects(
    JNIEnv *env, std::vector<std::unique_ptr<HeapObjectTrace>> *objects,
    std::vector<std::unique_ptr<HeapObjectTrace>> *still_live_objects) {
  for (auto &elem : *objects) {
    if (elem->IsLive(env)) {
      still_live_objects->push_back(std::move(elem));
    } else {
      elem->DeleteWeakReference(env);
      AddToGarbage(std::move(elem));
    }
  }
}

void HeapEventStorage::CompactSamples(JNIEnv *env) {
  std::lock_guard<std::mutex> lock(storage_lock_);

  std::vector<std::unique_ptr<HeapObjectTrace>> still_live;

  MoveLiveObjects(env, &newly_allocated_objects_, &still_live);
  MoveLiveObjects(env, &live_objects_, &still_live);

  // Live objects are the objects still alive.
  live_objects_ = std::move(still_live);
  // Newly allocated objects is now reset, those still alive are now in
  // live_objects.
  newly_allocated_objects_.clear();
}

std::unique_ptr<perftools::profiles::Profile> HeapEventStorage::ConvertToProto(
    ProfileProtoBuilder *builder,
    const std::vector<std::unique_ptr<HeapObjectTrace>> &objects) {

  std::size_t objects_size = objects.size();

  std::unique_ptr<google::javaprofiler::ProfileStackTrace[]> stack_trace_data(
      new google::javaprofiler::ProfileStackTrace[objects_size]);

  std::unique_ptr<JVMPI_CallTrace[]> call_trace_data(
      new JVMPI_CallTrace[objects_size]);

  for (int i = 0; i < objects_size; ++i) {
    auto *object = objects[i].get();
    auto *call_target = call_trace_data.get() + i;
    auto *trace_target = stack_trace_data.get() + i;

    auto *frames = object->Frames();
    *call_target = {nullptr, static_cast<int>(frames->size()), frames->data()};
    *trace_target = {call_target, object->Size()};
  }

  builder->AddTraces(stack_trace_data.get(), objects_size);

  return builder->CreateProto();
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
#ifdef ENABLE_HEAP_SAMPLING
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
#else
  return false;
#endif
}

void HeapMonitor::AddCallback(jvmtiEventCallbacks *callbacks) {
#ifdef ENABLE_HEAP_SAMPLING
  callbacks->SampledObjectAlloc = &SampledObjectAlloc;
  callbacks->GarbageCollectionFinish = &GarbageCollectionFinish;
#endif
}

// Currently, we enable once and forget about it.
bool HeapMonitor::Enable(jvmtiEnv *jvmti, JNIEnv* jni, int sampling_interval) {
#ifdef ENABLE_HEAP_SAMPLING
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

  jvmti_.store(jvmti);
  sampling_interval_.store(sampling_interval);

  if (!GetInstance()->CreateGCWaitingThread(jvmti, jni)) {
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

  return true;
#else
  return false;
#endif
}

void HeapMonitor::Disable() {
#ifdef ENABLE_HEAP_SAMPLING
  jvmtiEnv *jvmti = jvmti_.load();
  if (!jvmti) {
    return;
  }

  jvmti->SetEventNotificationMode(JVMTI_DISABLE,
                                  JVMTI_EVENT_SAMPLED_OBJECT_ALLOC, nullptr);
  jvmti->SetEventNotificationMode(JVMTI_DISABLE,
                                  JVMTI_EVENT_GARBAGE_COLLECTION_FINISH,
                                  nullptr);
  jvmti_.store(nullptr);

  // Notify the agent thread that we are done.
  google::javaprofiler::HeapMonitor::GetInstance()->ShutdownGCWaitingThread();

#else
  // Do nothing: we never enabled ourselves.
#endif
}

std::unique_ptr<perftools::profiles::Profile> HeapMonitor::GetHeapProfiles(
    JNIEnv* env, bool force_gc) {
#ifdef ENABLE_HEAP_SAMPLING
  // Note: technically this means that you cannot disable the sampler and then
  // get the profile afterwards; this could be changed if needed.
  if (jvmti_) {
    return GetInstance()->storage_.GetHeapProfiles(env, sampling_interval_,
                                                   force_gc);
  }
#endif
  return EmptyHeapProfile(env);
}

std::unique_ptr<perftools::profiles::Profile>
HeapMonitor::GetGarbageHeapProfiles(JNIEnv* env, bool force_gc) {
#ifdef ENABLE_HEAP_SAMPLING
  // Note: technically this means that you cannot disable the sampler and then
  // get the profile afterwards; this could be changed if needed.
  if (jvmti_) {
    return GetInstance()->storage_.GetGarbageHeapProfiles(
        env, sampling_interval_, force_gc);
  }
#endif
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
  GetInstance()->GCWaitingThreadRun(jni_env);
}

void HeapMonitor::GCWaitingThreadRun(JNIEnv* jni_env) {
  while (true) {
    GcEvent event = WaitForGC();

    // Was the heap monitor disabled?
    if (event == GcEvent::SHUTDOWN) {
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
