#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cwchar>

// Public D3D11.1 named-resource coverage. Names combine the worker process ID
// with an atomic per-process sequence, and every NT handle is scoped to its
// fixture. Concurrent workers therefore never share a kernel object name.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

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

struct NamedAccessCase {
  DWORD access;
  HRESULT expected;
  const char *name;
};

constexpr HRESULT kD3dErrNotAvailable = static_cast<HRESULT>(0x8876086au);

constexpr std::array kNamedAccessCases = {
    NamedAccessCase{DXGI_SHARED_RESOURCE_READ, S_OK, "Read"},
    NamedAccessCase{DXGI_SHARED_RESOURCE_WRITE, kD3dErrNotAvailable, "Write"},
    NamedAccessCase{DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                    S_OK, "ReadWrite"},
};

const dxmt::test::LogicalCaseFamilyRegistration kNamedAccessRegistration(
    "D3D11SharedNamedResourceContractSpec."
    "OpensNamedResourceWithDocumentedAccess",
    "D3D11.SharedResource.Named.Access.", kNamedAccessCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "D3D11_RESOURCE_MISC_SHARED_NTHANDLE,IDXGIResource1,"
      "CreateSharedHandle,OpenSharedResourceByName,"
      "DXGI_SHARED_RESOURCE_READ,DXGI_SHARED_RESOURCE_WRITE,"
      "ID3D11Texture2D,ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "two test-local D3D11 devices, one named shared texture, and one scoped "
     "NT handle with a process-unique object name",
     "open one named resource with read, write, and combined documented "
     "access masks on a second device",
     "read and combined access open a texture with the original description "
     "and ownership by the opening device; write-only access returns the "
     "native D3DERR_NOTAVAILABLE result",
     "logical ID, access mask, object name, open HRESULT, description, owner, "
     "and exact replay argument"});

const dxmt::test::TestCostRegistration
    kNamedAccessCost("D3D11SharedNamedResourceContractSpec."
                     "OpensNamedResourceWithDocumentedAccess",
                     dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kNamedRepeatedOpenCost("D3D11SharedNamedResourceContractSpec."
                           "RepeatedOpenByNameCreatesDistinctResourceObjects",
                           dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kNamedCaseCost("D3D11SharedNamedResourceContractSpec."
                   "NameComparisonMatchesNativeCaseInsensitivity",
                   dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kDuplicateNameCost(
    "D3D11SharedNamedResourceContractSpec.RejectsDuplicateLiveName",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kSecondHandleCost("D3D11SharedNamedResourceContractSpec."
                      "CreatesAdditionalAnonymousHandleForSameResource",
                      dxmt::test::kResourceTestCost);

std::atomic<std::uint32_t> g_next_name_id{0};

class D3D11SharedNamedResourceContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
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

    const std::uint32_t name_id =
        g_next_name_id.fetch_add(1, std::memory_order_relaxed);
    const int name_length = std::swprintf(
        shared_name_.data(), shared_name_.size(),
        L"DXMT_D3D11_SharedResource_%lu_%u",
        static_cast<unsigned long>(GetCurrentProcessId()), name_id);
    ASSERT_GT(name_length, 0);
    ASSERT_LT(static_cast<std::size_t>(name_length), shared_name_.size());

    desc_.Width = 24;
    desc_.Height = 12;
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

    ASSERT_EQ(
        texture_->QueryInterface(__uuidof(IDXGIResource1),
                                 reinterpret_cast<void **>(resource1_.put())),
        S_OK);
    ASSERT_EQ(resource1_->CreateSharedHandle(
                  nullptr,
                  DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                  shared_name_.data(), &shared_handle_.handle),
              S_OK);
    ASSERT_NE(shared_handle_.handle, nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device> second_device_;
  ComPtr<ID3D11Device1> second_device1_;
  ComPtr<ID3D11DeviceContext> second_context_;
  ComPtr<ID3D11Texture2D> texture_;
  ComPtr<IDXGIResource1> resource1_;
  D3D11_TEXTURE2D_DESC desc_ = {};
  std::array<wchar_t, MAX_PATH> shared_name_ = {};
  ScopedNtHandle shared_handle_;
};

TEST_F(D3D11SharedNamedResourceContractSpec,
       OpensNamedResourceWithDocumentedAccess) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kNamedAccessCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kNamedAccessRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const NamedAccessCase &test_case = kNamedAccessCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kNamedAccessRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " access=" << test_case.access
                 << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<ID3D11Texture2D> opened;
    const HRESULT open_result = second_device1_->OpenSharedResourceByName(
        shared_name_.data(), test_case.access, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void **>(opened.put()));
    EXPECT_EQ(open_result, test_case.expected);
    if (FAILED(test_case.expected)) {
      EXPECT_EQ(opened.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(opened);

    D3D11_TEXTURE2D_DESC opened_desc = {};
    opened->GetDesc(&opened_desc);
    EXPECT_EQ(opened_desc.Width, desc_.Width);
    EXPECT_EQ(opened_desc.Height, desc_.Height);
    EXPECT_EQ(opened_desc.Format, desc_.Format);
    EXPECT_EQ(opened_desc.BindFlags, desc_.BindFlags);
    EXPECT_EQ(opened_desc.MiscFlags, desc_.MiscFlags);
    ComPtr<ID3D11Device> owner;
    opened->GetDevice(owner.put());
    EXPECT_EQ(owner.get(), second_device_.get());
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kNamedAccessRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNamedResourceContractSpec,
       RepeatedOpenByNameCreatesDistinctResourceObjects) {
  ComPtr<ID3D11Texture2D> first;
  ComPtr<ID3D11Texture2D> second;
  constexpr DWORD kAccess =
      DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
  ASSERT_EQ(second_device1_->OpenSharedResourceByName(
                shared_name_.data(), kAccess, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void **>(first.put())),
            S_OK);
  ASSERT_EQ(second_device1_->OpenSharedResourceByName(
                shared_name_.data(), kAccess, __uuidof(ID3D11Texture2D),
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

TEST_F(D3D11SharedNamedResourceContractSpec,
       NameComparisonMatchesNativeCaseInsensitivity) {
  std::array<wchar_t, MAX_PATH> different_case = shared_name_;
  ASSERT_EQ(different_case[0], L'D');
  different_case[0] = L'd';

  ComPtr<ID3D11Texture2D> opened;
  ASSERT_EQ(second_device1_->OpenSharedResourceByName(
                different_case.data(), DXGI_SHARED_RESOURCE_READ,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void **>(opened.put())),
            S_OK);
  ASSERT_TRUE(opened);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNamedResourceContractSpec, RejectsDuplicateLiveName) {
  ComPtr<ID3D11Texture2D> duplicate_texture;
  ASSERT_EQ(context_.device()->CreateTexture2D(&desc_, nullptr,
                                               duplicate_texture.put()),
            S_OK);
  ComPtr<IDXGIResource1> duplicate_resource1;
  ASSERT_EQ(duplicate_texture->QueryInterface(
                __uuidof(IDXGIResource1),
                reinterpret_cast<void **>(duplicate_resource1.put())),
            S_OK);

  ScopedNtHandle duplicate_handle;
  duplicate_handle.handle = reinterpret_cast<HANDLE>(uintptr_t{1});
  EXPECT_EQ(duplicate_resource1->CreateSharedHandle(
                nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                shared_name_.data(), &duplicate_handle.handle),
            DXGI_ERROR_NAME_ALREADY_EXISTS);
  EXPECT_EQ(duplicate_handle.handle, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11SharedNamedResourceContractSpec,
       CreatesAdditionalAnonymousHandleForSameResource) {
  ScopedNtHandle second_handle;
  ASSERT_EQ(resource1_->CreateSharedHandle(
                nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                nullptr, &second_handle.handle),
            S_OK);
  ASSERT_NE(second_handle.handle, nullptr);

  ComPtr<ID3D11Texture2D> opened;
  ASSERT_EQ(second_device1_->OpenSharedResource1(
                second_handle.handle, __uuidof(ID3D11Texture2D),
                reinterpret_cast<void **>(opened.put())),
            S_OK);
  ASSERT_TRUE(opened);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device_->GetDeviceRemovedReason(), S_OK);
}

} // namespace
