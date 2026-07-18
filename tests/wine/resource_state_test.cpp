#include <dxmt_test.hpp>

#include "dxmt_allocation.hpp"
#include "dxmt_occlusion_query.hpp"
#include "dxmt_residency.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

class ProbeAllocation final : public dxmt::Allocation {
public:
  explicit ProbeAllocation(uint32_t &destructions)
      : destructions_(destructions) {}
  ~ProbeAllocation() override { ++destructions_; }

private:
  uint32_t &destructions_;
};

} // namespace

TEST(Allocation, DetectsRepeatedRetentionSequence) {
  uint32_t destructions = 0;
  ProbeAllocation allocation(destructions);
  EXPECT_FALSE(allocation.checkRetained(7));
  EXPECT_TRUE(allocation.checkRetained(7));
  EXPECT_FALSE(allocation.checkRetained(8));
  EXPECT_TRUE(allocation.checkRetained(8));
}

TEST(Allocation, DeduplicatesRetentionAcrossConcurrentRecorders) {
  constexpr size_t thread_count = 8;
  constexpr uint64_t sequence_count = 256;
  uint32_t destructions = 0;
  ProbeAllocation allocation(destructions);
  std::barrier phase(thread_count);
  std::atomic<uint64_t> first_retention_count = 0;
  std::vector<std::thread> recorders;
  recorders.reserve(thread_count);

  for (size_t thread = 0; thread < thread_count; ++thread) {
    recorders.emplace_back([&] {
      for (uint64_t sequence = 1; sequence <= sequence_count; ++sequence) {
        phase.arrive_and_wait();
        if (!allocation.checkRetained(sequence))
          first_retention_count.fetch_add(1, std::memory_order_relaxed);
        phase.arrive_and_wait();
      }
    });
  }
  for (auto &recorder : recorders)
    recorder.join();

  EXPECT_EQ(first_retention_count.load(std::memory_order_relaxed),
            sequence_count);
}

TEST(AllocationRefTracking, ClearsOutstandingReferencesOnDestruction) {
  static_assert(!std::is_copy_constructible_v<dxmt::AllocationRefTracking>);
  uint32_t destructions = 0;
  {
    dxmt::AllocationRefTracking tracking;
    EXPECT_TRUE(tracking.track(new ProbeAllocation(destructions)));
    EXPECT_EQ(destructions, 0u);
  }
  EXPECT_EQ(destructions, 1u);
}

TEST(AllocationRefTracking, ExtendsCapacityAndTransfersInInsertionOrder) {
  uint32_t destructions = 0;
  auto *allocation = new ProbeAllocation(destructions);
  dxmt::AllocationRefTracking tracking;

  for (size_t i = 0; i < 29; ++i)
    EXPECT_TRUE(tracking.track(allocation));
  EXPECT_FALSE(tracking.track(allocation));

  alignas(std::max_align_t) std::array<std::byte, 128> storage = {};
  tracking.addStorage(storage.data(), storage.size());
  EXPECT_TRUE(tracking.track(allocation));

  std::vector<dxmt::Allocation *> transferred;
  tracking.transferTo(transferred);
  ASSERT_EQ(transferred.size(), 30u);
  for (auto *entry : transferred)
    EXPECT_EQ(entry, allocation);
  for (auto *entry : transferred)
    entry->decRef();
  EXPECT_EQ(destructions, 1u);

  tracking.clear();
  EXPECT_EQ(destructions, 1u);
}

TEST(ResourceResidency, MergesRequestsWithinEncoderAndResetsOnNewEncoder) {
  dxmt::DXMT_RESOURCE_RESIDENCY_STATE state;
  auto requested = dxmt::DXMT_RESOURCE_RESIDENCY_VERTEX_READ;
  EXPECT_TRUE(dxmt::CheckResourceResidency(state, 1, requested));
  EXPECT_EQ(requested, dxmt::DXMT_RESOURCE_RESIDENCY_VERTEX_READ);

  requested = dxmt::DXMT_RESOURCE_RESIDENCY_VERTEX_READ;
  EXPECT_FALSE(dxmt::CheckResourceResidency(state, 1, requested));

  requested = dxmt::DXMT_RESOURCE_RESIDENCY_FRAGMENT_WRITE;
  EXPECT_TRUE(dxmt::CheckResourceResidency(state, 1, requested));
  EXPECT_EQ(requested, dxmt::DXMT_RESOURCE_RESIDENCY_VERTEX_READ |
                           dxmt::DXMT_RESOURCE_RESIDENCY_FRAGMENT_WRITE);

  requested = dxmt::DXMT_RESOURCE_RESIDENCY_MESH_READ;
  EXPECT_TRUE(dxmt::CheckResourceResidency(state, 2, requested));
  EXPECT_EQ(state.last_residency_mask, dxmt::DXMT_RESOURCE_RESIDENCY_MESH_READ);

  requested = dxmt::DXMT_RESOURCE_RESIDENCY_VERTEX_WRITE;
  EXPECT_FALSE(dxmt::CheckResourceResidency(state, 1, requested));
}

TEST(ResourceResidency, ConvertsMaskToUsageAndRenderStages) {
  const auto mask = dxmt::DXMT_RESOURCE_RESIDENCY_VERTEX_READ |
                    dxmt::DXMT_RESOURCE_RESIDENCY_FRAGMENT_WRITE |
                    dxmt::DXMT_RESOURCE_RESIDENCY_OBJECT_READ |
                    dxmt::DXMT_RESOURCE_RESIDENCY_MESH_WRITE;
  EXPECT_EQ(dxmt::GetUsageFromResidencyMask(mask),
            WMTResourceUsageRead | WMTResourceUsageWrite);
  EXPECT_EQ(dxmt::GetStagesFromResidencyMask(mask),
            WMTRenderStageVertex | WMTRenderStageFragment |
                WMTRenderStageObject | WMTRenderStageMesh);
  EXPECT_EQ(dxmt::GetUsageFromResidencyMask(dxmt::DXMT_RESOURCE_RESIDENCY_NULL),
            WMTResourceUsage(0));

  const auto geometry_masks =
      dxmt::DXMT_RESOURCE_RESIDENCY_OBJECT_GS_WRITE |
      dxmt::DXMT_RESOURCE_RESIDENCY_MESH_GS_READ;
  EXPECT_EQ(dxmt::GetUsageFromResidencyMask(geometry_masks),
            WMTResourceUsageRead | WMTResourceUsageWrite);
  EXPECT_EQ(dxmt::GetStagesFromResidencyMask(geometry_masks),
            WMTRenderStageObject | WMTRenderStageMesh);
}

TEST(VisibilityOffsetState, AdvancesOnlyAfterWrittenVisibilityData) {
  dxmt::VisibilityResultOffsetBumpState state;
  state.beginEncoder();

  uint64_t offset = 0;
  EXPECT_FALSE(state.tryGetNextWriteOffset(false, offset));
  EXPECT_EQ(offset, ~0ull);
  EXPECT_TRUE(state.tryGetNextWriteOffset(true, offset));
  EXPECT_EQ(offset, 0u);
  EXPECT_FALSE(state.tryGetNextWriteOffset(true, offset));
  EXPECT_EQ(state.getNextReadOffset(), 1u);
  EXPECT_TRUE(state.tryGetNextWriteOffset(true, offset));
  EXPECT_EQ(offset, 1u);

  state.endEncoder();
  EXPECT_EQ(state.reset(), 2u);
  EXPECT_EQ(state.reset(), 0u);
}

TEST(VisibilityQuery, AccumulatesReadbackAcrossCommandSequences) {
  dxmt::Rc<dxmt::VisibilityResultQuery> query(
      new dxmt::VisibilityResultQuery());
  query->begin(10, 1);
  query->end(12, 2);

  const std::array<uint64_t, 4> first = {100, 1, 2, 3};
  const std::array<uint64_t, 2> middle = {4, 5};
  const std::array<uint64_t, 3> last = {6, 7, 100};
  query->issue(10, first.data(), first.size());
  query->issue(11, middle.data(), middle.size());
  uint64_t value = 0;
  EXPECT_FALSE(query->getValue(&value));
  query->issue(12, last.data(), last.size());
  EXPECT_TRUE(query->getValue(&value));
  EXPECT_EQ(value, 28u);

  query->reset();
  EXPECT_FALSE(query->getValue(&value));
  query->begin(20, 3);
  query->end(20, 3);
  EXPECT_TRUE(query->getValue(&value));
  EXPECT_EQ(value, 0u);
}

TEST(VisibilityQuery, AccumulatesAHalfOpenRangeWithinOneSequence) {
  dxmt::Rc<dxmt::VisibilityResultQuery> query(
      new dxmt::VisibilityResultQuery());
  query->begin(30, 1);
  query->end(30, 4);
  EXPECT_EQ(query->queryEndAt(), 30u);

  uint64_t value = 0;
  EXPECT_FALSE(query->getValue(&value));
  const std::array<uint64_t, 5> readback = {100, 2, 3, 5, 100};
  query->issue(30, readback.data(), readback.size());
  EXPECT_TRUE(query->getValue(&value));
  EXPECT_EQ(value, 10u);
}

TEST(TimestampQuery, TracksSampleLocationAndResolvedValue) {
  dxmt::Rc<dxmt::TimestampQuery> query(new dxmt::TimestampQuery());
  uint64_t value = 0;
  EXPECT_FALSE(query->getValue(&value));

  query->setSampleSequence(11);
  query->setSampleIndex(13);
  EXPECT_EQ(query->sampleSequence(), 11u);
  EXPECT_EQ(query->sampleIndex(), 13u);
  query->setSampleLocation(17, 19);
  EXPECT_EQ(query->sampleSequence(), 17u);
  EXPECT_EQ(query->sampleIndex(), 19u);

  query->issue(23);
  EXPECT_TRUE(query->getValue(&value));
  EXPECT_EQ(value, 23u);
}
