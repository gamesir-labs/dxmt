#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_2.h>

#include <array>
#include <cstdint>

// Public shared-resource flag coverage. Every logical case owns its texture,
// handles, and keyed-mutex state, so the matrix is safe under the default
// parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct SharedFlagCase {
  UINT misc_flags;
  HRESULT create_result;
  HRESULT legacy_handle_result;
  HRESULT nt_handle_result;
  HRESULT keyed_mutex_result;
  const char *name;
};

constexpr std::array kSharedFlagCases = {
    SharedFlagCase{D3D11_RESOURCE_MISC_SHARED, S_OK, S_OK, E_INVALIDARG,
                   E_NOINTERFACE, "Legacy"},
    SharedFlagCase{D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, S_OK, S_OK,
                   E_INVALIDARG, S_OK, "LegacyKeyedMutex"},
    SharedFlagCase{D3D11_RESOURCE_MISC_SHARED_NTHANDLE, S_OK, E_INVALIDARG,
                   S_OK, E_NOINTERFACE, "NtHandle"},
    SharedFlagCase{D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                       D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,
                   S_OK, E_INVALIDARG, S_OK, S_OK, "NtHandleKeyedMutex"},
    SharedFlagCase{D3D11_RESOURCE_MISC_SHARED |
                       D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,
                   E_INVALIDARG, E_INVALIDARG, E_INVALIDARG, E_NOINTERFACE,
                   "MutuallyExclusiveLegacyFlags"},
};

const dxmt::test::LogicalCaseFamilyRegistration kSharedFlagRegistration(
    "D3D11SharedResourceFlagContractSpec.ExposesSharingInterfacesByFlag",
    "D3D11.SharedResource.Flags.", kSharedFlagCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateTexture2D,D3D11_RESOURCE_MISC_SHARED,"
      "D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,"
      "D3D11_RESOURCE_MISC_SHARED_NTHANDLE,IDXGIResource,IDXGIResource1,"
      "GetSharedHandle,CreateSharedHandle,IDXGIKeyedMutex"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one texture with scoped NT handle and "
     "keyed-mutex state per selected logical case",
     "create textures with each documented legacy, keyed-mutex, and NT "
     "sharing flag combination plus the mutually exclusive legacy pair",
     "valid combinations expose exactly their documented legacy handle, NT "
     "handle, and keyed-mutex surfaces; the mutually exclusive pair is "
     "rejected",
     "logical ID, flags, creation HRESULT, handle HRESULTs and values, keyed "
     "mutex HRESULT, description, device health, and exact replay argument"});

const dxmt::test::TestCostRegistration kSharedFlagCost(
    "D3D11SharedResourceFlagContractSpec.ExposesSharingInterfacesByFlag",
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

class D3D11SharedResourceFlagContractSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D11_TEXTURE2D_DESC TextureDesc(UINT misc_flags) const {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 20;
    desc.Height = 10;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = misc_flags;
    return desc;
  }

  D3D11TestContext context_;
};

TEST_F(D3D11SharedResourceFlagContractSpec, ExposesSharingInterfacesByFlag) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kSharedFlagCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kSharedFlagRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const SharedFlagCase &test_case = kSharedFlagCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kSharedFlagRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " misc_flags=" << test_case.misc_flags
                 << " Replay: --dxmt-case-id=" << case_id);

    const D3D11_TEXTURE2D_DESC desc = TextureDesc(test_case.misc_flags);
    ComPtr<ID3D11Texture2D> texture;
    const HRESULT create_result =
        context_.device()->CreateTexture2D(&desc, nullptr, texture.put());
    EXPECT_EQ(create_result, test_case.create_result);
    if (FAILED(test_case.create_result)) {
      EXPECT_EQ(texture.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(texture);

    D3D11_TEXTURE2D_DESC actual_desc = {};
    texture->GetDesc(&actual_desc);
    EXPECT_EQ(actual_desc.MiscFlags, test_case.misc_flags);

    ComPtr<IDXGIResource> resource;
    ComPtr<IDXGIResource1> resource1;
    ASSERT_EQ(
        texture->QueryInterface(__uuidof(IDXGIResource),
                                reinterpret_cast<void **>(resource.put())),
        S_OK);
    ASSERT_EQ(
        texture->QueryInterface(__uuidof(IDXGIResource1),
                                reinterpret_cast<void **>(resource1.put())),
        S_OK);

    HANDLE legacy_handle = nullptr;
    EXPECT_EQ(resource->GetSharedHandle(&legacy_handle),
              test_case.legacy_handle_result);
    if (SUCCEEDED(test_case.legacy_handle_result)) {
      EXPECT_NE(legacy_handle, nullptr);
    }

    ScopedNtHandle nt_handle;
    EXPECT_EQ(resource1->CreateSharedHandle(nullptr,
                                            DXGI_SHARED_RESOURCE_READ |
                                                DXGI_SHARED_RESOURCE_WRITE,
                                            nullptr, &nt_handle.handle),
              test_case.nt_handle_result);
    if (SUCCEEDED(test_case.nt_handle_result))
      EXPECT_NE(nt_handle.handle, nullptr);
    else
      EXPECT_EQ(nt_handle.handle, nullptr);

    ComPtr<IDXGIKeyedMutex> keyed_mutex;
    EXPECT_EQ(
        texture->QueryInterface(__uuidof(IDXGIKeyedMutex),
                                reinterpret_cast<void **>(keyed_mutex.put())),
        test_case.keyed_mutex_result);
    if (SUCCEEDED(test_case.keyed_mutex_result)) {
      ASSERT_TRUE(keyed_mutex);
      ASSERT_EQ(keyed_mutex->AcquireSync(0, 0), S_OK);
      ASSERT_EQ(keyed_mutex->ReleaseSync(0), S_OK);
    } else {
      EXPECT_EQ(keyed_mutex.get(), nullptr);
    }
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kSharedFlagRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
