#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <type_traits>
#include <utility>

namespace dxmt {

struct DescriptorSlotVersion {
  uint64_t epoch = 0;
  uint64_t sequence = 0;

  constexpr explicit operator bool() const noexcept {
    return epoch != 0 || sequence != 0;
  }
  constexpr bool operator==(const DescriptorSlotVersion &) const = default;
};

struct DescriptorContentRevision {
  uint64_t epoch = 0;
  uint64_t sequence = 0;

  constexpr explicit operator bool() const noexcept {
    return epoch != 0 || sequence != 0;
  }
  constexpr bool operator==(const DescriptorContentRevision &) const = default;
};

static_assert(sizeof(DescriptorSlotVersion) == 16);
static_assert(sizeof(DescriptorContentRevision) == 16);
static_assert(std::is_trivially_copyable_v<DescriptorSlotVersion>);
static_assert(std::is_trivially_copyable_v<DescriptorContentRevision>);

namespace detail {

struct DescriptorRevisionClockNoopHooks {
  constexpr void OnLoadBlockedByRollover() const noexcept {}
  constexpr void OnRolloverClaimed() const noexcept {}
};

} // namespace detail

/**
 * Process-wide descriptor publication clock.
 *
 * Normal publication takes only the three atomics below. A mutex is acquired
 * solely when the 64-bit sequence rolls into a new epoch. The transition
 * counter is a seqlock: odd means rollover is in progress, and a reader accepts
 * a pair only when the same even transition value surrounds both qword loads.
 */
template <typename Hooks = detail::DescriptorRevisionClockNoopHooks>
class BasicDescriptorRevisionClock {
public:
  constexpr static uint64_t kMax = std::numeric_limits<uint64_t>::max();

  BasicDescriptorRevisionClock() noexcept = default;
  explicit BasicDescriptorRevisionClock(
      DescriptorContentRevision initial) noexcept
      : epoch_(initial.epoch), sequence_(initial.sequence) {}
  BasicDescriptorRevisionClock(DescriptorContentRevision initial,
                               Hooks hooks) noexcept
      : epoch_(initial.epoch), sequence_(initial.sequence),
        hooks_(std::move(hooks)) {}

  BasicDescriptorRevisionClock(const BasicDescriptorRevisionClock &) = delete;
  BasicDescriptorRevisionClock &
  operator=(const BasicDescriptorRevisionClock &) = delete;

  DescriptorContentRevision Load() const noexcept {
    for (;;) {
      const uint64_t before = transition_.load(std::memory_order_acquire);
      if (before & 1u) {
        hooks_.OnLoadBlockedByRollover();
        continue;
      }
      const uint64_t epoch = epoch_.load(std::memory_order_acquire);
      const uint64_t sequence = sequence_.load(std::memory_order_acquire);
      const uint64_t after = transition_.load(std::memory_order_acquire);
      if (before == after && !(after & 1u))
        return {epoch, sequence};
    }
  }

  bool TryBump(DescriptorContentRevision &revision) noexcept {
    for (;;) {
      const uint64_t transition =
          transition_.load(std::memory_order_acquire);
      if (transition & 1u)
        continue;

      const uint64_t epoch = epoch_.load(std::memory_order_acquire);
      uint64_t sequence = sequence_.load(std::memory_order_relaxed);
      if (sequence == kMax)
        return TryRollover(revision);

      if (!sequence_.compare_exchange_weak(
              sequence, sequence + 1, std::memory_order_acq_rel,
              std::memory_order_relaxed))
        continue;

      // A rollover may have claimed the clock between our first transition
      // load and the sequence CAS. Such a CAS is intentionally discarded; the
      // rollover owns publication and this caller retries in the new epoch.
      if (transition_.load(std::memory_order_acquire) != transition ||
          epoch_.load(std::memory_order_acquire) != epoch)
        continue;

      revision = {epoch, sequence + 1};
      return true;
    }
  }

  DescriptorContentRevision Bump() noexcept {
    DescriptorContentRevision revision = {};
    if (!TryBump(revision))
      std::abort();
    return revision;
  }

private:
  bool TryRollover(DescriptorContentRevision &revision) noexcept {
    std::unique_lock lock(rollover_mutex_);

    for (;;) {
      uint64_t transition = transition_.load(std::memory_order_acquire);
      if (transition & 1u)
        continue;

      // Another thread may have completed rollover while this caller waited.
      if (sequence_.load(std::memory_order_acquire) != kMax) {
        lock.unlock();
        return TryBump(revision);
      }

      if (!transition_.compare_exchange_weak(
              transition, transition + 1, std::memory_order_acq_rel,
              std::memory_order_acquire))
        continue;

      // Compile-time hooks are a no-op in production. Unit tests can pause an
      // individual clock here to prove that Load rejects an odd seqlock state
      // without introducing a process-wide hook or a production wait path.
      hooks_.OnRolloverClaimed();

      const uint64_t epoch = epoch_.load(std::memory_order_relaxed);
      if (epoch == kMax) {
        transition_.store(transition + 2, std::memory_order_release);
        return false;
      }

      const uint64_t next_epoch = epoch + 1;
      epoch_.store(next_epoch, std::memory_order_relaxed);
      sequence_.store(1, std::memory_order_release);
      transition_.store(transition + 2, std::memory_order_release);
      revision = {next_epoch, 1};
      return true;
    }
  }

  std::atomic<uint64_t> transition_{0};
  std::atomic<uint64_t> epoch_{1};
  std::atomic<uint64_t> sequence_{1};
  std::mutex rollover_mutex_;
  [[no_unique_address]] Hooks hooks_{};
};

using DescriptorRevisionClock = BasicDescriptorRevisionClock<>;

} // namespace dxmt
