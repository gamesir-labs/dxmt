#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_2.h>

// Public D3D11 capability-query validation coverage. Every query is read-only
// on a test-local device and is safe under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

class D3D11DeviceCapabilityValidationContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device2), reinterpret_cast<void **>(device2_.put())),
        S_OK);
    ASSERT_NE(device2_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device2> device2_;
};

TEST_F(D3D11DeviceCapabilityValidationContractSpec,
       CheckFormatSupportRejectsNullOutput) {
  EXPECT_EQ(context_.device()->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM,
                                                  nullptr),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->CheckFormatSupport(DXGI_FORMAT_UNKNOWN, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DeviceCapabilityValidationContractSpec,
       MultisampleQueriesRejectNullOutputAcrossVersions) {
  EXPECT_EQ(context_.device()->CheckMultisampleQualityLevels(
                DXGI_FORMAT_R8G8B8A8_UNORM, 4, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(device2_->CheckMultisampleQualityLevels1(DXGI_FORMAT_R8G8B8A8_UNORM,
                                                     4, 0, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DeviceCapabilityValidationContractSpec,
       CheckFeatureSupportRejectsNullData) {
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D11_FEATURE_THREADING, nullptr,
                sizeof(D3D11_FEATURE_DATA_THREADING)),
            E_INVALIDARG);
  EXPECT_EQ(
      context_.device()->CheckFeatureSupport(
          D3D11_FEATURE_DOUBLES, nullptr, sizeof(D3D11_FEATURE_DATA_DOUBLES)),
      E_INVALIDARG);
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D11_FEATURE_D3D11_OPTIONS, nullptr,
                sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS)),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
