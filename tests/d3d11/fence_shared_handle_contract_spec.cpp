#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cwchar>

// Public D3D11.4 shared-fence handle coverage. Every case owns its fence,
// texture, event, and NT handles, so no kernel object or queue state is shared
// between default parallel scheduler workers.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct FenceAccessCase {
  DWORD access;
  const char *name;
};

struct FenceInterfaceCase {
  const GUID *iid;
  HRESULT expected;
  const char *name;
};

constexpr std::array kFenceAccessCases = {
    FenceAccessCase{GENERIC_ALL, "GenericAll"},
    FenceAccessCase{0, "Zero"},
    FenceAccessCase{GENERIC_READ, "GenericRead"},
    FenceAccessCase{GENERIC_WRITE, "GenericWrite"},
    FenceAccessCase{GENERIC_READ | GENERIC_WRITE, "GenericReadWrite"},
};

constexpr std::array kFenceInterfaceCases = {
    FenceInterfaceCase{&__uuidof(IUnknown), S_OK, "IUnknown"},
    FenceInterfaceCase{&__uuidof(ID3D11DeviceChild), S_OK, "DeviceChild"},
    FenceInterfaceCase{&__uuidof(ID3D11Fence), S_OK, "Fence"},
    FenceInterfaceCase{&__uuidof(ID3D11Buffer), E_NOINTERFACE,
                       "UnsupportedBuffer"},
};

enum class InvalidHandleKind {
  Null,
  Event,
  LegacyTexture,
  NtTexture,
};

struct InvalidHandleCase {
  InvalidHandleKind kind;
  const char *name;
};

constexpr std::array kInvalidHandleCases = {
    InvalidHandleCase{InvalidHandleKind::Null, "Null"},
    InvalidHandleCase{InvalidHandleKind::Event, "Event"},
    InvalidHandleCase{InvalidHandleKind::LegacyTexture, "LegacyTexture"},
    InvalidHandleCase{InvalidHandleKind::NtTexture, "NtTexture"},
};

const dxmt::test::LogicalCaseFamilyRegistration kFenceAccessRegistration(
    "D3D11FenceSharedHandleContractSpec.AcceptsAccessMasks",
    "D3D11.Fence.SharedHandle.Access.", kFenceAccessCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device5,CreateFence,D3D11_FENCE_FLAG_SHARED,ID3D11Fence,"
      "CreateSharedHandle,GENERIC_ALL,GENERIC_READ,GENERIC_WRITE"},
     dxmt::test::kResourceTestCost,
     "one test-local shared D3D11 fence and at most one scoped NT handle per "
     "selected logical case",
     "request a shared fence handle with GENERIC_ALL, zero, read, write, and "
     "combined read/write access masks",
     "each access mask creates a valid shared fence handle",
     "logical ID, access mask, HRESULT, handle value, device health, and "
     "exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kInvalidHandleRegistration(
    "D3D11FenceSharedHandleContractSpec.RejectsNonFenceHandles",
    "D3D11.Fence.Open.InvalidHandle.", kInvalidHandleCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device5,OpenSharedFence,HANDLE,CreateEventW,"
      "D3D11_RESOURCE_MISC_SHARED,D3D11_RESOURCE_MISC_SHARED_NTHANDLE,"
      "IDXGIResource,IDXGIResource1,GetSharedHandle,CreateSharedHandle"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and, when selected, one scoped event or "
     "shared texture handle",
     "call OpenSharedFence with a null handle, an event handle, a legacy "
     "shared-texture KMT handle, and a shared-texture NT handle",
     "every non-fence handle returns E_INVALIDARG and clears the output",
     "logical ID, handle kind and value, HRESULT, output value, device "
     "health, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kFenceInterfaceRegistration(
    "D3D11FenceSharedHandleContractSpec.OpensFenceByPublicInterface",
    "D3D11.Fence.Open.Interface.", kFenceInterfaceCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device5,OpenSharedFence,IUnknown,ID3D11DeviceChild,"
      "ID3D11Fence,ID3D11Buffer,GetCompletedValue,GetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one shared fence, and one scoped NT "
     "handle per physical test",
     "open one shared fence through each supported public interface plus one "
     "unrelated interface",
     "supported interfaces expose a fence with the original value and "
     "ownership by the opening device; the unrelated interface returns "
     "E_NOINTERFACE and no object",
     "logical ID, interface, open HRESULT, COM object, completed value, "
     "owner, device health, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kFenceAccessCost("D3D11FenceSharedHandleContractSpec.AcceptsAccessMasks",
                     dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kInvalidHandleCost(
    "D3D11FenceSharedHandleContractSpec.RejectsNonFenceHandles",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kOpenInterfaceCost(
    "D3D11FenceSharedHandleContractSpec.OpensFenceByPublicInterface",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kCreateOutputCost(
    "D3D11FenceSharedHandleContractSpec.RejectsNullSharedHandleOutput",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kDuplicateNameCost("D3D11FenceSharedHandleContractSpec."
                       "RejectsDuplicateLiveNamesCaseInsensitively",
                       dxmt::test::kResourceTestCost);

struct ScopedHandle {
  ScopedHandle() = default;
  ScopedHandle(const ScopedHandle &) = delete;
  ScopedHandle &operator=(const ScopedHandle &) = delete;

  ~ScopedHandle() {
    if (handle)
      CloseHandle(handle);
  }

  HANDLE handle = nullptr;
};

class D3D11FenceSharedHandleContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device5), reinterpret_cast<void **>(device5_.put())),
        S_OK);
  }

  ComPtr<ID3D11Fence> CreateSharedFence(UINT64 initial_value = 0) {
    ComPtr<ID3D11Fence> fence;
    EXPECT_EQ(device5_->CreateFence(initial_value, D3D11_FENCE_FLAG_SHARED,
                                    __uuidof(ID3D11Fence),
                                    reinterpret_cast<void **>(fence.put())),
              S_OK);
    return fence;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device5> device5_;
};

TEST_F(D3D11FenceSharedHandleContractSpec, AcceptsAccessMasks) {
  ComPtr<ID3D11Fence> fence = CreateSharedFence(7);
  ASSERT_TRUE(fence);

  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kFenceAccessCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kFenceAccessRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const FenceAccessCase &test_case = kFenceAccessCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kFenceAccessRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " access=" << test_case.access
                 << " Replay: --dxmt-case-id=" << case_id);

    ScopedHandle handle;
    EXPECT_EQ(fence->CreateSharedHandle(nullptr, test_case.access, nullptr,
                                        &handle.handle),
              S_OK);
    EXPECT_NE(handle.handle, nullptr);
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kFenceAccessRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSharedHandleContractSpec, RejectsNonFenceHandles) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kInvalidHandleCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kInvalidHandleRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const InvalidHandleCase &test_case = kInvalidHandleCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kInvalidHandleRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " Replay: --dxmt-case-id=" << case_id);

    ScopedHandle event;
    ScopedHandle nt_texture_handle;
    ComPtr<ID3D11Texture2D> texture;
    HANDLE handle = nullptr;
    if (test_case.kind == InvalidHandleKind::Event) {
      event.handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      ASSERT_NE(event.handle, nullptr);
      handle = event.handle;
    } else if (test_case.kind == InvalidHandleKind::LegacyTexture ||
               test_case.kind == InvalidHandleKind::NtTexture) {
      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = 16;
      desc.Height = 8;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      desc.MiscFlags = test_case.kind == InvalidHandleKind::LegacyTexture
                           ? D3D11_RESOURCE_MISC_SHARED
                           : D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                                 D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
      ASSERT_EQ(
          context_.device()->CreateTexture2D(&desc, nullptr, texture.put()),
          S_OK);
      if (test_case.kind == InvalidHandleKind::LegacyTexture) {
        ComPtr<IDXGIResource> resource;
        ASSERT_EQ(
            texture->QueryInterface(__uuidof(IDXGIResource),
                                    reinterpret_cast<void **>(resource.put())),
            S_OK);
        ASSERT_EQ(resource->GetSharedHandle(&handle), S_OK);
      } else {
        ComPtr<IDXGIResource1> resource;
        ASSERT_EQ(
            texture->QueryInterface(__uuidof(IDXGIResource1),
                                    reinterpret_cast<void **>(resource.put())),
            S_OK);
        ASSERT_EQ(resource->CreateSharedHandle(
                      nullptr,
                      DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                      nullptr, &nt_texture_handle.handle),
                  S_OK);
        handle = nt_texture_handle.handle;
      }
      ASSERT_NE(handle, nullptr);
    }

    void *opened = reinterpret_cast<void *>(std::uintptr_t{1});
    EXPECT_EQ(device5_->OpenSharedFence(handle, __uuidof(ID3D11Fence), &opened),
              E_INVALIDARG);
    EXPECT_EQ(opened, nullptr);
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kInvalidHandleRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSharedHandleContractSpec, OpensFenceByPublicInterface) {
  ComPtr<ID3D11Fence> fence = CreateSharedFence(13);
  ASSERT_TRUE(fence);
  ScopedHandle handle;
  ASSERT_EQ(
      fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle.handle),
      S_OK);
  ASSERT_NE(handle.handle, nullptr);

  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kFenceInterfaceCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kFenceInterfaceRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const FenceInterfaceCase &test_case = kFenceInterfaceCases[logical];
    const auto case_id = dxmt::test::LogicalCaseId(
        kFenceInterfaceRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " Replay: --dxmt-case-id=" << case_id);

    void *opened = reinterpret_cast<void *>(std::uintptr_t{1});
    const HRESULT open_result =
        device5_->OpenSharedFence(handle.handle, *test_case.iid, &opened);
    EXPECT_EQ(open_result, test_case.expected);
    if (FAILED(test_case.expected)) {
      EXPECT_EQ(opened, nullptr);
      continue;
    }
    ASSERT_NE(opened, nullptr);
    ComPtr<IUnknown> object(reinterpret_cast<IUnknown *>(opened));
    ComPtr<ID3D11Fence> opened_fence;
    ASSERT_EQ(
        object->QueryInterface(__uuidof(ID3D11Fence),
                               reinterpret_cast<void **>(opened_fence.put())),
        S_OK);
    EXPECT_EQ(opened_fence->GetCompletedValue(), 13u);
    ComPtr<ID3D11Device> owner;
    opened_fence->GetDevice(owner.put());
    EXPECT_EQ(owner.get(), context_.device());
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kFenceInterfaceRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSharedHandleContractSpec, RejectsNullSharedHandleOutput) {
  ComPtr<ID3D11Fence> fence = CreateSharedFence();
  ASSERT_TRUE(fence);
  EXPECT_EQ(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSharedHandleContractSpec,
       RejectsDuplicateLiveNamesCaseInsensitively) {
  ComPtr<ID3D11Fence> first_fence = CreateSharedFence();
  ComPtr<ID3D11Fence> second_fence = CreateSharedFence();
  ComPtr<ID3D11Fence> variant_fence = CreateSharedFence();
  ASSERT_TRUE(first_fence);
  ASSERT_TRUE(second_fence);
  ASSERT_TRUE(variant_fence);

  std::array<wchar_t, MAX_PATH> shared_name = {};
  const auto name_id = static_cast<unsigned long long>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const int name_length =
      std::swprintf(shared_name.data(), shared_name.size(),
                    L"DXMT_D3D11_FenceDuplicate_%lu_%llu",
                    static_cast<unsigned long>(GetCurrentProcessId()), name_id);
  ASSERT_GT(name_length, 0);
  ASSERT_LT(static_cast<std::size_t>(name_length), shared_name.size());

  ScopedHandle first_handle;
  ASSERT_EQ(first_fence->CreateSharedHandle(
                nullptr, GENERIC_ALL, shared_name.data(), &first_handle.handle),
            S_OK);
  ASSERT_NE(first_handle.handle, nullptr);

  ScopedHandle duplicate_handle;
  duplicate_handle.handle = reinterpret_cast<HANDLE>(std::uintptr_t{1});
  EXPECT_EQ(second_fence->CreateSharedHandle(nullptr, GENERIC_ALL,
                                             shared_name.data(),
                                             &duplicate_handle.handle),
            DXGI_ERROR_NAME_ALREADY_EXISTS);
  EXPECT_EQ(duplicate_handle.handle,
            reinterpret_cast<HANDLE>(std::uintptr_t{1}));
  if (duplicate_handle.handle == reinterpret_cast<HANDLE>(std::uintptr_t{1}))
    duplicate_handle.handle = nullptr;

  std::array<wchar_t, MAX_PATH> case_variant_name = shared_name;
  ASSERT_EQ(case_variant_name[0], L'D');
  case_variant_name[0] = L'd';
  ScopedHandle variant_handle;
  variant_handle.handle = reinterpret_cast<HANDLE>(std::uintptr_t{1});
  EXPECT_EQ(variant_fence->CreateSharedHandle(nullptr, GENERIC_ALL,
                                              case_variant_name.data(),
                                              &variant_handle.handle),
            DXGI_ERROR_NAME_ALREADY_EXISTS);
  EXPECT_EQ(variant_handle.handle, reinterpret_cast<HANDLE>(std::uintptr_t{1}));
  if (variant_handle.handle == reinterpret_cast<HANDLE>(std::uintptr_t{1}))
    variant_handle.handle = nullptr;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
