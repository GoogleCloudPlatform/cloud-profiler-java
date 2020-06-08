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

// This file contains structures used for profile stacktraces.

#ifndef THIRD_PARTY_JAVAPROFILER_STACKTRACES_H_
#define THIRD_PARTY_JAVAPROFILER_STACKTRACES_H_

#include <atomic>
#include <mutex>  // NOLINT
#include <unordered_map>
#include <vector>

#include "third_party/javaprofiler/native.h"
#include "third_party/javaprofiler/stacktrace_decls.h"

namespace google {
namespace javaprofiler {

// Maximum number of frames to store from the stack traces sampled.
const int kMaxFramesToCapture = 128;

uint64 CalculateHash(int64 attr, int num_frames,
                       const JVMPI_CallFrame *frame);
bool Equal(int num_frames, const JVMPI_CallFrame *f1,
           const JVMPI_CallFrame *f2);

typedef void (*ASGCTType)(JVMPI_CallTrace *, jint, void *);

const int kNumCallTraceErrors = 10;

class Asgct {
 public:
  static void SetAsgct(ASGCTType asgct) { asgct_ = asgct; }

  // AsyncGetCallTrace function, to be dlsym'd.
  static ASGCTType GetAsgct() { return asgct_; }

 private:
  static ASGCTType asgct_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(Asgct);
};

class AttributeTable {
 public:
  static void Init() {
    mutex_ = new (std::mutex);
    string_map_ = new (std::unordered_map<std::string, int>);
    strings_ = new (std::vector<std::string>);
    strings_->push_back("");
  }

  static int RegisterString(const char *value) {
    if (mutex_ == nullptr || value == nullptr || !*value) {
      // Not initialized or empty string.
      return 0;
    }
    std::lock_guard<std::mutex> lock(*mutex_);
    int ret = strings_->size();
    const auto inserted = string_map_->emplace(value, ret);
    if (!inserted.second) {
      // Insertion failed, use existing value.
      return inserted.first->second;
    }
    strings_->push_back(value);
    (*string_map_)[value] = ret;
    return ret;
  }

  static std::vector<std::string> GetStrings() {
    if (mutex_ == nullptr) {
      return std::vector<std::string>();
    }
    std::lock_guard<std::mutex> lock(*mutex_);
    return *strings_;
  }

 private:
  static std::mutex *mutex_;
  static std::unordered_map<std::string, int> *string_map_;
  static std::vector<std::string> *strings_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(AttributeTable);
};

// Multiset of stack traces. There is a maximum number of distinct
// traces that can be held, return by MaxEntries();
//
// The Add() operation is async-safe, but will fail and return false
// if there is no room to store the trace.
//
// The Extract() operation will remove a specific entry, and it can
// run concurrently with multiple Add() operations. Multiple
// invocations of Extract() cannot be executed concurrently.
//
// The synchronization is implemented by using a sentinel count value
// to reserve entries. Add() will reserve the first available entry,
// save the stack frame, and then release the entry for other calls to
// Add() or Extract(). Extract() will reserve the entry, wait until no
// additions are in progress, and then release the entry to be reused
// by a subsequent call to Add(). It is important for Extract() to
// wait until no additions are in progress to avoid releasing the
// entry while another thread is inspecting it.
class AsyncSafeTraceMultiset {
 public:
  AsyncSafeTraceMultiset() { Reset(); }

  void Reset() {
    memset(traces_, 0, sizeof(traces_));
  }

  // Add a trace to the set. If it is already present, increment its
  // count. This operation is thread safe and async safe.
  bool Add(int attr, JVMPI_CallTrace *trace);

  // Extract a trace from the array. frames must point to at least
  // max_frames contiguous frames. It will return the number of frames
  // written starting at frames[0], up to max_frames. returns 0 if
  // there is no valid trace at this location.  This operation is
  // thread safe with respect to Add() but only a single call to
  // Extract can be done at a time.
  int Extract(int location, int64 *attr, int max_frames,
              JVMPI_CallFrame *frames, int64 *count);

  int64 MaxEntries() const { return kMaxStackTraces; }

 private:
  struct TraceData {
    // attr is an integer attribute for the stack trace. On encode
    // this will represent a sample label.
    int attr;
    // trace is a triple containing the JNIEnv and the individual call frames.
    // The frames are stored in frame_buffer.
    JVMPI_CallTrace trace;
    // frame_buffer is the storage for stack frames.
    JVMPI_CallFrame frame_buffer[kMaxFramesToCapture];
    // Number of times a trace has been encountered.
    // 0 indicates that the trace is unused
    // <0 values are reserved, used for concurrency control.
    std::atomic<int64> count;
    // Number of active attempts to increase the counter on the trace.
    std::atomic<int> active_updates;
  };

  // TODO: Re-evaluate MaxStackTraces, to minimize storage
  // consumption while maintaining good performance and avoiding
  // overflow.
  static const int kMaxStackTraces = 2048;

  // Sentinel to use as trace count while the frames are being updated.
  static const int64 kTraceCountLocked = -1;

  TraceData traces_[kMaxStackTraces];
  DISALLOW_COPY_AND_ASSIGN(AsyncSafeTraceMultiset);
};

// TraceMultiset implements a growable multi-set of traces. It is not
// thread or async safe. Is it intended to be used to aggregate traces
// collected atomically from AsyncSafeTraceMultiset, which implements
// async and thread safe add/extract methods, but has fixed maximum
// size.
class TraceMultiset {
 private:
  typedef struct {
    std::vector<JVMPI_CallFrame> frames;
    int64 attr;
  } CallTrace;

  struct CallTraceHash {
    std::size_t operator()(const CallTrace &trace) const {
      return CalculateHash(trace.attr, trace.frames.size(),
                           trace.frames.data());
    }
  };

  struct CallTraceEqual {
    bool operator()(const CallTrace &t1, const CallTrace &t2) const {
      if (t1.attr != t2.attr) {
        return false;
      }
      if (t1.frames.size() != t2.frames.size()) {
        return false;
      }
      return Equal(t1.frames.size(), t1.frames.data(), t2.frames.data());
    }
  };

  typedef std::unordered_map<CallTrace, uint64, CallTraceHash, CallTraceEqual>
      CountMap;

 public:
  TraceMultiset() {}

  // Add a trace to the array. If it is already in the array,
  // increment its count.
  void Add(int64 attr, int num_frames, JVMPI_CallFrame *frames,
           int64 count);

  typedef CountMap::iterator iterator;
  typedef CountMap::const_iterator const_iterator;

  iterator begin() { return traces_.begin(); }
  iterator end() { return traces_.end(); }

  const_iterator begin() const { return const_iterator(traces_.begin()); }
  const_iterator end() const { return const_iterator(traces_.end()); }

  iterator erase(iterator it) { return traces_.erase(it); }

  void Clear() { traces_.clear(); }

 private:
  CountMap traces_;
  DISALLOW_COPY_AND_ASSIGN(TraceMultiset);
};

// HarvestSamples extracts traces from an asyncsafe trace multiset
// and copies them into a trace multiset. It returns the number of samples
// that were copied. This is thread-safe with respect to other threads adding
// samples into the asyncsafe set.
int HarvestSamples(AsyncSafeTraceMultiset *from, TraceMultiset *to);

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_STACKTRACES_H_
