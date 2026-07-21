#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class OptionalCommandContractSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(OptionalCommandContractSpec, EmptyStreamOutputTargetIsHarmless) {
  context_.list()->SOSetTargets(0, 0, nullptr);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec,
       NullProgrammableSamplePositionsResetIsHarmless) {
  ComPtr<ID3D12GraphicsCommandList1> list1;
  ASSERT_EQ(context_.list()->QueryInterface(
                __uuidof(ID3D12GraphicsCommandList1),
                reinterpret_cast<void **>(list1.put())),
            S_OK);
  list1->SetSamplePositions(0, 0, nullptr);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec,
       DefaultDepthBoundsAreHarmlessWhenUnsupported) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2 = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS2, &options2, sizeof(options2)),
            S_OK);
  if (options2.DepthBoundsTestSupported)
    GTEST_SKIP() << "Depth bounds are advertised";

  ComPtr<ID3D12GraphicsCommandList1> list1;
  const HRESULT interface_result = context_.list()->QueryInterface(
      __uuidof(ID3D12GraphicsCommandList1),
      reinterpret_cast<void **>(list1.put()));
  if (interface_result == E_NOINTERFACE)
    return;
  ASSERT_EQ(interface_result, S_OK);

  list1->OMSetDepthBounds(0.0f, 1.0f);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(OptionalCommandContractSpec,
       DefaultShadingRateIsHarmlessWhenUnsupported) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6)),
            S_OK);
  if (options6.VariableShadingRateTier !=
      D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED) {
    GTEST_SKIP() << "Variable rate shading is advertised";
  }

  ComPtr<ID3D12GraphicsCommandList5> list5;
  const HRESULT interface_result = context_.list()->QueryInterface(
      __uuidof(ID3D12GraphicsCommandList5),
      reinterpret_cast<void **>(list5.put()));
  if (interface_result == E_NOINTERFACE)
    return;
  ASSERT_EQ(interface_result, S_OK);

  list5->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
  list5->RSSetShadingRateImage(nullptr);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
