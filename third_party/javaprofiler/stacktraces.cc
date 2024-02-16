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

#include "third_party/javaprofiler/stacktraces.h"

namespace google {
namespace javaprofiler {

ASGCTType Asgct::asgct_;

std::mutex *AttributeTable::mutex_;
std::unordered_map<std::string, int> *AttributeTable::string_map_;
std::vector<std::string> *AttributeTable::strings_;

bool AsyncSafeTraceMultiset::Add(int attr, JVMPI_CallTrace *trace) {
  uint64_t hash_val = CalculateHash(attr, trace->num_frames, &trace->frames[0]);

  for (int64_t i = 0; i < MaxEntries(); i++) {
    int64_t idx = (i + hash_val) % MaxEntries();
    auto &entry = traces_[idx];
    int64_t count_zero = 0;
    entry.active_updates.fetch_add(1, std::memory_order_acquire);
    int64_t count = entry.count.load(std::memory_order_acquire);
    switch (count) {
      case 0:
        if (entry.count.compare_exchange_weak(count_zero, kTraceCountLocked,
                                              std::memory_order_relaxed)) {
          // This entry is reserved, there is no danger of interacting
          // with Extract, so decrement active_updates early.
          entry.active_updates.fetch_sub(1, std::memory_order_release);
          // memcpy is not async safe
          JVMPI_CallFrame *fb = entry.frame_buffer;
          int num_frames = trace->num_frames;
          for (int frame_num = 0; frame_num < num_frames; ++frame_num) {
            fb[frame_num].lineno = trace->frames[frame_num].lineno;
            fb[frame_num].method_id = trace->frames[frame_num].method_id;
          }
          entry.trace.frames = fb;
          entry.trace.num_frames = num_frames;
          entry.attr = attr;
          entry.count.store(static_cast<int64_t>(1), std::memory_order_release);
          return true;
        }
        break;
      case kTraceCountLocked:
        // This entry is being updated by another thread. Move on.
        // Worst case we may end with multiple entries with the same trace.
        break;
      default:
        if (attr == entry.attr && trace->num_frames == entry.trace.num_frames &&
            Equal(trace->num_frames, entry.trace.frames, trace->frames)) {
          // Bump using a compare-swap instead of fetch_add to ensure
          // it hasn't been locked by a thread doing Extract().
          // Reload count in case it was updated while we were
          // examining the trace.
          count = entry.count.load(std::memory_order_relaxed);
          if (count != kTraceCountLocked &&
              entry.count.compare_exchange_weak(count, count + 1,
                                                std::memory_order_relaxed)) {
            entry.active_updates.fetch_sub(1, std::memory_order_release);
            return true;
          }
        }
    }
    // Did nothing, but we still need storage ordering between this
    // store and preceding loads.
    entry.active_updates.fetch_sub(1, std::memory_order_release);
  }
  return false;
}

int AsyncSafeTraceMultiset::Extract(int location, int64_t *attr, int max_frames,
                                    JVMPI_CallFrame *frames, int64_t *count) {
  if (location < 0 || location >= MaxEntries()) {
    return 0;
  }
  auto &entry = traces_[location];
  int64_t c = entry.count.load(std::memory_order_acquire);
  if (c <= 0) {
    // Unused or in process of being updated, skip for now.
    return 0;
  }
  int num_frames = entry.trace.num_frames;
  if (num_frames > max_frames) {
    num_frames = max_frames;
  }

  c = entry.count.exchange(kTraceCountLocked, std::memory_order_acquire);

  *attr = entry.attr;
  for (int i = 0; i < num_frames; ++i) {
    frames[i].lineno = entry.trace.frames[i].lineno;
    frames[i].method_id = entry.trace.frames[i].method_id;
  }

  while (entry.active_updates.load(std::memory_order_acquire) != 0) {
    // spin
    // TODO: Introduce a limit to detect and break
    // deadlock
  }

  entry.count.store(0, std::memory_order_release);
  *count = c;
  return num_frames;
}

void TraceMultiset::Add(int64_t attr, int num_frames, JVMPI_CallFrame *frames,
                        int64_t count) {
  CallTrace t;
  t.attr = attr;
  t.frames = std::vector<JVMPI_CallFrame>(frames, frames + num_frames);

  auto entry = traces_.find(t);
  if (entry != traces_.end()) {
    entry->second += count;
    return;
  }
  traces_.emplace(std::move(t), count);
}

int HarvestSamples(AsyncSafeTraceMultiset *from, TraceMultiset *to) {
  int trace_count = 0;
  int64_t num_traces = from->MaxEntries();
  for (int64_t i = 0; i < num_traces; i++) {
    JVMPI_CallFrame frame[kMaxFramesToCapture];
    int64_t attr, count;

    int num_frames =
        from->Extract(i, &attr, kMaxFramesToCapture, &frame[0], &count);
    if (num_frames > 0 && count > 0) {
      ++trace_count;
      to->Add(attr, num_frames, &frame[0], count);
    }
  }
  return trace_count;
}

uint64_t CalculateHash(int64_t attr, int num_frames,
                       const JVMPI_CallFrame *frame) {
  // Make hash-value
  uint64_t h = attr;
  h += h << 10;
  h ^= h >> 6;
  for (int i = 0; i < num_frames; i++) {
    h += reinterpret_cast<uintptr_t>(frame[i].method_id);
    h += h << 10;
    h ^= h >> 6;
    h += static_cast<uintptr_t>(frame[i].lineno);
    h += h << 10;
    h ^= h >> 6;
  }
  h += h << 3;
  h ^= h >> 11;
  return h;
}

bool Equal(int num_frames, const JVMPI_CallFrame *f1,
           const JVMPI_CallFrame *f2) {
  // Compare individual members to avoid differences in padding.
  for (int i = 0; i < num_frames; i++) {
    if (f1[i].method_id != f2[i].method_id || f1[i].lineno != f2[i].lineno) {
      return false;
    }
  }
  return true;
}

}  // namespace javaprofiler
}  // namespace google
