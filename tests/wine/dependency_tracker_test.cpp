#include <dxmt_test.hpp>

#include "dxmt_deptrack.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

TEST(FenceSet, SupportsSetAlgebraAcrossParityLanes) {
  dxmt::FenceSet fences;
  for (const auto id : {1u, 65u, 129u, 193u})
    fences.set(id);

  EXPECT_EQ(fences.count(), 4u);
  EXPECT_EQ(fences.laneMask(), 1ull << 1);
  EXPECT_TRUE(fences.test(129));
  EXPECT_TRUE(fences.testAndSet(129));
  EXPECT_FALSE(fences.testAndSet(2));
  EXPECT_EQ(fences.count(), 5u);

  dxmt::FenceSet other;
  other.set(2);
  other.set(66);
  EXPECT_TRUE(fences.intersectedWith(other));
  EXPECT_FALSE(fences.contains(other));

  const auto combined = fences.unionOf(other);
  EXPECT_TRUE(combined.contains(fences));
  EXPECT_TRUE(combined.contains(other));

  auto reduced = combined;
  reduced.subtract(fences);
  EXPECT_EQ(reduced.count(), 1u);
  EXPECT_TRUE(reduced.test(66));
  reduced.unset(66);
  EXPECT_TRUE(reduced.empty());
}

TEST(FenceSet, EnumeratesCurrentAndPriorEntriesDeterministically) {
  dxmt::FenceSet current;
  current.set(3);
  current.set(67);
  dxmt::FenceSet prior;
  prior.set(67);
  prior.set(131);

  std::vector<dxmt::EncoderId> current_ids;
  std::vector<dxmt::EncoderId> prior_ids;
  current.forEach(
      prior, [&](auto id) { prior_ids.push_back(id); },
      [&](auto id) { current_ids.push_back(id); });

  EXPECT_EQ(prior_ids, (std::vector<dxmt::EncoderId>{67, 131}));
  EXPECT_EQ(current_ids, (std::vector<dxmt::EncoderId>{3}));
}

TEST(FenceSet, KeepsExactEncoderIdsBeyondLegacySlotCapacity) {
  dxmt::FenceSet fences;
  EXPECT_TRUE(fences.set(256));
  EXPECT_TRUE(fences.set(512));
  EXPECT_EQ(fences.count(), 2u);
  EXPECT_TRUE(fences.test(256));
  EXPECT_TRUE(fences.test(512));
}

TEST(FenceSet, IteratesExactIdsInAscendingOrderAfterSetOperations) {
  dxmt::FenceSet fences;
  for (const auto id : {1024u, 3u, 512u, 256u, 1024u})
    fences.set(id);

  dxmt::FenceSet removed;
  removed.set(512);
  fences.subtract(removed);
  fences.merge(dxmt::FenceSet(768));

  std::vector<dxmt::EncoderId> ids;
  fences.forEach([&](auto id) { ids.push_back(id); });
  EXPECT_EQ(ids, (std::vector<dxmt::EncoderId>{3, 256, 768, 1024}));
}

TEST(CommandBufferFenceBindingTable,
     DropsPriorCommandBufferIdsBeforeReusingTheirSlots) {
  dxmt::CommandBufferFenceBindingTable bindings;
  bindings.reset(0);
  EXPECT_EQ(bindings.bind(1024, 0, 8), 0u);
  ASSERT_TRUE(bindings.find(1024).has_value());

  bindings.reset(bindings.slotCount());
  EXPECT_FALSE(bindings.find(1024).has_value());
  EXPECT_EQ(bindings.bind(2048, 0, 4), 0u);
  EXPECT_EQ(*bindings.find(2048), 0u);
}

TEST(CommandBufferFenceBindingTable,
     ReusesOnlyNonOverlappingIntervalsWithinOneCommandBuffer) {
  dxmt::CommandBufferFenceBindingTable bindings;
  bindings.reset(0);

  EXPECT_EQ(bindings.bind(300, 0, 4), 0u);
  EXPECT_EQ(bindings.bind(301, 2, 6), 1u);
  EXPECT_EQ(bindings.bind(302, 5, 8), 0u);
  EXPECT_EQ(bindings.bind(303, 7, 9), 1u);
  EXPECT_EQ(bindings.slotCount(), 2u);
}

TEST(CommandBufferFenceBindingTable,
     KeepsTouchingIntervalsSeparateAndReusesTheNextAvailableSlot) {
  dxmt::CommandBufferFenceBindingTable bindings;
  bindings.reset(0);

  EXPECT_EQ(bindings.bind(400, 0, 4), 0u);
  EXPECT_EQ(bindings.bind(401, 4, 5), 1u);
  EXPECT_EQ(bindings.bind(402, 5, 6), 0u);
  EXPECT_EQ(bindings.slotCount(), 2u);
}

TEST(CommandBufferFenceBindingTable, ReusesPreallocatedSlotsAfterReset) {
  dxmt::CommandBufferFenceBindingTable bindings;
  bindings.reset(3);
  EXPECT_EQ(bindings.bind(500, 0, 1), 0u);
  EXPECT_EQ(bindings.slotCount(), 3u);

  bindings.reset(bindings.slotCount());
  EXPECT_FALSE(bindings.find(500).has_value());
  EXPECT_EQ(bindings.bind(501, 0, 1), 0u);
  EXPECT_EQ(bindings.slotCount(), 3u);
}

TEST(CommandBufferFenceBindingTable,
     DoesNotBindExternalOnlyWaitsToFutureLocalUpdates) {
  dxmt::CommandBufferFenceBindingTable bindings;
  bindings.reset(2);

  constexpr dxmt::EncoderId kExternalWait = 900;
  constexpr dxmt::EncoderId kFutureLocalUpdate = 1200;
  EXPECT_FALSE(bindings.find(kExternalWait).has_value());
  EXPECT_EQ(bindings.bind(kFutureLocalUpdate, 5, 7), 0u);
  EXPECT_FALSE(bindings.find(kExternalWait).has_value());
  EXPECT_EQ(*bindings.find(kFutureLocalUpdate), 0u);
}

TEST(FenceDependencyOrderTracker,
     ClassifiesPriorFutureSameAndExternalWaitsByExactId) {
  dxmt::FenceDependencyOrderTracker tracker;
  tracker.recordUpdates(dxmt::FenceSet(1024));
  tracker.recordUpdates(dxmt::FenceSet(2048));
  tracker.recordUpdates(dxmt::FenceSet(3072));

  dxmt::FenceSet first_waits;
  first_waits.set(2048);
  first_waits.set(4096);
  tracker.analyzeEncoder(first_waits, dxmt::FenceSet(1024));

  dxmt::FenceSet second_waits;
  second_waits.set(1024);
  second_waits.set(3072);
  tracker.analyzeEncoder(second_waits, dxmt::FenceSet(3072));

  const auto &analysis = tracker.analysis();
  EXPECT_EQ(analysis.prior_local_waits, 1u);
  EXPECT_EQ(analysis.future_local_waits, 1u);
  EXPECT_EQ(analysis.same_encoder_waits, 1u);
  EXPECT_EQ(analysis.external_waits, 1u);
}

TEST(FenceDependencyOrderTracker, CountsRepeatedUpdatesByExactId) {
  dxmt::FenceDependencyOrderTracker tracker;
  dxmt::FenceSet first;
  first.set(256);
  first.set(512);
  tracker.recordUpdates(first);

  dxmt::FenceSet repeated;
  repeated.set(256);
  repeated.set(768);
  tracker.recordUpdates(repeated);
  tracker.recordUpdates(dxmt::FenceSet(256));

  EXPECT_EQ(tracker.analysis().repeated_updates, 2u);
}

TEST(RenderFenceMerge, RemovesNaturallyOrderedCrossStageWait) {
  dxmt::FenceSet latter_fragment_waits(256);
  dxmt::FenceSet former_pre_raster_updates(256);

  const auto plan = dxmt::BuildRenderFenceMergePlan(
      latter_fragment_waits, {}, {}, {}, {}, {}, {},
      former_pre_raster_updates);

  EXPECT_TRUE(plan.valid());
  EXPECT_TRUE(plan.fragment_waits.empty());
  EXPECT_TRUE(plan.pre_raster_updates.test(256));
}

TEST(RenderFenceMerge, RejectsSameStageAndReverseDependencies) {
  const auto same_stage = dxmt::BuildRenderFenceMergePlan(
      dxmt::FenceSet(256), {}, {}, {}, {}, {}, dxmt::FenceSet(256), {});
  EXPECT_FALSE(same_stage.valid());

  const auto reverse_stage = dxmt::BuildRenderFenceMergePlan(
      {}, dxmt::FenceSet(257), {}, {}, {}, {}, dxmt::FenceSet(257), {});
  EXPECT_FALSE(reverse_stage.valid());
}

TEST(RenderFenceMerge, RejectsDependencyOnLatterPassUpdate) {
  const auto plan = dxmt::BuildRenderFenceMergePlan(
      {}, {}, dxmt::FenceSet(258), {}, dxmt::FenceSet(258), {}, {}, {});
  EXPECT_FALSE(plan.valid());
}

TEST(RenderFenceMerge, ResolveMergeRemovesInternalWaits) {
  const auto plan = dxmt::BuildRenderResolveFenceMergePlan(
      {}, dxmt::FenceSet(259), dxmt::FenceSet(259), dxmt::FenceSet(260));
  EXPECT_TRUE(plan.valid());
  EXPECT_TRUE(plan.fragment_waits.empty());
  EXPECT_TRUE(plan.fragment_updates.test(259));
  EXPECT_TRUE(plan.fragment_updates.test(260));
}

TEST(TrackingSet, RetainsOnlyTheRecentEncoderWindow) {
  dxmt::TrackingSet<4> tracking;
  EXPECT_TRUE(tracking.add(65));
  EXPECT_FALSE(tracking.add(65));
  EXPECT_TRUE(tracking.add(66));
  EXPECT_TRUE(tracking.add(67));
  EXPECT_TRUE(tracking.add(68));
  EXPECT_TRUE(tracking.isLastAccess(68));

  std::vector<dxmt::EncoderId> ids;
  EXPECT_EQ(tracking.enumerate(69, [&](auto id) { ids.push_back(id); }), 3u);
  EXPECT_EQ(ids, (std::vector<dxmt::EncoderId>{68, 67, 66}));

  tracking.clear();
  ids.clear();
  EXPECT_EQ(tracking.enumerate(70, [&](auto id) { ids.push_back(id); }), 0u);
}

TEST(GenericAccessTracker, EmitsCrossEncoderWaitsAndInEncoderBarriers) {
  dxmt::GenericAccessTracker tracker;
  dxmt::FenceSet waits;
  dxmt::EncoderBarrierState barriers;

  tracker.accessExclusive(65, waits, barriers, false);
  EXPECT_TRUE(waits.empty());
  tracker.accessShared(66, waits, barriers);
  EXPECT_TRUE(waits.test(65));

  waits = {};
  tracker.accessExclusive(67, waits, barriers, false);
  EXPECT_TRUE(waits.test(66));
  EXPECT_TRUE(waits.test(65));

  dxmt::GenericAccessTracker same_encoder;
  barriers = {};
  same_encoder.accessExclusive(65, waits, barriers, false);
  same_encoder.accessShared(65, waits, barriers);
  EXPECT_EQ(barriers.barrierSet, dxmt::kBarrierTypeRW);

  barriers = {};
  same_encoder.accessExclusive(65, waits, barriers, true);
  EXPECT_EQ(barriers.barrierSet, dxmt::kBarrierTypeWaW);
}

TEST(GenericAccessTracker, DistinguishesPreRasterAndFragmentBarriers) {
  dxmt::GenericAccessTracker write_then_read;
  dxmt::FenceSet waits;
  dxmt::EncoderBarrierState barriers;
  write_then_read.accessExclusivePreRaster(65, waits, barriers, false);
  write_then_read.accessSharedFragment(66, waits, barriers);
  EXPECT_EQ(barriers.barrierFragmentAfterPreRasterSet, dxmt::kBarrierTypeRW);
  EXPECT_EQ(barriers.barrierSet, 0u);

  dxmt::GenericAccessTracker read_then_write;
  waits = {};
  barriers = {};
  read_then_write.accessSharedPreRaster(65, waits, barriers);
  read_then_write.accessExclusiveFragment(66, waits, barriers, false);
  EXPECT_EQ(barriers.barrierFragmentAfterPreRasterSet, dxmt::kBarrierTypeRW);
  EXPECT_EQ(barriers.barrierSet, 0u);
}

TEST(GenericAccessTracker, RetainsDistantWriterAndAllActiveReaders) {
  dxmt::GenericAccessTracker writer_tracker;
  dxmt::FenceSet waits;
  dxmt::EncoderBarrierState barriers;
  writer_tracker.accessExclusive(256, waits, barriers, false);
  waits = {};
  writer_tracker.accessShared(400, waits, barriers);
  EXPECT_EQ(waits.count(), 1u);
  EXPECT_TRUE(waits.test(256));

  dxmt::GenericAccessTracker reader_tracker;
  for (dxmt::EncoderId id = 256; id < 356; id++)
    reader_tracker.accessShared(id, waits, barriers);
  waits = {};
  reader_tracker.accessExclusive(356, waits, barriers, false);
  EXPECT_EQ(waits.count(), 100u);
  for (dxmt::EncoderId id = 256; id < 356; id++)
    EXPECT_TRUE(waits.test(id));
}

TEST(GenericAccessTracker, KeepsOneProducerForMultipleReadersWithoutReadEdges) {
  dxmt::GenericAccessTracker tracker;
  dxmt::FenceSet waits;
  dxmt::EncoderBarrierState barriers;
  tracker.accessExclusive(256, waits, barriers, false);

  waits = {};
  tracker.accessShared(257, waits, barriers);
  EXPECT_EQ(waits.count(), 1u);
  EXPECT_TRUE(waits.test(256));

  waits = {};
  tracker.accessShared(258, waits, barriers);
  EXPECT_EQ(waits.count(), 1u);
  EXPECT_TRUE(waits.test(256));
  EXPECT_FALSE(waits.test(257));
}

TEST(FenceLocalityCheck, RemovesTransitiveAndImplicitWaits) {
  dxmt::FenceLocalityCheck locality;

  auto waits = locality.collectAndSimplifyWaits(dxmt::FenceSet(256), 257);
  EXPECT_EQ(waits.count(), 1u);
  EXPECT_TRUE(waits.test(256));

  dxmt::FenceSet chained;
  chained.set(256);
  chained.set(257);
  waits = locality.collectAndSimplifyWaits(chained, 258);
  EXPECT_EQ(waits.count(), 1u);
  EXPECT_TRUE(waits.test(257));
  EXPECT_FALSE(waits.test(256));

  waits = locality.collectAndSimplifyWaits({}, 259, true);
  EXPECT_EQ(waits.count(), 0u);
  EXPECT_FALSE(waits.test(258));
}

TEST(FenceLocalityCheck, PreservesStrongDependenciesOutsideSummaryWindow) {
  dxmt::GenericAccessTracker tracker;
  dxmt::FenceSet strong_waits;
  dxmt::EncoderBarrierState barriers;
  tracker.accessExclusive(256, strong_waits, barriers, false);
  strong_waits = {};
  tracker.accessShared(600, strong_waits, barriers);
  ASSERT_TRUE(strong_waits.test(256));

  dxmt::FenceLocalityCheck locality;

  for (dxmt::EncoderId id = 256; id < 600; id++)
    locality.collectAndSimplifyWaits({}, id);

  auto waits = locality.collectAndSimplifyWaits(strong_waits, 600);
  EXPECT_EQ(waits.count(), 1u);
  EXPECT_TRUE(waits.test(256));
}

} // namespace
