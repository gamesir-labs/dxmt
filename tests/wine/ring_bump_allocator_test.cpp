#include <dxmt_test.hpp>

#include "dxmt_ring_bump_allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

struct FakeAllocatorState {
  uint64_t next_id = 1;
  std::vector<size_t> allocated_sizes;
};

class FakeAllocator {
public:
  struct Block {
    uint64_t id = 0;
    size_t size = 0;
  };

  explicit FakeAllocator(std::shared_ptr<FakeAllocatorState> state)
      : state_(std::move(state)) {}

  Block allocate(size_t size) {
    state_->allocated_sizes.push_back(size);
    return {.id = state_->next_id++, .size = size};
  }

private:
  std::shared_ptr<FakeAllocatorState> state_;
};

using TestRing = dxmt::RingBumpState<FakeAllocator, 64, dxmt::null_mutex>;

TEST(RingBumpAllocator, AlignsSuballocationsAndFillsCurrentBlock) {
  auto state = std::make_shared<FakeAllocatorState>();
  TestRing ring{FakeAllocator(state)};

  const auto [first_block, first_offset] = ring.allocate(1, 0, 7, 1);
  const auto first_id = first_block.id;
  const auto [second_block, second_offset] = ring.allocate(2, 0, 8, 16);
  EXPECT_EQ(first_offset, 0u);
  EXPECT_EQ(second_offset, 16u);
  EXPECT_EQ(second_block.id, first_id);
  EXPECT_EQ(state->allocated_sizes, (std::vector<size_t>{64}));

  const auto [third_block, third_offset] = ring.allocate(3, 0, 41, 1);
  EXPECT_NE(third_block.id, first_id);
  EXPECT_EQ(third_offset, 0u);
  EXPECT_EQ(state->allocated_sizes, (std::vector<size_t>{64, 64}));
}

TEST(RingBumpAllocator, ReusesCompletedStandardBlocks) {
  auto state = std::make_shared<FakeAllocatorState>();
  TestRing ring{FakeAllocator(state)};

  const auto [first_block, first_offset] = ring.allocate(1, 0, 64, 1);
  const auto first_id = first_block.id;
  EXPECT_EQ(first_offset, 0u);
  ring.free_blocks(1);

  const auto [reused_block, reused_offset] = ring.allocate(2, 2, 16, 8);
  EXPECT_EQ(reused_block.id, first_id);
  EXPECT_EQ(reused_offset, 0u);
  EXPECT_EQ(state->allocated_sizes.size(), 1u);
}

TEST(RingBumpAllocator, ReleasesAdHocOversizedBlocksAfterCompletion) {
  auto state = std::make_shared<FakeAllocatorState>();
  TestRing ring{FakeAllocator(state)};

  const auto [oversized, oversized_offset] = ring.allocate(1, 0, 96, 1);
  const auto oversized_id = oversized.id;
  EXPECT_EQ(oversized.size, 96u);
  EXPECT_EQ(oversized_offset, 0u);
  ring.free_blocks(1);

  const auto [standard, standard_offset] = ring.allocate(2, 2, 16, 1);
  EXPECT_NE(standard.id, oversized_id);
  EXPECT_EQ(standard.size, 64u);
  EXPECT_EQ(standard_offset, 0u);
  EXPECT_EQ(state->allocated_sizes, (std::vector<size_t>{96, 64}));
}

TEST(RingBumpAllocator, DoesNotReuseBlocksStillInFlight) {
  auto state = std::make_shared<FakeAllocatorState>();
  TestRing ring{FakeAllocator(state)};

  const auto [first, first_offset] = ring.allocate(10, 0, 64, 1);
  const auto first_id = first.id;
  EXPECT_EQ(first_offset, 0u);
  const auto [second, second_offset] = ring.allocate(11, 9, 64, 1);
  EXPECT_NE(second.id, first_id);
  EXPECT_EQ(second_offset, 0u);
}

TEST(RingBumpAllocator, TracksTheLastSequenceThatUsedAStandardBlock) {
  auto state = std::make_shared<FakeAllocatorState>();
  TestRing ring{FakeAllocator(state)};

  const auto [first, first_offset] = ring.allocate(10, 0, 1, 1);
  const auto first_id = first.id;
  const auto [same, exact_fit_offset] = ring.allocate(11, 0, 48, 16);
  EXPECT_EQ(first_offset, 0u);
  EXPECT_EQ(exact_fit_offset, 16u);
  EXPECT_EQ(same.id, first_id);

  ring.free_blocks(10);
  const auto [second, second_offset] = ring.allocate(12, 11, 64, 1);
  EXPECT_NE(second.id, first_id);
  EXPECT_EQ(second_offset, 0u);

  ring.free_blocks(11);
  const auto [reused, reused_offset] = ring.allocate(13, 12, 64, 1);
  EXPECT_EQ(reused.id, first_id);
  EXPECT_EQ(reused_offset, 0u);
  EXPECT_EQ(state->allocated_sizes.size(), 2u);
}

} // namespace
