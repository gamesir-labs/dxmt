#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_1.h>
#include <dxgi1_2.h>

// Public D3D11.1 NT-handle sharing coverage. Each test owns its texture,
// anonymous NT handle, devices, and keyed mutex state. Zero-timeout probes make
// lock ownership deterministic without blocking or sharing synchronization
// state with parallel workers.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

const dxmt::test::TestCostRegistration kOpenNtHandleCost(
    "D3D11SharedNtHandleContractSpec.OpensAnonymousNtHandleOnSecondDevice",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kRepeatedOpenCost("D3D11SharedNtHandleContractSpec."
                      "RepeatedOpenCreatesDistinctResourceObjects",
                      dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kKeyOwnershipCost(
    "D3D11SharedNtHandleContractSpec.TransfersKeyOwnershipBetweenDevices",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kInitialKeyCost(
    "D3D11SharedNtHandleContractSpec.InitialStateAcceptsOnlyKeyZero",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kExactReleasedKeyCost(
    "D3D11SharedNtHandleContractSpec.ReleasedStateRequiresExactKey",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kNonOwnerReleaseCost("D3D11SharedNtHandleContractSpec.NonOwnerReleaseFails",
                         dxmt::test::kResourceTestCost);

struct ScopedNtHandle {
  ScopedNtHandle() = default;
  ScopedNtHandle(const ScopedNtHandle &) = delete;
  ScopedNtHandle &operator=(const ScopedNtHandle &) = delete;

  ~ScopedNtHandle() {
    if (handle)
      CloseHandle(handle);
  }

  HANDLE handle = nullptr;
};

class D3D11SharedNtHandleContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device1), reinterpret_cast<void **>(device1_.put())),
        S_OK);

    constexpr D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
    ASSERT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                                nullptr, 0, &kFeatureLevel, 1,
                                D3D11_SDK_VERSION, second_device_.put(),
                                &chosen_level, second_context_.put()),
              S_OK);
    ASSERT_TRUE(second_device_);
    ASSERT_TRUE(second_context_);
    ASSERT_EQ(chosen_level, kFeatureLevel);
    ASSERT_EQ(second_device_->QueryInterface(
                  __uuidof(ID3D11Device1),
                  reinterpret_cast<void **>(second_device1_.put())),
              S_OK);

    desc_.Width = 32;
    desc_.Height = 16;
    desc_.MipLevels = 1;
    desc_.ArraySize = 1;
    desc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc_.SampleDesc.Count = 1;
    desc_.Usage = D3D11_USAGE_DEFAULT;
    desc_.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc_.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                      D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ASSERT_EQ(
        context_.device()->CreateTexture2D(&desc_, nullptr, texture_.put()),
        S_OK);
    ASSERT_TRUE(texture_);
    ASSERT_EQ(texture_->QueryInterface(
                  __uuidof(IDXGIKeyedMutex),
                  reinterpret_cast<void **>(creator_mutex_.put())),
              S_OK);

    ComPtr<IDXGIResource1> resource1;
    ASSERT_EQ(
        texture_->QueryInterface(__uuidof(IDXGIResource1),
                                 reinterpret_cast<void **>(resource1.put())),
        S_OK);
    ASSERT_EQ(resource1->CreateSharedHandle(nullptr,
                                            DXGI_SHARED_RESOURCE_READ |
                                                DXGI_SHARED_RESOURCE_WRITE,
                                            nullptr, &shared_handle_.handle),
              S_OK);
    ASSERT_NE(shared_handle_.handle, nullptr);

    ASSERT_EQ(second_device1_->OpenSharedResource1(
                  shared_handle_.handle, __uuidof(ID3D11Texture2D),
                  reinterpret_cast<void **>(opened_texture_.put())),
              S_OK);
    ASSERT_TRUE(opened_texture_);
    ASSERT_EQ(opened_texture_->QueryInterface(
                  __uuidof(IDXGIKeyedMutex),
                  reinterpret_cast<void **>(opened_mutex_.put())),
              S_OK);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device1> device1_;
  ComPtr<ID3D11Device> second_device_;
  ComPtr<ID3D11Device1> second_device1_;
  ComPtr<ID3D11DeviceContext> second_context_;
  ComPtr<ID3D11Texture2D> texture_;
  ComPtr<ID3D11Texture2D> opened_texture_;
  ComPtr<IDXGIKeyedMutex> creator_mutex_;
  ComPtr<IDXGIKeyedMutex> opened_mutex_;
  D3D11_TEXTURE2D_DESC desc_ = {};
  ScopedNtHandle shared_handle_;
};

TEST_F(D3D11SharedNtHandleContractSpec, OpensAnonymousNtHandleOnSecondDevice) {
  D3D11_TEXTURE2D_DESC opened_desc = {};
  opened_texture_->GetDesc(&opened_desc);
  EXPECT_EQ(opened_desc.Width, desc_.Width);
  EXPECT_EQ(opened_desc.Height, desc_.Height);
  EXPECT_EQ(opened_desc.MipLevels, desc_.MipLevels);
  EXPECT_EQ(opened_desc.ArraySize, desc_.ArraySize);
  EXPECT_EQ(opened_desc.Format, desc_.Format);
  EXPECT_EQ(opened_desc.SampleDesc.Count, desc_.SampleDesc.Count);
  EXPECT_EQ(opened_desc.BindFlags, desc_.BindFlags);
  EXPECT_EQ(opened_desc.MiscFlags, desc_.MiscFlags);

  ComPtr<ID3D11Device> owner;
  opened_texture_->GetDevice(owner.put());
  EXPECT_EQ(owner.get(), second_device_.get());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNtHandleContractSpec,
       RepeatedOpenCreatesDistinctResourceObjects) {
  ComPtr<ID3D11Texture2D> first;
  ComPtr<ID3D11Texture2D> second;
  ASSERT_EQ(second_device1_->OpenSharedResource1(
                shared_handle_.handle, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void **>(first.put())),
            S_OK);
  ASSERT_EQ(second_device1_->OpenSharedResource1(
                shared_handle_.handle, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void **>(second.put())),
            S_OK);

  ComPtr<IUnknown> first_identity;
  ComPtr<IUnknown> second_identity;
  ASSERT_EQ(
      first->QueryInterface(__uuidof(IUnknown),
                            reinterpret_cast<void **>(first_identity.put())),
      S_OK);
  ASSERT_EQ(
      second->QueryInterface(__uuidof(IUnknown),
                             reinterpret_cast<void **>(second_identity.put())),
      S_OK);
  EXPECT_NE(first_identity.get(), second_identity.get());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNtHandleContractSpec, TransfersKeyOwnershipBetweenDevices) {
  ASSERT_EQ(creator_mutex_->AcquireSync(0, 0), S_OK);
  EXPECT_EQ(opened_mutex_->AcquireSync(0, 0),
            static_cast<HRESULT>(WAIT_TIMEOUT));
  ASSERT_EQ(creator_mutex_->ReleaseSync(7), S_OK);

  ASSERT_EQ(opened_mutex_->AcquireSync(7, 0), S_OK);
  EXPECT_EQ(creator_mutex_->AcquireSync(7, 0),
            static_cast<HRESULT>(WAIT_TIMEOUT));
  ASSERT_EQ(opened_mutex_->ReleaseSync(13), S_OK);

  ASSERT_EQ(creator_mutex_->AcquireSync(13, 0), S_OK);
  ASSERT_EQ(creator_mutex_->ReleaseSync(0), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNtHandleContractSpec, InitialStateAcceptsOnlyKeyZero) {
  EXPECT_EQ(creator_mutex_->AcquireSync(1, 0),
            static_cast<HRESULT>(WAIT_TIMEOUT));
  ASSERT_EQ(opened_mutex_->AcquireSync(0, 0), S_OK);
  ASSERT_EQ(opened_mutex_->ReleaseSync(0), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNtHandleContractSpec, ReleasedStateRequiresExactKey) {
  ASSERT_EQ(creator_mutex_->AcquireSync(0, 0), S_OK);
  ASSERT_EQ(creator_mutex_->ReleaseSync(9), S_OK);
  EXPECT_EQ(opened_mutex_->AcquireSync(8, 0),
            static_cast<HRESULT>(WAIT_TIMEOUT));
  ASSERT_EQ(opened_mutex_->AcquireSync(9, 0), S_OK);
  ASSERT_EQ(opened_mutex_->ReleaseSync(0), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNtHandleContractSpec, NonOwnerReleaseFails) {
  EXPECT_EQ(opened_mutex_->ReleaseSync(5), E_FAIL);
  ASSERT_EQ(creator_mutex_->AcquireSync(0, 0), S_OK);
  ASSERT_EQ(creator_mutex_->ReleaseSync(0), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

} // namespace
