#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Public D3D12 fence API matrix: CreateFence / Signal / GetCompletedValue /
// SetEventOnCompletion.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct FenceValueCase {
  UINT64 initial;
  UINT64 signal;
};

std::vector<FenceValueCase> BuildFenceValueCases() {
  std::vector<FenceValueCase> cases;
  const UINT64 seeds[] = {0,
                          1,
                          2,
                          31,
                          32,
                          63,
                          64,
                          255,
                          256,
                          1023,
                          1024,
                          (1ull << 32) - 1,
                          (1ull << 32),
                          (1ull << 40),
                          UINT64_MAX - 1};
  for (const UINT64 initial : seeds) {
    cases.push_back({initial, initial});
    if (initial < UINT64_MAX)
      cases.push_back({initial, initial + 1});
    if (initial > 0)
      cases.push_back({initial, initial - 1});
  }
  return cases;
}

class FenceValueMatrixSpec
    : public ::testing::TestWithParam<FenceValueCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(FenceValueMatrixSpec, CreateReportsInitialCompletedValue) {
  const auto &test = GetParam();
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                test.initial, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put())),
            S_OK);
  ASSERT_TRUE(fence);
  EXPECT_EQ(fence->GetCompletedValue(), test.initial);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(FenceValueMatrixSpec, CpuSignalAdvancesCompletedValueMonotonically) {
  const auto &test = GetParam();
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                test.initial, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put())),
            S_OK);
  ASSERT_TRUE(fence);
  ASSERT_EQ(fence->Signal(test.signal), S_OK);
  EXPECT_EQ(fence->GetCompletedValue(), test.signal);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(FenceValueMatrixSpec, SetEventOnCompletionMatchesCompletedValue) {
  const auto &test = GetParam();
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                test.initial, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put())),
            S_OK);
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  const UINT64 wait_value =
      test.initial > 0 ? test.initial : (test.signal > 0 ? test.signal : 0);
  ASSERT_EQ(fence->SetEventOnCompletion(wait_value, event), S_OK);
  if (fence->GetCompletedValue() >= wait_value) {
    EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_OBJECT_0);
  } else {
    EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);
    if (test.signal >= wait_value) {
      ASSERT_EQ(fence->Signal(test.signal), S_OK);
      EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);
    }
  }
  CloseHandle(event);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string FenceValueName(const ::testing::TestParamInfo<FenceValueCase> &info) {
  return "I" + std::to_string(info.param.initial) + "S" +
         std::to_string(info.param.signal) + "N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(ValueMatrix, FenceValueMatrixSpec,
                         ::testing::ValuesIn(BuildFenceValueCases()),
                         FenceValueName);

} // namespace
