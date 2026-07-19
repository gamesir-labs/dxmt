#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class D3D12OptionalFeatureGateSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12OptionalFeatureGateSpec, ReportsPublicOptionalFeatureTiers) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2 = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS2, &options2, sizeof(options2)),
            S_OK);
  EXPECT_GE(options2.ProgrammableSamplePositionsTier,
            D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED);
  EXPECT_LE(options2.ProgrammableSamplePositionsTier,
            D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_2);

  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)),
            S_OK);
  EXPECT_GE(options5.RaytracingTier, D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
  EXPECT_LE(options5.RaytracingTier, D3D12_RAYTRACING_TIER_1_1);
  EXPECT_GE(options5.RenderPassesTier, D3D12_RENDER_PASS_TIER_0);
  EXPECT_LE(options5.RenderPassesTier, D3D12_RENDER_PASS_TIER_2);

  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6)),
            S_OK);
  EXPECT_GE(options6.VariableShadingRateTier,
            D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED);
  EXPECT_LE(options6.VariableShadingRateTier,
            D3D12_VARIABLE_SHADING_RATE_TIER_2);

  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)),
            S_OK);
  EXPECT_GE(options7.MeshShaderTier, D3D12_MESH_SHADER_TIER_NOT_SUPPORTED);
  EXPECT_LE(options7.MeshShaderTier, D3D12_MESH_SHADER_TIER_1);
  EXPECT_GE(options7.SamplerFeedbackTier,
            D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED);
  EXPECT_LE(options7.SamplerFeedbackTier, D3D12_SAMPLER_FEEDBACK_TIER_1_0);
}

TEST_F(D3D12OptionalFeatureGateSpec,
       ResettingProgrammableSamplePositionsIsHarmless) {
  ComPtr<ID3D12GraphicsCommandList1> list1;
  ASSERT_EQ(context_.list()->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList1),
                reinterpret_cast<void **>(list1.put())),
            S_OK);
  list1->SetSamplePositions(0, 0, nullptr);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
