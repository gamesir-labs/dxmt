#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Plan §11.5: GetTimestampFrequency / GetClockCalibration queue matrix.
// Public D3D12 API only.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct QueueTypeCase {
  D3D12_COMMAND_LIST_TYPE type;
  bool expect_timestamp_ok;
};

std::vector<QueueTypeCase> BuildQueueTypeCases() {
  return {
      {D3D12_COMMAND_LIST_TYPE_DIRECT, true},
      {D3D12_COMMAND_LIST_TYPE_COMPUTE, true},
      // Copy queues often lack timestamps; accept success or documented failure.
      {D3D12_COMMAND_LIST_TYPE_COPY, false},
  };
}

class TimestampFrequencyMatrixSpec
    : public ::testing::TestWithParam<QueueTypeCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(TimestampFrequencyMatrixSpec, FrequencyQueryIsCoherent) {
  const auto &test = GetParam();
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = test.type;
  ComPtr<ID3D12CommandQueue> queue;
  ASSERT_EQ(context_.device()->CreateCommandQueue(
                &desc, IID_PPV_ARGS(queue.put())),
            S_OK);
  UINT64 frequency = 0;
  const HRESULT hr = queue->GetTimestampFrequency(&frequency);
  if (test.expect_timestamp_ok) {
    ASSERT_EQ(hr, S_OK) << "type=" << static_cast<UINT>(test.type);
    EXPECT_GT(frequency, 0ull);
    UINT64 frequency2 = 0;
    ASSERT_EQ(queue->GetTimestampFrequency(&frequency2), S_OK);
    EXPECT_EQ(frequency, frequency2);
  } else {
    // Copy queue: either reports a frequency or fails cleanly.
    if (SUCCEEDED(hr))
      EXPECT_GT(frequency, 0ull);
    else
      EXPECT_HRESULT_FAILED(hr);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(TimestampFrequencyMatrixSpec, ClockCalibrationPairIsCoherent) {
  const auto &test = GetParam();
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = test.type;
  ComPtr<ID3D12CommandQueue> queue;
  ASSERT_EQ(context_.device()->CreateCommandQueue(
                &desc, IID_PPV_ARGS(queue.put())),
            S_OK);
  UINT64 gpu = 0, cpu = 0;
  const HRESULT hr = queue->GetClockCalibration(&gpu, &cpu);
  if (SUCCEEDED(hr)) {
    // A second sample should not go backwards on the CPU side.
    UINT64 gpu2 = 0, cpu2 = 0;
    ASSERT_EQ(queue->GetClockCalibration(&gpu2, &cpu2), S_OK);
    EXPECT_GE(cpu2, cpu);
    EXPECT_GE(gpu2, gpu);
  } else if (!test.expect_timestamp_ok) {
    EXPECT_HRESULT_FAILED(hr);
  } else {
    // Direct/compute should support calibration when timestamps work.
    EXPECT_EQ(hr, S_OK);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string QueueTypeName(const ::testing::TestParamInfo<QueueTypeCase> &info) {
  return "T" + std::to_string(static_cast<UINT>(info.param.type));
}

INSTANTIATE_TEST_SUITE_P(QueueMatrix, TimestampFrequencyMatrixSpec,
                         ::testing::ValuesIn(BuildQueueTypeCases()),
                         QueueTypeName);

} // namespace
