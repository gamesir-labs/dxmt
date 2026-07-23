#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <array>
#include <cstdint>

// Public D3D11.4 fence-flag coverage. Every logical case owns its fence and
// shared handle, so it is safe under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct FenceFlagCase {
  D3D11_FENCE_FLAG flags;
  HRESULT create_result;
  HRESULT shared_handle_result;
  const char *name;
};

constexpr std::array kFenceFlagCases = {
    FenceFlagCase{D3D11_FENCE_FLAG_NONE, S_OK, E_INVALIDARG, "None"},
    FenceFlagCase{D3D11_FENCE_FLAG_SHARED, S_OK, S_OK, "Shared"},
    FenceFlagCase{D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER, S_OK, S_OK,
                  "SharedCrossAdapter"},
    FenceFlagCase{D3D11_FENCE_FLAG_NON_MONITORED, S_OK, E_INVALIDARG,
                  "NonMonitored"},
    FenceFlagCase{static_cast<D3D11_FENCE_FLAG>(0), E_INVALIDARG, E_INVALIDARG,
                  "Zero"},
    FenceFlagCase{static_cast<D3D11_FENCE_FLAG>(0x10), E_INVALIDARG,
                  E_INVALIDARG, "UnknownLowBit"},
    FenceFlagCase{static_cast<D3D11_FENCE_FLAG>(0x80000000u), E_INVALIDARG,
                  E_INVALIDARG, "UnknownHighBit"},
};

const dxmt::test::LogicalCaseFamilyRegistration kFenceFlagRegistration(
    "D3D11FenceFlagContractSpec.CreatesOnlyDefinedFenceModes",
    "D3D11.Fence.Flags.", kFenceFlagCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device5,CreateFence,D3D11_FENCE_FLAG_NONE,"
      "D3D11_FENCE_FLAG_SHARED,D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER,"
      "D3D11_FENCE_FLAG_NON_MONITORED,ID3D11Fence,CreateSharedHandle"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one fence, and at most one scoped NT "
     "handle per selected logical case",
     "create fences with every individually defined D3D11 fence mode plus "
     "zero and unknown low/high flag bits",
     "all defined modes create a fence at the requested initial value, only "
     "shared modes export a handle, and undefined flag values are rejected",
     "logical ID, flags, creation and sharing HRESULTs, completed value, "
     "handle value, device health, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kFenceFlagCost("D3D11FenceFlagContractSpec.CreatesOnlyDefinedFenceModes",
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

class D3D11FenceFlagContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device5), reinterpret_cast<void **>(device5_.put())),
        S_OK);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device5> device5_;
};

TEST_F(D3D11FenceFlagContractSpec, CreatesOnlyDefinedFenceModes) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kFenceFlagCases.size(); ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kFenceFlagRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const FenceFlagCase &test_case = kFenceFlagCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kFenceFlagRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " flags=" << static_cast<UINT>(test_case.flags)
                 << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<ID3D11Fence> fence;
    const HRESULT create_result =
        device5_->CreateFence(23, test_case.flags, __uuidof(ID3D11Fence),
                              reinterpret_cast<void **>(fence.put()));
    EXPECT_EQ(create_result, test_case.create_result);
    if (FAILED(test_case.create_result)) {
      EXPECT_EQ(fence.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(fence);
    EXPECT_EQ(fence->GetCompletedValue(), 23u);

    ScopedHandle handle;
    const HRESULT shared_handle_result = fence->CreateSharedHandle(
        nullptr, GENERIC_ALL, nullptr, &handle.handle);
    EXPECT_EQ(shared_handle_result, test_case.shared_handle_result);
    if (SUCCEEDED(test_case.shared_handle_result))
      EXPECT_NE(handle.handle, nullptr);
    else
      EXPECT_EQ(handle.handle, nullptr);
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kFenceFlagRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
