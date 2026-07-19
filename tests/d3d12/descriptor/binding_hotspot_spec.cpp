#include <dxmt_test.hpp>

#include "../d3d12_binding_hotspot_scenarios.hpp"

namespace {

using dxmt::test::BindingHotspotMeasurement;

TEST(D3D12BindingHotspot, PublishesArgumentTableUpdatesBeforeGpuUse) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunArgumentTableUpdateScenario(8, 3, &measurement);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(measurement.operations, 24u);
  EXPECT_EQ(measurement.actual, measurement.expected);
}

TEST(D3D12BindingHotspot, MaterializesEveryRootTableDuringClose) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunRootTableMaterializationScenario(4, 8, &measurement);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(measurement.operations, 32u);
  EXPECT_EQ(measurement.actual, measurement.expected);
}

TEST(D3D12BindingHotspot, PublishesBulkDescriptorMirrorCopiesBeforeGpuUse) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunDescriptorMirrorMutationScenario(8, 4, &measurement);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(measurement.operations, 32u);
  EXPECT_EQ(measurement.actual, measurement.expected);
}

TEST(D3D12BindingHotspot, PreservesTypedBufferTextureViewMaterialization) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunTypedBufferDescriptorScenario(&measurement);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(measurement.actual, measurement.expected);
}

TEST(D3D12BindingHotspot, InvalidatesMaterializedRootTableAfterRebind) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunRootTableInvalidationScenario(&measurement);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(measurement.actual, measurement.expected);
}

TEST(D3D12BindingHotspot,
     ExecutesBatchedRootDescriptorDrawsWithValidLifetimes) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunCompiledRootDescriptorResidencyScenario(17,
                                                             &measurement);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(measurement.operations, 17u);
  EXPECT_EQ(measurement.actual, measurement.expected);
}

TEST(D3D12BindingHotspot, BatchesThousandsOfLogicalCasesIntoGpuOracles) {
  BindingHotspotMeasurement argument_updates;
  auto error =
      dxmt::test::RunArgumentTableUpdateScenario(64, 32, &argument_updates);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(argument_updates.operations, 2048u);
  EXPECT_EQ(argument_updates.actual, argument_updates.expected);

  BindingHotspotMeasurement root_tables;
  error = dxmt::test::RunRootTableMaterializationScenario(128, 16,
                                                          &root_tables);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(root_tables.operations, 2048u);
  EXPECT_EQ(root_tables.actual, root_tables.expected);

  BindingHotspotMeasurement descriptor_copies;
  error = dxmt::test::RunDescriptorMirrorMutationScenario(
      64, 32, &descriptor_copies);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(descriptor_copies.operations, 2048u);
  EXPECT_EQ(descriptor_copies.actual, descriptor_copies.expected);
}

TEST(D3D12BindingHotspot, DelaysMappedRootCbvReuseUntilQueueFenceCompletes) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunRootCbvFenceReuseScenario(&measurement);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(measurement.operations, 1u);
  EXPECT_EQ(measurement.actual, measurement.expected);
}

TEST(D3D12BindingHotspot,
     PreservesUiLayersAcrossQueuedExecuteAndBackbufferReuse) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunQueuedUiLayerCompositionScenario(12, &measurement);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(measurement.operations, 36u);
  EXPECT_EQ(measurement.actual, measurement.expected);
}

} // namespace
