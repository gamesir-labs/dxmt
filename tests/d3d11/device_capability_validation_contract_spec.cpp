#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_2.h>

#include <array>
#include <cstdint>

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
       FormatSupportEntryPointsReturnIdenticalFlags) {
  constexpr std::array<DXGI_FORMAT, 4> kFormats = {
      DXGI_FORMAT_R8G8B8A8_UNORM,
      DXGI_FORMAT_R32_UINT,
      DXGI_FORMAT_D24_UNORM_S8_UINT,
      DXGI_FORMAT_BC1_UNORM,
  };

  for (const DXGI_FORMAT format : kFormats) {
    UINT direct_support = 0;
    ASSERT_EQ(context_.device()->CheckFormatSupport(format, &direct_support),
              S_OK);

    D3D11_FEATURE_DATA_FORMAT_SUPPORT feature_support = {};
    feature_support.InFormat = format;
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D11_FEATURE_FORMAT_SUPPORT, &feature_support,
                  sizeof(feature_support)),
              S_OK);
    EXPECT_EQ(feature_support.OutFormatSupport, direct_support)
        << "format=" << static_cast<std::uint32_t>(format);
  }
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
       CheckFeatureSupportRejectsIncorrectStructureSizes) {
  D3D11_FEATURE_DATA_THREADING threading = {};
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D11_FEATURE_THREADING, &threading, sizeof(threading) - 1),
            E_INVALIDARG);

  D3D11_FEATURE_DATA_DOUBLES doubles = {};
  EXPECT_EQ(context_.device()->CheckFeatureSupport(
                D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles) + 1),
            E_INVALIDARG);

  D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
  EXPECT_EQ(context_.device()->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,
                                                   &options, 0),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
