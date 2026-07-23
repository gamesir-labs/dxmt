#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_3.h>

#include <array>
#include <cstdint>

// Public IDXGIObject contracts for the DXGI device reached from D3D11. All
// private data belongs to the test-local object, making these tests parallel
// safe without scheduler serialization.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr GUID kPrivateDataKey = {
    0xc06a28cc,
    0x26bf,
    0x46ad,
    {0xb4, 0x2d, 0x46, 0x62, 0x18, 0x19, 0xd5, 0xe2},
};

class D3D11DxgiDeviceObjectContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(context_.device()->QueryInterface(
                  __uuidof(IDXGIDevice),
                  reinterpret_cast<void **>(dxgi_device_.put())),
              S_OK);
    ASSERT_EQ(context_.device()->QueryInterface(
                  __uuidof(IDXGIDevice3),
                  reinterpret_cast<void **>(dxgi_device3_.put())),
              S_OK);
  }

  D3D11TestContext context_;
  ComPtr<IDXGIDevice> dxgi_device_;
  ComPtr<IDXGIDevice3> dxgi_device3_;
};

TEST_F(D3D11DxgiDeviceObjectContractSpec,
       SharesPrivateDataAcrossInterfaceVersions) {
  constexpr std::array<std::uint8_t, 6> kInitial = {1, 3, 5, 7, 9, 11};
  constexpr std::array<std::uint8_t, 3> kReplacement = {0xa1, 0xb2, 0xc3};

  ASSERT_EQ(dxgi_device_->SetPrivateData(kPrivateDataKey, kInitial.size(),
                                         kInitial.data()),
            S_OK);

  UINT required_size = 0;
  ASSERT_EQ(
      dxgi_device3_->GetPrivateData(kPrivateDataKey, &required_size, nullptr),
      S_OK);
  ASSERT_EQ(required_size, kInitial.size());

  std::array<std::uint8_t, kInitial.size()> initial = {};
  UINT actual_size = initial.size();
  ASSERT_EQ(dxgi_device3_->GetPrivateData(kPrivateDataKey, &actual_size,
                                          initial.data()),
            S_OK);
  EXPECT_EQ(actual_size, kInitial.size());
  EXPECT_EQ(initial, kInitial);

  ASSERT_EQ(dxgi_device3_->SetPrivateData(kPrivateDataKey, kReplacement.size(),
                                          kReplacement.data()),
            S_OK);
  std::array<std::uint8_t, kReplacement.size()> replacement = {};
  actual_size = replacement.size();
  ASSERT_EQ(dxgi_device_->GetPrivateData(kPrivateDataKey, &actual_size,
                                         replacement.data()),
            S_OK);
  EXPECT_EQ(actual_size, kReplacement.size());
  EXPECT_EQ(replacement, kReplacement);

  ASSERT_EQ(dxgi_device_->SetPrivateData(kPrivateDataKey, 0, nullptr), S_OK);
  actual_size = replacement.size();
  EXPECT_EQ(dxgi_device3_->GetPrivateData(kPrivateDataKey, &actual_size,
                                          replacement.data()),
            DXGI_ERROR_NOT_FOUND);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiDeviceObjectContractSpec,
       GetParentReturnsCreatingAdapterIdentity) {
  ComPtr<IDXGIAdapter> parent;
  ASSERT_EQ(dxgi_device3_->GetParent(__uuidof(IDXGIAdapter),
                                     reinterpret_cast<void **>(parent.put())),
            S_OK);
  ASSERT_NE(parent.get(), nullptr);

  ComPtr<IUnknown> parent_identity;
  ASSERT_EQ(
      parent->QueryInterface(__uuidof(IUnknown),
                             reinterpret_cast<void **>(parent_identity.put())),
      S_OK);
  ComPtr<IUnknown> expected_identity;
  ASSERT_EQ(context_.adapter()->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(expected_identity.put())),
            S_OK);
  EXPECT_EQ(parent_identity.get(), expected_identity.get());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
