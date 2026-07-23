#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// Public D3D11.1 NT-handle sharing coverage. Each test owns its texture,
// anonymous NT handle, devices, and keyed mutex state. Zero-timeout probes make
// lock ownership deterministic without blocking or sharing synchronization
// state with parallel workers.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct SharedNtInterfaceCase {
  const GUID *iid;
  HRESULT expected;
  const char *name;
};

constexpr std::array kSharedNtInterfaceCases = {
    SharedNtInterfaceCase{&__uuidof(IUnknown), S_OK, "IUnknown"},
    SharedNtInterfaceCase{&__uuidof(ID3D11DeviceChild), S_OK, "DeviceChild"},
    SharedNtInterfaceCase{&__uuidof(ID3D11Resource), S_OK, "Resource"},
    SharedNtInterfaceCase{&__uuidof(ID3D11Texture2D), S_OK, "Texture2D"},
    SharedNtInterfaceCase{&__uuidof(IDXGIResource1), S_OK, "DxgiResource1"},
    SharedNtInterfaceCase{&__uuidof(ID3D11Buffer), E_NOINTERFACE,
                          "UnsupportedBuffer"},
};

const dxmt::test::LogicalCaseFamilyRegistration kSharedNtInterfaceRegistration(
    "D3D11SharedNtHandleContractSpec.OpensNtHandleByPublicInterface",
    "D3D11.SharedResource.NtHandle.Interface.", kSharedNtInterfaceCases.size(),
    1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "D3D11_RESOURCE_MISC_SHARED_NTHANDLE,IDXGIResource1,"
      "CreateSharedHandle,OpenSharedResource1,ID3D11Resource,"
      "ID3D11Texture2D,QueryInterface"},
     dxmt::test::kResourceTestCost,
     "two test-local D3D11 devices, one shared texture, and one scoped NT "
     "handle per physical test",
     "open one anonymous NT shared handle through each public resource "
     "interface plus one unrelated interface",
     "supported interfaces expose the shared texture; the unrelated "
     "interface returns E_NOINTERFACE and no object",
     "logical ID, interface, open HRESULT, COM object, device health, and "
     "exact replay argument"});

const dxmt::test::TestCostRegistration kOpenNtHandleCost(
    "D3D11SharedNtHandleContractSpec.OpensAnonymousNtHandleOnSecondDevice",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kOpenNtInterfaceCost(
    "D3D11SharedNtHandleContractSpec.OpensNtHandleByPublicInterface",
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
const dxmt::test::TestCostRegistration kTextureContentsCost(
    "D3D11SharedNtHandleContractSpec.SharesTextureContentsBidirectionally",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kNtHandleTypeCost(
    "D3D11SharedNtHandleContractSpec.RejectsNtHandleInLegacyOpenMethod",
    dxmt::test::kResourceTestCost);

HRESULT ReadTextureContents(ID3D11Device *device, ID3D11DeviceContext *context,
                            ID3D11Texture2D *texture,
                            const D3D11_TEXTURE2D_DESC &source_desc,
                            std::vector<std::uint32_t> *contents) {
  D3D11_TEXTURE2D_DESC staging_desc = source_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;

  ComPtr<ID3D11Texture2D> staging;
  HRESULT hr = device->CreateTexture2D(&staging_desc, nullptr, staging.put());
  if (FAILED(hr))
    return hr;

  context->CopyResource(staging.get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;

  contents->resize(source_desc.Width * source_desc.Height);
  const std::size_t row_size = source_desc.Width * sizeof(std::uint32_t);
  for (UINT y = 0; y < source_desc.Height; ++y) {
    std::memcpy(contents->data() + y * source_desc.Width,
                static_cast<const std::uint8_t *>(mapped.pData) +
                    y * mapped.RowPitch,
                row_size);
  }
  context->Unmap(staging.get(), 0);
  return S_OK;
}

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

TEST_F(D3D11SharedNtHandleContractSpec, OpensNtHandleByPublicInterface) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kSharedNtInterfaceCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(
            kSharedNtInterfaceRegistration.family(), logical))
      continue;
    ++selected_count;
    const SharedNtInterfaceCase &test_case = kSharedNtInterfaceCases[logical];
    const auto case_id = dxmt::test::LogicalCaseId(
        kSharedNtInterfaceRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message() << "LogicalCaseId: " << case_id
                                      << " interface=" << test_case.name
                                      << " handle=" << shared_handle_.handle
                                      << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<IUnknown> opened;
    const HRESULT open_result = second_device1_->OpenSharedResource1(
        shared_handle_.handle, *test_case.iid,
        reinterpret_cast<void **>(opened.put()));
    EXPECT_EQ(open_result, test_case.expected);
    if (FAILED(test_case.expected)) {
      EXPECT_EQ(opened.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(opened);

    ComPtr<ID3D11Texture2D> opened_texture;
    ASSERT_EQ(
        opened->QueryInterface(__uuidof(ID3D11Texture2D),
                               reinterpret_cast<void **>(opened_texture.put())),
        S_OK);
    D3D11_TEXTURE2D_DESC opened_desc = {};
    opened_texture->GetDesc(&opened_desc);
    EXPECT_EQ(opened_desc.Width, desc_.Width);
    EXPECT_EQ(opened_desc.Height, desc_.Height);
    EXPECT_EQ(opened_desc.Format, desc_.Format);
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kSharedNtInterfaceRegistration.family().case_id_prefix);
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
  EXPECT_EQ(opened_mutex_->ReleaseSync(5), DXGI_ERROR_INVALID_CALL);
  ASSERT_EQ(creator_mutex_->AcquireSync(0, 0), S_OK);
  ASSERT_EQ(creator_mutex_->ReleaseSync(0), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNtHandleContractSpec, SharesTextureContentsBidirectionally) {
  const std::size_t pixel_count = desc_.Width * desc_.Height;
  std::vector<std::uint32_t> creator_contents(pixel_count);
  std::vector<std::uint32_t> opened_contents(pixel_count);
  for (UINT y = 0; y < desc_.Height; ++y) {
    for (UINT x = 0; x < desc_.Width; ++x) {
      const std::size_t index = y * desc_.Width + x;
      creator_contents[index] =
          0xff000000u | (x * 7u) | (y * 11u << 8u) | ((x + y) * 5u << 16u);
      opened_contents[index] =
          0xff000000u | (y * 13u) | (x * 3u << 8u) | ((x * 2u + y * 3u) << 16u);
    }
  }

  ASSERT_EQ(creator_mutex_->AcquireSync(0, 0), S_OK);
  context_.context()->UpdateSubresource(texture_.get(), 0, nullptr,
                                        creator_contents.data(),
                                        desc_.Width * sizeof(std::uint32_t), 0);
  context_.context()->Flush();
  ASSERT_EQ(creator_mutex_->ReleaseSync(21), S_OK);

  ASSERT_EQ(opened_mutex_->AcquireSync(21, 0), S_OK);
  std::vector<std::uint32_t> actual;
  ASSERT_EQ(ReadTextureContents(second_device_.get(), second_context_.get(),
                                opened_texture_.get(), desc_, &actual),
            S_OK);
  EXPECT_EQ(actual, creator_contents);
  second_context_->UpdateSubresource(opened_texture_.get(), 0, nullptr,
                                     opened_contents.data(),
                                     desc_.Width * sizeof(std::uint32_t), 0);
  second_context_->Flush();
  ASSERT_EQ(opened_mutex_->ReleaseSync(34), S_OK);

  ASSERT_EQ(creator_mutex_->AcquireSync(34, 0), S_OK);
  ASSERT_EQ(ReadTextureContents(context_.device(), context_.context(),
                                texture_.get(), desc_, &actual),
            S_OK);
  EXPECT_EQ(actual, opened_contents);
  ASSERT_EQ(creator_mutex_->ReleaseSync(0), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNtHandleContractSpec, RejectsNtHandleInLegacyOpenMethod) {
  ComPtr<ID3D11Texture2D> opened;
  EXPECT_EQ(second_device_->OpenSharedResource(
                shared_handle_.handle, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void **>(opened.put())),
            E_INVALIDARG);
  EXPECT_EQ(opened.get(), nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

} // namespace
