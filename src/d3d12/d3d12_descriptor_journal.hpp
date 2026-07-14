#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dxmt::d3d12 {

struct DescriptorChange {
  uint64_t sequence = 0;
  uint32_t slot = 0;
  uint64_t generation = 0;
};

struct DescriptorChangeSet {
  uint64_t cursor = 0;
  bool complete = true;
  std::vector<DescriptorChange> changes;
};

// A bounded per-heap mutation journal. Consumers keep only a cursor; if they
// fall behind the retained window, complete=false forces a full descriptor
// scan instead of silently dropping an invalidation.
class DescriptorChangeJournal {
public:
  explicit DescriptorChangeJournal(size_t capacity)
      : entries_(std::max<size_t>(capacity, 1)) {}

  uint64_t cursor() const { return next_sequence_ - 1; }

  void Record(uint32_t slot, uint64_t generation) {
    const uint64_t sequence = next_sequence_++;
    entries_[size_ < entries_.size() ? size_ : head_] =
        DescriptorChange{sequence, slot, generation};
    if (size_ < entries_.size()) {
      size_++;
    } else {
      head_ = (head_ + 1) % entries_.size();
    }
  }

  DescriptorChangeSet ChangesSince(uint64_t cursor) const {
    DescriptorChangeSet result = {.cursor = this->cursor()};
    if (!size_ || cursor >= result.cursor)
      return result;

    const uint64_t oldest = entry(0).sequence;
    if (cursor + 1 < oldest) {
      result.complete = false;
      return result;
    }

    const auto first_sequence = cursor + 1;
    const size_t first_index =
        first_sequence <= oldest
            ? 0
            : std::min<uint64_t>(first_sequence - oldest, size_);
    result.changes.reserve(size_ - first_index);
    for (size_t i = first_index; i < size_; i++) {
      const auto &change = entry(i);
      result.changes.push_back(change);
    }
    return result;
  }

private:
  const DescriptorChange &entry(size_t logical_index) const {
    const size_t physical =
        size_ < entries_.size() ? logical_index
                                : (head_ + logical_index) % entries_.size();
    return entries_[physical];
  }

  std::vector<DescriptorChange> entries_;
  uint64_t next_sequence_ = 1;
  size_t head_ = 0;
  size_t size_ = 0;
};

} // namespace dxmt::d3d12
