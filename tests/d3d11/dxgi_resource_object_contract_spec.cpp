#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_2.h>

#include <array>
#include <cstdint>

// Public DXGI resource object contracts for a non-shared D3D11 texture. All
// state and handles are test-local, so these tests are safe under the default
// parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr GUID kResourceDataKey = {
    0x3b347d17,
    0xb741,
    0x4c96,
    {0x8e, 0x1c, 0x37, 0x10, 0xcb, 0x83, 0xd0, 0xa6},
};

class D3D11DxgiResourceObjectContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 16;
    desc.Height = 8;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ASSERT_EQ(
        context_.device()->CreateTexture2D(&desc, nullptr, texture_.put()),
        S_OK);
    ASSERT_NE(texture_.get(), nullptr);
    ASSERT_EQ(
        texture_->QueryInterface(__uuidof(IDXGIResource),
                                 reinterpret_cast<void **>(resource_.put())),
        S_OK);
    ASSERT_EQ(
        texture_->QueryInterface(__uuidof(IDXGIResource1),
                                 reinterpret_cast<void **>(resource1_.put())),
        S_OK);
    ASSERT_NE(resource_.get(), nullptr);
    ASSERT_NE(resource1_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Texture2D> texture_;
  ComPtr<IDXGIResource> resource_;
  ComPtr<IDXGIResource1> resource1_;
};

TEST_F(D3D11DxgiResourceObjectContractSpec,
       SharesCanonicalIdentityAcrossResourceInterfaces) {
  ComPtr<IUnknown> texture_identity;
  ComPtr<IUnknown> resource_identity;
  ComPtr<IUnknown> resource1_identity;
  ASSERT_EQ(texture_->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(texture_identity.put())),
            S_OK);
  ASSERT_EQ(resource_->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(resource_identity.put())),
            S_OK);
  ASSERT_EQ(resource1_->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(resource1_identity.put())),
            S_OK);
  EXPECT_EQ(resource_identity.get(), texture_identity.get());
  EXPECT_EQ(resource1_identity.get(), texture_identity.get());

  ComPtr<IDXGIResource> base_again;
  ASSERT_EQ(
      resource1_->QueryInterface(__uuidof(IDXGIResource),
                                 reinterpret_cast<void **>(base_again.put())),
      S_OK);
  EXPECT_EQ(base_again.get(), resource_.get());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiResourceObjectContractSpec,
       SharesPrivateDataAcrossD3DAndDxgiInterfaces) {
  constexpr std::array<std::uint8_t, 7> initial = {1, 2, 3, 5, 8, 13, 21};
  constexpr std::array<std::uint8_t, 4> replacement = {0xde, 0xad, 0xbe, 0xef};

  ASSERT_EQ(texture_->SetPrivateData(kResourceDataKey, initial.size(),
                                     initial.data()),
            S_OK);
  std::array<std::uint8_t, initial.size()> actual_initial = {};
  UINT actual_size = actual_initial.size();
  ASSERT_EQ(resource1_->GetPrivateData(kResourceDataKey, &actual_size,
                                       actual_initial.data()),
            S_OK);
  EXPECT_EQ(actual_size, initial.size());
  EXPECT_EQ(actual_initial, initial);

  ASSERT_EQ(resource_->SetPrivateData(kResourceDataKey, replacement.size(),
                                      replacement.data()),
            S_OK);
  std::array<std::uint8_t, replacement.size()> actual_replacement = {};
  actual_size = actual_replacement.size();
  ASSERT_EQ(texture_->GetPrivateData(kResourceDataKey, &actual_size,
                                     actual_replacement.data()),
            S_OK);
  EXPECT_EQ(actual_size, replacement.size());
  EXPECT_EQ(actual_replacement, replacement);

  ASSERT_EQ(resource1_->SetPrivateData(kResourceDataKey, 0, nullptr), S_OK);
  actual_size = actual_replacement.size();
  EXPECT_EQ(texture_->GetPrivateData(kResourceDataKey, &actual_size,
                                     actual_replacement.data()),
            DXGI_ERROR_NOT_FOUND);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiResourceObjectContractSpec,
       NonSharedTextureReportsNoLegacyOrNtHandle) {
  HANDLE legacy_handle = reinterpret_cast<HANDLE>(uintptr_t{1});
  EXPECT_EQ(resource_->GetSharedHandle(&legacy_handle), S_OK);
  EXPECT_EQ(legacy_handle, nullptr);

  HANDLE nt_handle = reinterpret_cast<HANDLE>(uintptr_t{1});
  EXPECT_EQ(resource1_->CreateSharedHandle(nullptr,
                                           GENERIC_ALL |
                                               DXGI_SHARED_RESOURCE_READ |
                                               DXGI_SHARED_RESOURCE_WRITE,
                                           nullptr, &nt_handle),
            E_INVALIDARG);
  EXPECT_EQ(nt_handle, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiResourceObjectContractSpec,
       NonSharedTextureRejectsKeyedMutexInterface) {
  void *output = reinterpret_cast<void *>(uintptr_t{1});
  EXPECT_EQ(texture_->QueryInterface(__uuidof(IDXGIKeyedMutex), &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
