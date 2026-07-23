#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>
#include <dxgi.h>

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
  HRESULT expected;
  const char *name;
};

constexpr std::array kFenceAccessCases = {
    FenceAccessCase{GENERIC_ALL, S_OK, "GenericAll"},
    FenceAccessCase{0, DXGI_ERROR_INVALID_CALL, "Zero"},
    FenceAccessCase{GENERIC_READ, DXGI_ERROR_INVALID_CALL, "GenericRead"},
    FenceAccessCase{GENERIC_WRITE, DXGI_ERROR_INVALID_CALL, "GenericWrite"},
    FenceAccessCase{GENERIC_READ | GENERIC_WRITE, DXGI_ERROR_INVALID_CALL,
                    "GenericReadWrite"},
};

enum class InvalidHandleKind {
  Null,
  Event,
  LegacyTexture,
};

struct InvalidHandleCase {
  InvalidHandleKind kind;
  const char *name;
};

constexpr std::array kInvalidHandleCases = {
    InvalidHandleCase{InvalidHandleKind::Null, "Null"},
    InvalidHandleCase{InvalidHandleKind::Event, "Event"},
    InvalidHandleCase{InvalidHandleKind::LegacyTexture, "LegacyTexture"},
};

const dxmt::test::LogicalCaseFamilyRegistration kFenceAccessRegistration(
    "D3D11FenceSharedHandleContractSpec.AcceptsOnlyGenericAllAccess",
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
     "GENERIC_ALL creates a valid handle; every other access mask returns "
     "DXGI_ERROR_INVALID_CALL without producing a handle",
     "logical ID, access mask, HRESULT, handle value, device health, and "
     "exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kInvalidHandleRegistration(
    "D3D11FenceSharedHandleContractSpec.RejectsNonFenceHandles",
    "D3D11.Fence.Open.InvalidHandle.", kInvalidHandleCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device5,OpenSharedFence,HANDLE,CreateEventW,"
      "D3D11_RESOURCE_MISC_SHARED,IDXGIResource,GetSharedHandle"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and, when selected, one scoped event or "
     "legacy shared texture",
     "call OpenSharedFence with a null handle, an event handle, and a legacy "
     "shared-texture KMT handle",
     "every non-fence handle returns E_INVALIDARG and clears the output",
     "logical ID, handle kind and value, HRESULT, output value, device "
     "health, and exact replay argument"});

const dxmt::test::TestCostRegistration kFenceAccessCost(
    "D3D11FenceSharedHandleContractSpec.AcceptsOnlyGenericAllAccess",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kInvalidHandleCost(
    "D3D11FenceSharedHandleContractSpec.RejectsNonFenceHandles",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kOpenOutputCost(
    "D3D11FenceSharedHandleContractSpec.ValidatesOpenOutputAndInterface",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kCreateOutputCost(
    "D3D11FenceSharedHandleContractSpec.RejectsNullSharedHandleOutput",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kDuplicateNameCost(
    "D3D11FenceSharedHandleContractSpec.RejectsDuplicateLiveName",
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

TEST_F(D3D11FenceSharedHandleContractSpec, AcceptsOnlyGenericAllAccess) {
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
    const HRESULT result = fence->CreateSharedHandle(nullptr, test_case.access,
                                                     nullptr, &handle.handle);
    EXPECT_EQ(result, test_case.expected);
    if (SUCCEEDED(test_case.expected))
      EXPECT_NE(handle.handle, nullptr);
    else
      EXPECT_EQ(handle.handle, nullptr);
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
    ComPtr<ID3D11Texture2D> texture;
    HANDLE handle = nullptr;
    if (test_case.kind == InvalidHandleKind::Event) {
      event.handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      ASSERT_NE(event.handle, nullptr);
      handle = event.handle;
    } else if (test_case.kind == InvalidHandleKind::LegacyTexture) {
      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = 16;
      desc.Height = 8;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
      ASSERT_EQ(
          context_.device()->CreateTexture2D(&desc, nullptr, texture.put()),
          S_OK);
      ComPtr<IDXGIResource> resource;
      ASSERT_EQ(
          texture->QueryInterface(__uuidof(IDXGIResource),
                                  reinterpret_cast<void **>(resource.put())),
          S_OK);
      ASSERT_EQ(resource->GetSharedHandle(&handle), S_OK);
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

TEST_F(D3D11FenceSharedHandleContractSpec, ValidatesOpenOutputAndInterface) {
  ComPtr<ID3D11Fence> fence = CreateSharedFence(13);
  ASSERT_TRUE(fence);
  ScopedHandle handle;
  ASSERT_EQ(
      fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle.handle),
      S_OK);
  ASSERT_NE(handle.handle, nullptr);

  EXPECT_EQ(
      device5_->OpenSharedFence(handle.handle, __uuidof(ID3D11Fence), nullptr),
      S_FALSE);

  void *opened = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(
      device5_->OpenSharedFence(handle.handle, __uuidof(ID3D11Buffer), &opened),
      E_NOINTERFACE);
  EXPECT_EQ(opened, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSharedHandleContractSpec, RejectsNullSharedHandleOutput) {
  ComPtr<ID3D11Fence> fence = CreateSharedFence();
  ASSERT_TRUE(fence);
  EXPECT_EQ(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, nullptr),
            DXGI_ERROR_INVALID_CALL);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSharedHandleContractSpec, RejectsDuplicateLiveName) {
  ComPtr<ID3D11Fence> first_fence = CreateSharedFence();
  ComPtr<ID3D11Fence> second_fence = CreateSharedFence();
  ASSERT_TRUE(first_fence);
  ASSERT_TRUE(second_fence);

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
  EXPECT_EQ(duplicate_handle.handle, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
