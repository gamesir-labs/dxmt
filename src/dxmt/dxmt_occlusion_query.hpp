#pragma once

#include "Metal.hpp"
#include "rc/util_rc_ptr.hpp"
#include "wsi_platform.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace dxmt {
class VisibilityResultOffsetBumpState {
public:
  void
  beginEncoder() {
    assert(!within_encoder);
    assert(!current_data_is_dirty);
    assert(!~previous_offset);
    within_encoder = true;
  }

  bool
  tryGetNextWriteOffset(bool has_active_occlusion_queries, uint64_t &offset) {
    assert(within_encoder);
    offset = getNextWriteOffset(has_active_occlusion_queries);
    if (offset == previous_offset) {
      return false;
    }
    previous_offset = offset;
    return true;
  }

  uint64_t
  getNextReadOffset() {
    if (within_encoder) {
      if (current_data_is_dirty) {
        current_data_is_dirty = false;
        return ++next_offset;
      }
    }
    assert(!current_data_is_dirty);
    return next_offset;
  }

  void
  endEncoder() {
    assert(within_encoder);
    within_encoder = false;
    previous_offset = ~0uLL;
    if (current_data_is_dirty) {
      next_offset++;
      current_data_is_dirty = false;
    }
  }

  uint64_t
  reset() {
    assert(!within_encoder && "encoder still active");
    assert(!current_data_is_dirty && "encoder still active");
    auto ret = next_offset;
    next_offset = 0;
    return ret;
  }

private:
  uint64_t
  getNextWriteOffset(bool has_active_occlusion_queries) {
    if (has_active_occlusion_queries) {
      current_data_is_dirty = true;
      return next_offset;
    }
    return ~0uLL;
  };

  bool within_encoder = false;
  bool current_data_is_dirty = false;
  uint64_t previous_offset = ~0uLL;
  uint64_t next_offset = 0;
};

class VisibilityResultQuery {
public:
  void
  incRef() {
    refcount_.fetch_add(1u, std::memory_order_acquire);
  }
  void
  decRef() {
    if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
      delete this;
  }

  void
  begin(uint64_t seqId, unsigned offset) {
    accumulated_value_ = 0;
    seq_id_begin = seqId;
    occlusion_counter_begin = offset;
    seq_id_end = ~0uLL;
    occlusion_counter_end = ~0uLL;
  }

  void
  end(uint64_t seqId, unsigned offset) {
    seq_id_end = seqId;
    occlusion_counter_end = offset;
    if (seq_id_begin == seq_id_end && occlusion_counter_begin == occlusion_counter_end) {
      seq_id_issued = seq_id_end;
    }
  }

  uint64_t queryEndAt() {
    return seq_id_end;
  };

  void
  issue(uint64_t seqId, uint64_t const *readbackBuffer, unsigned numResults) {
    assert(seqId >= seq_id_begin);
    assert(seqId <= seq_id_end);
    uint64_t const *start = seqId == seq_id_begin ? readbackBuffer + occlusion_counter_begin : readbackBuffer;
    uint64_t const *end = seqId == seq_id_end ? readbackBuffer + occlusion_counter_end : readbackBuffer + numResults;
    assert(start <= end);
    while (start != end) {
      accumulated_value_ += *start;
      start++;
    }
    seq_id_issued = seqId;
  }

  bool
  getValue(uint64_t *value) {
    if (seq_id_end <= seq_id_issued) {
      *value = accumulated_value_;
      return true;
    }
    return false;
  }

  void
  reset() {
    accumulated_value_ = 0;
    seq_id_begin = ~0uLL;
    occlusion_counter_begin = ~0uLL;
    seq_id_end = ~0uLL;
    occlusion_counter_end = ~0uLL;
    seq_id_issued = 0;
  };

private:
  uint64_t accumulated_value_ = 0;
  uint64_t seq_id_begin = ~0uLL;
  uint64_t occlusion_counter_begin = ~0uLL;
  uint64_t seq_id_end = ~0uLL;
  uint64_t occlusion_counter_end = ~0uLL;
  uint64_t seq_id_issued = 0;
  std::atomic<uint32_t> refcount_ = {0u};
};

class VisibilityResultReadback {
public:
  VisibilityResultReadback(
      WMT::Device device, uint64_t seq_id, uint64_t num_results, std::vector<Rc<VisibilityResultQuery>> &queries
  ) :
      seq_id(seq_id),
      num_results(num_results),
      queries(queries) {
        visibility_result_heap_info.options = WMTResourceHazardTrackingModeUntracked;
        visibility_result_heap_info.memory.set(nullptr);
#ifdef __i386__
        visibility_result_heap_info.memory.set(wsi::aligned_malloc(num_results * sizeof(uint64_t), DXMT_PAGE_SIZE));
#endif
        visibility_result_heap_info.length = num_results * sizeof(uint64_t);
        visibility_result_heap = device.newBuffer(visibility_result_heap_info);
      }
  ~VisibilityResultReadback() {
    for (auto query : queries) {
      query->issue(seq_id, (uint64_t *)visibility_result_heap_info.memory.get(), num_results);
    }
#ifdef __i386__
    wsi::aligned_free(visibility_result_heap_info.memory.get());
#endif
  }

  VisibilityResultReadback(const VisibilityResultReadback &) = delete;
  VisibilityResultReadback(VisibilityResultReadback &&) = delete;

  uint64_t seq_id;
  uint64_t num_results;
  std::vector<Rc<VisibilityResultQuery>> queries;
  WMTBufferInfo visibility_result_heap_info;
  WMT::Reference<WMT::Buffer> visibility_result_heap;
};

/**
 * TODO: rename the whole file to dxmt_query.hpp
 */

class TimestampQuery {
public:
  void
  incRef() {
    refcount_.fetch_add(1u, std::memory_order_acquire);
  }
  void
  decRef() {
    if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
      delete this;
  }

  bool
  getValue(uint64_t *value) {
    if (cached_value_ == ~0ull)
      return false;

    *value = cached_value_;
    return true;
  }

  void
  issue(uint64_t sampled_data) {
    cached_value_ = sampled_data;
  }

  uint64_t
  sampleIndex() const {
    return sample_index_;
  }

  uint64_t
  sampleSequence() const {
    return sample_sequence_;
  }

  void
  setSampleIndex(uint64_t sample_index) {
    sample_index_ = sample_index;
  }

  void
  setSampleLocation(uint64_t sample_sequence, uint64_t sample_index) {
    sample_sequence_ = sample_sequence;
    sample_index_ = sample_index;
  }

#if DXMT_DX12_METAL4
  void
  setResolveSource(const WMT::Reference<WMT::CounterHeap> &heap,
                   uint64_t heap_entry_size) {
    resolve_heap_ = heap;
    resolve_heap_entry_size_ = heap_entry_size;
  }

  WMT::Reference<WMT::CounterHeap>
  resolveHeap() const {
    return resolve_heap_;
  }

  uint64_t
  resolveHeapEntrySize() const {
    return resolve_heap_entry_size_;
  }
#endif

private:
  uint64_t cached_value_ = ~0ull;
  uint64_t sample_sequence_ = ~0ull;
  uint64_t sample_index_ = ~0ull;
#if DXMT_DX12_METAL4
  WMT::Reference<WMT::CounterHeap> resolve_heap_;
  uint64_t resolve_heap_entry_size_ = 0;
#endif
  std::atomic<uint32_t> refcount_ = {0u};
};

using TimestampQueryList = std::vector<std::pair<Rc<TimestampQuery>, uint64_t>>;

class TimestampReadbackSBuf {
public:
  TimestampReadbackSBuf(
      WMT::Device device, WMT::CommandBuffer cmdbuf, uint64_t num_samples, TimestampQueryList &&queries
  ) :
      queries_(std::move(queries)),
      num_samples_(num_samples) {
#if DXMT_DX12_METAL4
    WMT::Error error = {};
    timestamp_context_ = device.newTimestampContext();
    heap_entry_size_ = device.timestampHeapEntrySize();
    timestamp_heap_ = device.newTimestampCounterHeap(num_samples, &error);
    assert(timestamp_context_);
    assert(timestamp_heap_);
    assert(heap_entry_size_ >= sizeof(WMTMTL4TimestampHeapEntry));
    for (const auto &[query, sample_index] : queries_)
      query->setResolveSource(timestamp_heap_, heap_entry_size_);
#else
    sample_buffer_ = device.newCounterSampleBuffer(num_samples, true);
#endif
  }

  ~TimestampReadbackSBuf() {
    // TODO: small_vector opt
#if DXMT_DX12_METAL4
    std::vector<uint8_t> resolved(num_samples_ * heap_entry_size_);
    timestamp_heap_.resolveCounterRange(0, num_samples_, resolved.data(), resolved.size());
    for (const auto &[query, sample_index] : queries_) {
      auto *entry = reinterpret_cast<const WMTMTL4TimestampHeapEntry *>(resolved.data() + sample_index * heap_entry_size_);
      query->issue(entry->timestamp);
    }
    timestamp_context_.destroy();
#else
    std::vector<uint64_t> results(num_samples_);
    sample_buffer_.resolveCounterRange(0, num_samples_, results.data(), num_samples_ * sizeof(uint64_t));
    for (const auto &[query, sample_index] : queries_) {
      query->issue(results[sample_index]);
    }
#endif
  }

  TimestampReadbackSBuf(const TimestampReadbackSBuf &) = delete;
  TimestampReadbackSBuf(TimestampReadbackSBuf &&) = delete;

#if DXMT_DX12_METAL4
  WMT::CounterHeap counterHeap() {
    return timestamp_heap_;
  };

  uint64_t
  heapEntrySize() const {
    return heap_entry_size_;
  }

  WMT::TimestampContext timestampContext() {
    return timestamp_context_;
  }
#else
  WMT::CounterSampleBuffer
  sampleBuffer() {
    return sample_buffer_;
  };
#endif

private:
  TimestampQueryList queries_;
#if DXMT_DX12_METAL4
  WMT::TimestampContext timestamp_context_;
  WMT::Reference<WMT::CounterHeap> timestamp_heap_;
  uint64_t heap_entry_size_;
#else
  WMT::Reference<WMT::CounterSampleBuffer> sample_buffer_;
#endif
  uint64_t num_samples_;
};

class TimestampReadbackCBuf {
public:
  TimestampReadbackCBuf(
      WMT::Device device, WMT::CommandBuffer cmdbuf, uint64_t num_samples, TimestampQueryList &&queries
  ) :
      queries_(std::move(queries)),
      num_samples_(num_samples),
      cmdbuf_(cmdbuf) {}

  ~TimestampReadbackCBuf() {
    // TODO: small_vector opt
    std::vector<uint64_t> results(num_samples_);
    /**
    There is no implicit relationship between `gpuEndTime` and order of commit, but we still want a later issued query
    to return a timestamp greater or equal to previous ones, so check and use maximum.

    `thread_local` makes sense because this destructor is only called on 1. finishing thread 2. (abnormal) device
    destruction
    */
    thread_local uint64_t latest_ts_on_finish_thread = 0;
    latest_ts_on_finish_thread = std::max(cmdbuf_.gpuEndTime(), latest_ts_on_finish_thread);
    std::fill(results.begin(), results.end(), latest_ts_on_finish_thread);
    for (const auto &[query, sample_index] : queries_) {
      query->issue(results[sample_index]);
    }
  }

  TimestampReadbackCBuf(const TimestampReadbackCBuf &) = delete;
  TimestampReadbackCBuf(TimestampReadbackCBuf &&) = delete;

#if DXMT_DX12_METAL4
  WMT::CounterHeap
  counterHeap() {
    return {};
  };

  uint64_t
  heapEntrySize() const {
    return 0;
  }

  WMT::TimestampContext timestampContext() {
    return {};
  }
#else
  WMT::CounterSampleBuffer
  sampleBuffer() {
    return {};
  };
#endif

private:
  TimestampQueryList queries_;
  uint64_t num_samples_;
  WMT::CommandBuffer cmdbuf_;
};

// Timestamp counters need per-sample values. The command-buffer end-time fallback
// collapses all samples in one command buffer to the same timestamp, which makes
// begin/end deltas always resolve to zero.
using TimestampReadback = TimestampReadbackSBuf;

class TimestampQueryState {
public:
  TimestampQueryState(WMT::Device device) : device_(device) {}

  uint64_t
  addQuery(TimestampQuery *query) {
    auto sample_index = num_samples_;
    addQueryAt(query, sample_index);
    return sample_index;
  }

  uint64_t
  addQueryAt(TimestampQuery *query, uint64_t sample_index) {
    query->setSampleIndex(sample_index);
    queries_.push_back({query, sample_index});
    num_samples_ = std::max(num_samples_, sample_index + 1);
    return sample_index;
  }

  void
  coalesceQuery(TimestampQuery *query) {
    assert(num_samples_);
    query->setSampleIndex(num_samples_ - 1);
    queries_.push_back({query, num_samples_ - 1});
  }

  std::unique_ptr<TimestampReadback>
  flush(WMT::CommandBuffer cmdbuf) {
    if (num_samples_ == 0)
      return {};

    auto ret = std::make_unique<TimestampReadback>(device_, cmdbuf, num_samples_, std::move(queries_));
    num_samples_ = 0;
    queries_ = {};
    return ret;
  }

private:
  uint64_t num_samples_ = 0;
  TimestampQueryList queries_;
  WMT::Device device_;
};

struct QueryReadbacks {
  std::unique_ptr<VisibilityResultReadback> visibility;
  std::unique_ptr<TimestampReadback> timestamp;
  std::vector<std::function<void()>> diagnostics;
};

} // namespace dxmt
