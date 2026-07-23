#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

// Public D3D11.4 fence-object coverage. Each case owns its device, fence,
// events, and handles, so no queue ordering or process-global state is shared
// between parallel scheduler workers.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::array<UINT64, 4> kInitialValues = {
    0,
    1,
    42,
    std::numeric_limits<UINT64>::max() - 1,
};

constexpr std::uint32_t kFenceCaseCount = kInitialValues.size();

const dxmt::test::LogicalCaseFamilyRegistration kFenceCases(
    "D3D11FenceContractSpec.PreservesInitialValuesAndComContracts",
    "D3D11.Fence.Create.InitialValue.", kFenceCaseCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device5,CreateFence,ID3D11FenceGetCompletedValue,"
      "ID3D11DeviceChildGetDevice,QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live unshared fence per selected "
     "logical case",
     "create fences at zero, small, ordinary, and near-maximum initial values "
     "through ID3D11Device5 and inspect their public interfaces",
     "every fence exposes its exact initial completed value, returns the "
     "creating device, and preserves one COM identity across fence and "
     "device-child interfaces",
     "logical ID, selected-case count, initial and completed values, creation "
     "and QI HRESULTs, object and owner addresses, COM identities, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kFenceCost("D3D11FenceContractSpec.PreservesInitialValuesAndComContracts",
               dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kFenceHandleInheritanceCost(
    "D3D11FenceContractSpec.CreatesSharedHandlesWithRequestedInheritance",
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

class D3D11FenceContractSpec : public ::testing::Test {
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

  ComPtr<ID3D11Fence> CreateFence(UINT64 initial_value) {
    ComPtr<ID3D11Fence> fence;
    EXPECT_EQ(device5_->CreateFence(initial_value, D3D11_FENCE_FLAG_NONE,
                                    __uuidof(ID3D11Fence),
                                    reinterpret_cast<void **>(fence.put())),
              S_OK);
    return fence;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device5> device5_;
};

TEST_F(D3D11FenceContractSpec, PreservesInitialValuesAndComContracts) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kFenceCaseCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kFenceCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix", kFenceCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const UINT64 initial_value = kInitialValues[logical];
    ComPtr<ID3D11Fence> fence;
    const HRESULT create_result = device5_->CreateFence(
        initial_value, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence),
        reinterpret_cast<void **>(fence.put()));

    UINT64 completed_value = 0;
    ComPtr<ID3D11Device> owner;
    ComPtr<ID3D11DeviceChild> child;
    ComPtr<IUnknown> fence_identity;
    ComPtr<IUnknown> child_identity;
    HRESULT child_result = E_FAIL;
    HRESULT fence_identity_result = E_FAIL;
    HRESULT child_identity_result = E_FAIL;
    if (create_result == S_OK && fence) {
      completed_value = fence->GetCompletedValue();
      fence->GetDevice(owner.put());
      child_result = fence->QueryInterface(
          __uuidof(ID3D11DeviceChild), reinterpret_cast<void **>(child.put()));
      fence_identity_result = fence->QueryInterface(
          __uuidof(IUnknown), reinterpret_cast<void **>(fence_identity.put()));
      if (child_result == S_OK && child) {
        child_identity_result = child->QueryInterface(
            __uuidof(IUnknown),
            reinterpret_cast<void **>(child_identity.put()));
      }
    }

    const bool valid =
        create_result == S_OK && fence && completed_value == initial_value &&
        owner.get() == context_.device() && child_result == S_OK && child &&
        fence_identity_result == S_OK && fence_identity &&
        child_identity_result == S_OK &&
        child_identity.get() == fence_identity.get();
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kFenceCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kFenceCases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " initial_value=" << initial_value
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: create_hresult=" << S_OK
                  << " child_hresult=" << S_OK << " identity_hresult=" << S_OK
                  << " completed_value=" << initial_value
                  << " owner=" << context_.device() << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " child_hresult=" << child_result
                  << " fence_identity_hresult=" << fence_identity_result
                  << " child_identity_hresult=" << child_identity_result
                  << " completed_value=" << completed_value
                  << " fence=" << fence.get() << " child=" << child.get()
                  << " fence_identity=" << fence_identity.get()
                  << " child_identity=" << child_identity.get()
                  << " owner=" << owner.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceContractSpec, RejectsUnsupportedInterface) {
  void *fence = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device5_->CreateFence(0, D3D11_FENCE_FLAG_NONE,
                                  __uuidof(ID3D11Buffer), &fence),
            E_NOINTERFACE);
  EXPECT_EQ(fence, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceContractSpec, RejectsSharedHandleForUnsharedFence) {
  ComPtr<ID3D11Fence> fence = CreateFence(7);
  ASSERT_NE(fence.get(), nullptr);

  HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceContractSpec, CreatesSharedHandlesWithRequestedInheritance) {
  ComPtr<ID3D11Fence> fence;
  ASSERT_EQ(device5_->CreateFence(7, D3D11_FENCE_FLAG_SHARED,
                                  __uuidof(ID3D11Fence),
                                  reinterpret_cast<void **>(fence.put())),
            S_OK);
  ASSERT_TRUE(fence);

  ScopedHandle default_handle;
  ASSERT_EQ(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                      &default_handle.handle),
            S_OK);
  ASSERT_NE(default_handle.handle, nullptr);
  DWORD handle_flags = HANDLE_FLAG_INHERIT;
  ASSERT_TRUE(GetHandleInformation(default_handle.handle, &handle_flags));
  EXPECT_EQ(handle_flags & HANDLE_FLAG_INHERIT, 0u);

  SECURITY_ATTRIBUTES attributes = {};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  ScopedHandle inherited_handle;
  ASSERT_EQ(fence->CreateSharedHandle(&attributes, GENERIC_ALL, nullptr,
                                      &inherited_handle.handle),
            S_OK);
  ASSERT_NE(inherited_handle.handle, nullptr);
  handle_flags = 0;
  ASSERT_TRUE(GetHandleInformation(inherited_handle.handle, &handle_flags));
  EXPECT_EQ(handle_flags & HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
