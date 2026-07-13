#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include "util_bit.hpp"
#include "util_svector.hpp"

namespace dxmt {

namespace ResourceAccess {
constexpr int Read = 1 << 0;
constexpr int Write = 1 << 1;
constexpr int ReadWrite = Read | Write;
constexpr int UAV = 1 << 2;
constexpr int All = ReadWrite | UAV;
}; // namespace ResourceAccess

constexpr auto kLog2Lane = 6ull;
constexpr auto kLane = 1 << kLog2Lane;
constexpr auto kLaneMask = kLane - 1;
constexpr auto kAllLaneMask = ~0ull >> (64 /* uint64_t */ - kLane);
constexpr auto kParity = 4; // can also use 3, although power of 2 is nice
constexpr auto kParityLane = kParity * kLane;

static_assert(kLog2Lane <= 6);
static_assert(kLane > 1);

using LaneStorage = uint64_t;
using EncoderId = uint64_t;

constexpr auto
PARITY(EncoderId id) {
  return (id >> kLog2Lane) % kParity;
}

constexpr auto
LANE(EncoderId id) {
  return id & kLaneMask;
}

class FenceSet {
public:
  FenceSet() = default;

  FenceSet(EncoderId id) {
    set(id);
  }

  bool
  set(EncoderId id) {
    for (auto &entry : entries_) {
      if (entry == id)
        return false;
      if ((entry % kParityLane) == (id % kParityLane)) {
        entry = std::max(entry, id);
        return false;
      }
    }
    entries_.push_back(id);
    return true;
  }

  void
  unset(EncoderId id) {
    for (size_t i = 0; i < entries_.size(); i++) {
      if (entries_[i] == id) {
        entries_.erase(i);
        return;
      }
    }
  }

  bool
  test(EncoderId id) const {
    return std::find(entries_.begin(), entries_.end(), id) != entries_.end();
  }

  bool
  testAndSet(EncoderId id) {
    return !set(id);
  }

  bool
  add(EncoderId id) {
    return set(id);
  }

  bool
  isLastAccess(EncoderId id) const {
    return test(id);
  }

  template <typename Fn>
  size_t
  enumerate(EncoderId id_before, Fn &&fn) {
    size_t count = 0;
    for (auto id : entries_) {
      if (id >= id_before)
        continue;
      fn(id);
      count++;
    }
    return count;
  }

  bool
  intersectedWith(const FenceSet &set) const {
    for (auto id : entries_) {
      if (set.test(id))
        return true;
    }
    return false;
  }

  bool
  contains(const FenceSet &set) const {
    for (auto id : set.entries_) {
      if (!test(id))
        return false;
    }
    return true;
  }

  FenceSet &
  merge(const FenceSet &set) {
    for (auto id : set.entries_)
      this->set(id);
    return *this;
  }

  FenceSet
  unionOf(const FenceSet &set) const {
    FenceSet ret(*this);
    ret.merge(set);
    return ret;
  }

  FenceSet &
  subtract(const FenceSet &set) {
    for (auto id : set.entries_)
      unset(id);
    return *this;
  }

  LaneStorage
  laneMask() const {
    LaneStorage ret = 0;
    for (auto id : entries_)
      ret |= 1ull << LANE(id);
    return ret;
  }

  LaneStorage
  storage(int parity) const {
    LaneStorage ret = 0;
    for (auto id : entries_) {
      if (PARITY(id) == static_cast<uint64_t>(parity))
        ret |= 1ull << LANE(id);
    }
    return ret;
  }

  uint32_t
  count() const {
    return static_cast<uint32_t>(entries_.size());
  }

  bool
  empty() const {
    return entries_.empty();
  }

  void
  clear() {
    entries_.clear();
  }

  template <typename Fn>
  void
  forEach(Fn &&fn) {
    for (auto id : entries_)
      fn(id);
  }

  template <typename Fn, typename FnPrior>
  void
  forEach(const FenceSet &prior, FnPrior &&fnPrior, Fn &&fn) {
    for (auto id : prior.entries_)
      fnPrior(id);
    for (auto id : entries_) {
      if (!prior.test(id))
        fn(id);
    }
  }

private:
  small_vector<EncoderId, 4> entries_;
};

template <size_t Sz = kLane, size_t Forward = 1> class TrackingSet {
public:
  TrackingSet() {
    cursor = 0;
    clear();
  };

  bool
  add(EncoderId id) {
    assert(storage_[cursor] <= id);
    if (storage_[cursor] == id)
      return false;
    {
      cursor++;
      cursor = cursor % Sz;
    }
    storage_[cursor] = id;
    return true;
  };

  bool
  isLastAccess(EncoderId id) {
    return storage_[cursor] == id;
  }

  void
  clear() {
    storage_[cursor] = 0;
  };

  template <typename Fn>
  size_t
  enumerate(EncoderId id_before, Fn &&fn) {
    size_t count = 0;
    assert(id_before > Sz);
    for (size_t i = 0; i < Sz; i++) {
      auto c = storage_[(cursor + Sz - i) % Sz];
      if (c >= id_before) {
        assert(c - id_before <= Forward);
        continue;
      }
      if (c > (id_before - Sz)) {
        fn(c);
        count++;
        continue;
      }
      break;
    }
    return count;
  }

private:
  EncoderId storage_[Sz + Forward];
  uint32_t cursor;
};

constexpr auto kBarrierTypeRW = 1 << 0;
constexpr auto kBarrierTypeWaW = 1 << 1;

struct EncoderBarrierState {
  uint64_t barrierSet                       : 2 = 0;
  uint64_t barrierPreRasterSet              : 2 = 0;
  uint64_t barrierFragmentAfterPreRasterSet : 2 = 0;
  uint64_t barrierPreRasterAfterFragmentSet : 2 = 0;
  uint64_t reserved                         : 56;
};

class GenericAccessTracker {
public:
  void accessShared(EncoderId id, FenceSet &wait_fences, EncoderBarrierState &barrier_state);
  void accessExclusive(EncoderId id, FenceSet &wait_fences, EncoderBarrierState &barrier_state, bool uav);

  void accessSharedPreRaster(EncoderId id, FenceSet &wait_fences, EncoderBarrierState &barrier_state);
  void accessExclusivePreRaster(EncoderId id, FenceSet &wait_fences, EncoderBarrierState &barrier_state, bool uav);
  void accessSharedFragment(EncoderId id, FenceSet &wait_fences, EncoderBarrierState &barrier_state);
  void accessExclusiveFragment(EncoderId id, FenceSet &wait_fences, EncoderBarrierState &barrier_state, bool uav);

private:
  /**
   * Previous shared access
   */
  FenceSet shared_;
  /**
   * Last exclusive access
   */
  EncoderId exclusive_{};
  uint64_t isShared               : 1 = 0;
  uint64_t isSharedPreRaster      : 1 = 0;
  uint64_t lastWriteFromPreRaster : 1 = 0;
};

class FenceLocalityCheck {
public:
  FenceSet collectAndSimplifyWaits(
      FenceSet strong_fences,
      EncoderId id,
      bool implicit_pre_raster_wait = false,
      const char *trace_scope = nullptr);

private:
  std::array<FenceSet, kParityLane> summary_;
  std::array<EncoderId, kParityLane> summary_generation_ = {};
};

} // namespace dxmt
