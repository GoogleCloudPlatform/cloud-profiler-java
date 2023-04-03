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

#ifndef THIRD_PARTY_JAVAPROFILER_HEAP_SAMPLER_H_
#define THIRD_PARTY_JAVAPROFILER_HEAP_SAMPLER_H_

#include <jni.h>
#include <jvmti.h>

#include <atomic>
#include <condition_variable>  // NOLINT
#include <functional>
#include <list>
#include <memory>
#include <mutex>  // NOLINT
#include <vector>

#include "third_party/javaprofiler/profile_proto_builder.h"

typedef void (*AllocationInstrumentationFunction)(jlong thread_id,
                                                  jbyte *name,
                                                  int name_length,
                                                  int size,
                                                  jlong gcontext);

typedef void (*GarbageInstrumentationFunction)(jlong thread_id,
                                               jbyte *name,
                                               int name_length,
                                               int size,
                                               jlong gcontext);

namespace google {
namespace javaprofiler {

// A sampled heap object, defined by the object, its size, and the stack
// frame.
class HeapObjectTrace {
 public:
  // This object owns the jweak object parameter. It is freed when the object
  // is sent to the garbage list, and the object is set to nullptr.
  HeapObjectTrace(jweak object, jlong size,
                  const std::vector<JVMPI_CallFrame> &&frames, jbyte *name,
                  int name_length, jlong thread_id)
      : object_(object), size_(size), frames_(std::move(frames)),
        name_(name), name_length_(name_length), thread_id_(thread_id) {}

  HeapObjectTrace(jweak object, jlong size,
                  const std::vector<JVMPI_CallFrame> &frames)
      : object_(object), size_(size), frames_(frames) {}

  // Allow moving.
  HeapObjectTrace(HeapObjectTrace&& o) = default;
  HeapObjectTrace& operator=(HeapObjectTrace&& o) = default;

  // No copying allowed.
  HeapObjectTrace(const HeapObjectTrace& o) = delete;
  HeapObjectTrace& operator=(const HeapObjectTrace& o) = delete;

  std::vector<JVMPI_CallFrame> &Frames() {
    return frames_;
  }

  int Size() const {
    return size_;
  }

  jbyte *Name() const {
    return name_;
  }

  int NameLength() const {
    return name_length_;
  }

  jlong ThreadId() const {
    return thread_id_;
  }

  void DeleteWeakReference(JNIEnv* env) {
    env->DeleteWeakGlobalRef(object_);
    object_ = nullptr;
  }

  bool IsLive(JNIEnv *env) {
    // When GC collects the object, the object represented by the weak
    // reference will be considered as the same object as NULL.
    return !env->IsSameObject(object_, NULL);
  }

  // Make copying an explicit operation for the one case we need it (adding
  // to the peak heapz storage)
  HeapObjectTrace Copy() {
    return HeapObjectTrace(object_, size_, frames_);
  }

 private:
  jweak object_;
  int size_;
  std::vector<JVMPI_CallFrame> frames_;
  jbyte *name_;
  int name_length_;
  jlong thread_id_;
};

// Storage for the sampled heap objects recorded from the heap sampling JVMTI
// API callbacks.
class HeapEventStorage {
 public:
  typedef std::function<void(const HeapObjectTrace &)> GcCallback;

  HeapEventStorage(jvmtiEnv *jvmti,
                   ProfileFrameCache *cache = nullptr,
                   int max_garbage_size = 200,
                   GcCallback gc_callback = [](const HeapObjectTrace &t){});

  // TODO: establish correct shutdown sequence: how do we ensure that
  // things are not going to go awfully wrong at shutdown, is it this class' job
  // or should it be the owner of this class' instance's job?

  // Adds an object to the storage system.
  void Add(JNIEnv *jni, jthread thread, jobject object, jclass klass,
           jlong size, const std::vector<JVMPI_CallFrame> &&frames, jbyte *name,
           jint name_len, jlong thread_id);

  // Returns a perftools::profiles::Profile with the objects stored via
  // calls to the Add method.
  // force_gc provides a means to force GC before returning the sampled heap
  // profiles;
  // setting force_gc to true has a performance impact and is discouraged.
  std::unique_ptr<perftools::profiles::Profile> GetHeapProfiles(
      JNIEnv *env, int sampling_interval, bool force_gc = false) {
    return GetProfiles(env, sampling_interval, force_gc, true);
  }

  // Return the largest profile recorded so far.
  std::unique_ptr<perftools::profiles::Profile> GetPeakHeapProfiles(
      JNIEnv *env, int sampling_interval);

  // Returns a perftools::profiles::Profile with the objects that have been
  // GC'd.
  // force_gc provides a means to force GC before returning the sampled heap
  // profiles;
  // setting force_gc to true has a performance impact and is discouraged.
  std::unique_ptr<perftools::profiles::Profile> GetGarbageHeapProfiles(
      JNIEnv* env, int sampling_interval, bool force_gc = false) {
    return GetProfiles(env, sampling_interval, force_gc, false);
  }

  void CompactSamples(JNIEnv *env);

  // Not copyable or movable.
  HeapEventStorage(const HeapEventStorage&) = delete;
  HeapEventStorage& operator=(const HeapEventStorage&) = delete;

 private:
  // Helper for creating a google::javaprofiler::ProfileStackTrace array
  // to pass to ProfileProtoBuilder.
  class StackTraceArrayBuilder {
   public:
    StackTraceArrayBuilder(std::size_t objects_size);

    void AddTrace(HeapObjectTrace &object);

    google::javaprofiler::ProfileStackTrace* GetStackTraceData() const {
      return stack_trace_data_.get();
    }

   private:
    int curr_trace_ = 0;
    std::size_t objects_size_;
    std::unique_ptr<google::javaprofiler::ProfileStackTrace[]>
        stack_trace_data_;
    std::unique_ptr<JVMPI_CallTrace[]> call_trace_data_;
  };


  static std::unique_ptr<perftools::profiles::Profile> ConvertToProto(
      ProfileProtoBuilder *builder, std::vector<HeapObjectTrace> &objects);

  std::unique_ptr<perftools::profiles::Profile> GetProfiles(
      JNIEnv* env, int sampling_interval, bool force_gc, bool get_live);

  // Add object to the garbage list: it uses a queue with a max size of
  // max_garbage_size, provided via the constructor.
  // obj will be std::move'd to the garbage_list.
  void AddToGarbage(HeapObjectTrace &&obj);

  // Moves live objects from objects to still_live_objects; live elements from
  // the objects vector are replaced with nullptr via std::move.
  void MoveLiveObjects(
      JNIEnv *env, std::vector<HeapObjectTrace> *objects,
      std::vector<HeapObjectTrace> *still_live_objects);

  int64_t ProfileSize(const std::vector<HeapObjectTrace> &objects) const;

  std::vector<HeapObjectTrace> newly_allocated_objects_;
  std::vector<HeapObjectTrace> live_objects_;

  int64_t peak_profile_size_;
  std::vector<HeapObjectTrace> peak_objects_;

  // Though a queue really would be nice, we need a way to iterate when
  // requested.
  int max_garbage_size_;
  int cur_garbage_pos_;
  std::vector<HeapObjectTrace> garbage_objects_;

  std::mutex storage_lock_;
  jvmtiEnv *jvmti_;
  ProfileFrameCache *cache_;
  const GcCallback gc_callback_;
};

// Due to the JVMTI callback, everything here is static.
class HeapMonitor {
 public:
  static bool Enable(jvmtiEnv *jvmti, JNIEnv* jni, int sampling_interval,
                     bool use_jvm_trace);
  static void Disable();

  static bool Enabled() { return heap_monitor_ != nullptr; }

  // Returns a perftools::profiles::Profile with the objects provided by the
  // HeapEventStorage.
  static std::unique_ptr<perftools::profiles::Profile> GetHeapProfiles(
      JNIEnv* env, bool force_gc);

  // Returns a perftools::profiles::Profile with the GC'd objects provided by
  // the HeapEventStorage.
  static std::unique_ptr<perftools::profiles::Profile> GetGarbageHeapProfiles(
      JNIEnv* env, bool force_gc);

  // Return the largest profile recorded so far.
  static std::unique_ptr<perftools::profiles::Profile> GetPeakHeapProfiles(
    JNIEnv* env, bool force_gc);

  static void AddSample(JNIEnv *jni_env, jthread thread, jobject object,
                        jclass object_klass, jlong size, jbyte *name,
                        jint name_len, jlong thread_id);

  static void InvokeAllocationInstrumentationFunctions(jlong thread_id,
                                                       jbyte *name,
                                                       int name_length,
                                                       int size,
                                                       jlong gcontext);

  static void AddAllocationInstrumentation(
    AllocationInstrumentationFunction fn);

  static bool HasAllocationInstrumentation();

  static void InvokeGarbageInstrumentationFunctions(jlong thread_id,
                                                    jbyte *name,
                                                    int name_length,
                                                    int size,
                                                    jlong gcontext);

  static void AddGarbageInstrumentation(GarbageInstrumentationFunction fn);

  static bool HasGarbageInstrumentation();

  static void AddCallback(jvmtiEventCallbacks *callbacks);

  static void NotifyGCWaitingThread() {
    GetInstance()->NotifyGCWaitingThreadInternal(GcEvent::GC_FINISHED);
  }

  // Not copyable or movable.
  HeapMonitor(const HeapMonitor &) = delete;
  HeapMonitor &operator=(const HeapMonitor &) = delete;

 private:
  static const HeapEventStorage::GcCallback gc_callback_;

  HeapMonitor() : storage_(jvmti_.load(), GetFrameCache(), 200, gc_callback_) {
  }

  static std::atomic<HeapMonitor *> heap_monitor_;

  // We initialize heap_monitor_ in Enable, so ensure Enable is called before
  // any call to this method.
  static HeapMonitor *GetInstance() {
    assert(heap_monitor_ != nullptr);
    return heap_monitor_;
  }

  ProfileFrameCache *GetFrameCache() {
    ProfileFrameCache *cache = nullptr;
    return cache;
  }

  static bool Supported(jvmtiEnv* jvmti);

  enum class GcEvent {
    NO_EVENT,
    GC_FINISHED,
    SHUTDOWN
  };

  bool CreateGCWaitingThread(jvmtiEnv* jvmti, JNIEnv* jni);
  void ShutdownGCWaitingThread();
  static void GCWaitingThread(jvmtiEnv *jvmti_env, JNIEnv *jni_env, void *arg);
  void GCWaitingThreadRun(JNIEnv* jni_env);
  GcEvent WaitForGC();
  void NotifyGCWaitingThreadInternal(GcEvent event);
  void WaitForShutdown();

  void CompactData(JNIEnv* jni_env);

  static std::vector<AllocationInstrumentationFunction> alloc_inst_functions_;
  static std::vector<GarbageInstrumentationFunction> gc_inst_functions_;
  static std::unique_ptr<perftools::profiles::Profile> EmptyHeapProfile(
      JNIEnv *jni_env);

  static std::atomic<jvmtiEnv *> jvmti_;
  static std::atomic<int> sampling_interval_;
  static std::atomic<bool> use_jvm_trace_;

  std::list<GcEvent> gc_notify_events_;
  bool gc_thread_shutdown = false;
  std::condition_variable gc_waiting_cv_;
  std::mutex gc_waiting_mutex_;
  HeapEventStorage storage_;
};

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_HEAP_SAMPLER_H_
