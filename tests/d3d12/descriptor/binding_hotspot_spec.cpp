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

TEST(D3D12BindingHotspot, RetainsSharedAllocationAfterPartialOverwrite) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunSharedDescriptorResidencyScenario(&measurement);
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
     KeepsCompiledRootDescriptorsResidentThroughBatchedEncoding) {
  BindingHotspotMeasurement measurement;
  const auto error =
      dxmt::test::RunCompiledRootDescriptorResidencyScenario(257,
                                                             &measurement);
  ASSERT_FALSE(error) << (error ? *error : "");
  EXPECT_EQ(measurement.operations, 257u);
  EXPECT_EQ(measurement.actual, measurement.expected);
}

} // namespace
