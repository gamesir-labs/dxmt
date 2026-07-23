#pragma once

#include "thread.hpp"
#include <atomic>
#include <cstdint>

namespace dxmt::lifecycle {

inline std::atomic<uint64_t> global_event_sequence{1};
inline std::atomic<uint64_t> global_pair_sequence{1};

inline uint64_t
nextEventSequence() {
  return global_event_sequence.fetch_add(1, std::memory_order_relaxed);
}

inline uint64_t
nextPairSequence() {
  return global_pair_sequence.fetch_add(1, std::memory_order_relaxed);
}

inline uint32_t
threadId() {
  return dxmt::this_thread::get_id();
}

} // namespace dxmt::lifecycle
