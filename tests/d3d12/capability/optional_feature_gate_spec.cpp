#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdlib>
#include <cstring>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

// Native WARP leaves the caller's failure sentinel unchanged for protected
// sessions and state objects. DXMT deliberately clears both output pointers.
bool ShouldCheckDxmtFailureOutputClearing() {
  const char *schema = std::getenv("ORACLE_SUITE_SCHEMA");
  return !schema || std::strcmp(schema, "public-api-v1") != 0;
}

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

TEST_F(D3D12OptionalFeatureGateSpec,
       UnadvertisedProtectedSessionsRejectCreation) {
  D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT support = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT, &support,
                sizeof(support)),
            S_OK);
  if (support.Support !=
      D3D12_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_NONE) {
    GTEST_SKIP() << "Protected resource sessions are advertised";
  }

  ComPtr<ID3D12Device4> device4;
  const HRESULT interface_result = context_.device()->QueryInterface(
      __uuidof(ID3D12Device4), reinterpret_cast<void **>(device4.put()));
  if (interface_result == E_NOINTERFACE)
    return;
  ASSERT_EQ(interface_result, S_OK);

  const D3D12_PROTECTED_RESOURCE_SESSION_DESC desc = {
      .NodeMask = 0,
      .Flags = D3D12_PROTECTED_RESOURCE_SESSION_FLAG_NONE,
  };
  void *session = context_.device();
  const HRESULT create_result = device4->CreateProtectedResourceSession(
      &desc, __uuidof(ID3D12ProtectedResourceSession), &session);
  EXPECT_TRUE(FAILED(create_result));
  if (ShouldCheckDxmtFailureOutputClearing()) {
    EXPECT_EQ(session, nullptr);
  }
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12OptionalFeatureGateSpec,
       EmptyMetaCommandEnumerationRejectsCreationAndClearsOutput) {
  ComPtr<ID3D12Device5> device5;
  const HRESULT interface_result = context_.device()->QueryInterface(
      __uuidof(ID3D12Device5), reinterpret_cast<void **>(device5.put()));
  if (interface_result == E_NOINTERFACE)
    return;
  ASSERT_EQ(interface_result, S_OK);

  UINT command_count = 0;
  ASSERT_EQ(device5->EnumerateMetaCommands(&command_count, nullptr), S_OK);
  if (command_count != 0)
    GTEST_SKIP() << "Meta commands are advertised";

  void *meta_command = context_.device();
  const HRESULT create_result = device5->CreateMetaCommand(
      GUID_NULL, 0, nullptr, 0, __uuidof(ID3D12MetaCommand), &meta_command);
  EXPECT_TRUE(FAILED(create_result));
  EXPECT_EQ(meta_command, nullptr);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12OptionalFeatureGateSpec,
       UnadvertisedRaytracingRejectsStateObjects) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)),
            S_OK);
  if (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
    GTEST_SKIP() << "Raytracing is advertised";

  ComPtr<ID3D12Device5> device5;
  const HRESULT interface_result = context_.device()->QueryInterface(
      __uuidof(ID3D12Device5), reinterpret_cast<void **>(device5.put()));
  if (interface_result == E_NOINTERFACE)
    return;
  ASSERT_EQ(interface_result, S_OK);

  const D3D12_STATE_OBJECT_DESC desc = {
      .Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
      .NumSubobjects = 0,
      .pSubobjects = nullptr,
  };
  void *state_object = context_.device();
  const HRESULT create_result = device5->CreateStateObject(
      &desc, __uuidof(ID3D12StateObject), &state_object);
  EXPECT_TRUE(FAILED(create_result));
  if (ShouldCheckDxmtFailureOutputClearing()) {
    EXPECT_EQ(state_object, nullptr);
  }
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
