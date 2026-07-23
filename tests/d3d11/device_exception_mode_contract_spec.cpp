#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <array>
#include <limits>

// Public ID3D11Device exception-mode coverage. Every case uses a test-local
// device and only changes state owned by that device, so the cases are safe for
// the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

class D3D11DeviceExceptionModeContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device5), reinterpret_cast<void **>(device5_.put())),
        S_OK);
    ASSERT_NE(device5_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device5> device5_;
};

TEST_F(D3D11DeviceExceptionModeContractSpec, DefaultsToDisabled) {
  EXPECT_EQ(context_.device()->GetExceptionMode(), 0u);
  EXPECT_EQ(device5_->GetExceptionMode(), 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DeviceExceptionModeContractSpec,
       RoundTripsSupportedModeAcrossDeviceVersions) {
  constexpr std::array<UINT, 4> kModes = {
      D3D11_RAISE_FLAG_DRIVER_INTERNAL_ERROR,
      0,
      D3D11_RAISE_FLAG_DRIVER_INTERNAL_ERROR,
      0,
  };

  for (std::size_t i = 0; i < kModes.size(); ++i) {
    ID3D11Device *setter = i % 2 == 0
                               ? context_.device()
                               : static_cast<ID3D11Device *>(device5_.get());
    EXPECT_EQ(setter->SetExceptionMode(kModes[i]), S_OK);
    EXPECT_EQ(context_.device()->GetExceptionMode(), kModes[i]);
    EXPECT_EQ(device5_->GetExceptionMode(), kModes[i]);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DeviceExceptionModeContractSpec,
       RejectsUnsupportedBitsWithoutChangingMode) {
  ASSERT_EQ(context_.device()->SetExceptionMode(
                D3D11_RAISE_FLAG_DRIVER_INTERNAL_ERROR),
            S_OK);

  constexpr std::array<UINT, 3> kInvalidModes = {
      2,
      D3D11_RAISE_FLAG_DRIVER_INTERNAL_ERROR | 2,
      std::numeric_limits<UINT>::max(),
  };
  for (const UINT invalid_mode : kInvalidModes) {
    EXPECT_EQ(device5_->SetExceptionMode(invalid_mode), E_INVALIDARG);
    EXPECT_EQ(context_.device()->GetExceptionMode(),
              static_cast<UINT>(D3D11_RAISE_FLAG_DRIVER_INTERNAL_ERROR));
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
