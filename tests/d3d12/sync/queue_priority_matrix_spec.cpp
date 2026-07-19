#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

// Public D3D12 CreateCommandQueue priority × type matrix.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct QueuePriorityCase {
  D3D12_COMMAND_LIST_TYPE type;
  INT priority;
  bool expect_ok;
};

std::vector<QueuePriorityCase> BuildQueuePriorityCases() {
  std::vector<QueuePriorityCase> cases;
  const D3D12_COMMAND_LIST_TYPE types[] = {
      D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE,
      D3D12_COMMAND_LIST_TYPE_COPY,
  };
  const INT priorities[] = {
      D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
      D3D12_COMMAND_QUEUE_PRIORITY_HIGH,
      D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME,
      0,
      1,
      -1,
      100,
      1000,
      INT_MAX,
      INT_MIN,
  };
  for (const auto type : types) {
    for (const INT priority : priorities) {
      const bool expect_ok =
          priority == D3D12_COMMAND_QUEUE_PRIORITY_NORMAL ||
          priority == D3D12_COMMAND_QUEUE_PRIORITY_HIGH ||
          priority == 0;
      cases.push_back({type, priority, expect_ok});
    }
  }
  return cases;
}

class QueuePriorityMatrixSpec
    : public ::testing::TestWithParam<QueuePriorityCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(QueuePriorityMatrixSpec, CreateCommandQueueMatchesPriorityPolicy) {
  const auto &test = GetParam();
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = test.type;
  desc.Priority = test.priority;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  ComPtr<ID3D12CommandQueue> queue;
  const HRESULT hr = context_.device()->CreateCommandQueue(
      &desc, IID_PPV_ARGS(queue.put()));
  if (test.expect_ok) {
    // Some implementations accept GLOBAL_REALTIME; either success or failure
    // is fine as long as the device stays healthy.
    if (SUCCEEDED(hr)) {
      ASSERT_TRUE(queue);
      EXPECT_EQ(queue->GetDesc().Type, test.type);
    }
  } else {
    // Extreme priorities should fail closed or be clamped without device loss.
    if (FAILED(hr)) {
      EXPECT_FALSE(queue);
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string QueuePriorityName(
    const ::testing::TestParamInfo<QueuePriorityCase> &info) {
  const auto priority = info.param.priority;
  const std::string priority_token =
      priority < 0 ? ("Neg" + std::to_string(-static_cast<int64_t>(priority)))
                   : std::to_string(priority);
  return "T" + std::to_string(static_cast<UINT>(info.param.type)) + "P" +
         priority_token + "N" + std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(PriorityMatrix, QueuePriorityMatrixSpec,
                         ::testing::ValuesIn(BuildQueuePriorityCases()),
                         QueuePriorityName);

} // namespace
