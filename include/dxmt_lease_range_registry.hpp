#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>

namespace dxmt {

/**
 * Address-shaped handle registry with lifetime-safe lease acquisition.
 *
 * The acquire callback runs while the registry shared lock is held. This is
 * the linearization point that lets a caller retain the range owner before a
 * concurrent unregister operation can remove the range and release the
 * registry's owner reference.
 */
template <typename Record, typename Owner> class LeaseRangeRegistry {
public:
  bool Register(Record *records, std::size_t count, Owner *owner) {
    if (!records || !count || !owner)
      return false;

    const uintptr_t begin = reinterpret_cast<uintptr_t>(records);
    if (count > UINTPTR_MAX / sizeof(Record))
      return false;
    const uintptr_t size = count * sizeof(Record);
    if (size > UINTPTR_MAX - begin)
      return false;
    const uintptr_t end = begin + size;

    std::unique_lock lock(mutex_);
    auto next = ranges_.lower_bound(begin);
    if ((next != ranges_.end() && next->second.begin < end) ||
        (next != ranges_.begin() && std::prev(next)->second.end > begin))
      return false;
    ranges_.emplace(begin, Range{begin, end, records, count, owner});
    return true;
  }

  bool Unregister(Record *records, Owner *owner) {
    if (!records || !owner)
      return false;
    std::unique_lock lock(mutex_);
    return UnregisterLocked(records, owner);
  }

  /**
   * Non-blocking unregister used when a caller must not wait behind an active
   * lookup lease acquisition. A false result means either lock contention or
   * that the owner-qualified range was not registered.
   */
  bool TryUnregister(Record *records, Owner *owner) {
    if (!records || !owner)
      return false;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
      return false;
    return UnregisterLocked(records, owner);
  }

  template <typename Validator, typename Acquire>
  auto Lookup(uintptr_t address, Validator &&validator,
              Acquire &&acquire) const
      -> std::invoke_result_t<Acquire, Record *, Owner *> {
    using Lease = std::invoke_result_t<Acquire, Record *, Owner *>;
    static_assert(std::is_default_constructible_v<Lease>);

    if (!address)
      return Lease{};

    std::shared_lock lock(mutex_);
    auto next = ranges_.upper_bound(address);
    if (next == ranges_.begin())
      return Lease{};
    const auto &range = std::prev(next)->second;
    if (address < range.begin || address >= range.end)
      return Lease{};
    const uintptr_t byte_offset = address - range.begin;
    if (byte_offset % sizeof(Record))
      return Lease{};
    const std::size_t index = byte_offset / sizeof(Record);
    if (index >= range.count)
      return Lease{};

    auto *record = range.records + index;
    if (!std::invoke(std::forward<Validator>(validator), record) ||
        !range.owner)
      return Lease{};

    // Do not move this invocation outside the shared-lock scope. The lease
    // must retain the owner before Unregister can release registry ownership.
    return std::invoke(std::forward<Acquire>(acquire), record, range.owner);
  }

private:
  bool UnregisterLocked(Record *records, Owner *owner) {
    const auto it = ranges_.find(reinterpret_cast<uintptr_t>(records));
    if (it == ranges_.end() || it->second.owner != owner)
      return false;
    ranges_.erase(it);
    return true;
  }

  struct Range {
    uintptr_t begin = 0;
    uintptr_t end = 0;
    Record *records = nullptr;
    std::size_t count = 0;
    Owner *owner = nullptr;
  };

  std::map<uintptr_t, Range> ranges_;
  mutable std::shared_mutex mutex_;
};

} // namespace dxmt
