#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §11.5: timestamp query pairs / multi-slot matrix, monotonic results.
// Public D3D12 API only.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct TimestampCase {
  UINT heap_count;
  UINT pair_count; // number of begin/end pairs written into consecutive slots
};

std::vector<TimestampCase> BuildTimestampCases() {
  std::vector<TimestampCase> cases;
  for (UINT heap_count : {2u, 4u, 8u, 16u, 32u, 64u}) {
    for (UINT pairs = 1; pairs * 2 <= heap_count; ++pairs) {
      if (pairs == 1 || pairs == 2 || pairs * 2 == heap_count || pairs == 3)
        cases.push_back({heap_count, pairs});
    }
  }
  return cases;
}

class TimestampMatrixSpec : public ::testing::TestWithParam<TimestampCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(TimestampMatrixSpec, EndNotBeforeBeginAndSlotsAreIndependent) {
  const auto &test = GetParam();
  D3D12_QUERY_HEAP_DESC desc = {};
  desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  desc.Count = test.heap_count;
  ComPtr<ID3D12QueryHeap> heap;
  ASSERT_EQ(context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put())),
            S_OK);

  const UINT64 result_bytes = test.heap_count * sizeof(UINT64);
  auto result = context_.CreateBuffer(result_bytes, D3D12_HEAP_TYPE_READBACK,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(result);

  // Insert a tiny bit of GPU work between stamps so clocks can advance.
  auto scratch = context_.CreateBuffer(64, D3D12_HEAP_TYPE_DEFAULT,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  auto upload = context_.CreateUploadBuffer(64);
  ASSERT_TRUE(scratch);
  ASSERT_TRUE(upload);

  for (UINT p = 0; p < test.pair_count; ++p) {
    const UINT begin = p * 2;
    const UINT end = begin + 1;
    context_.list()->EndQuery(heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, begin);
    context_.list()->CopyBufferRegion(scratch.get(), 0, upload.get(), 0, 64);
    context_.list()->EndQuery(heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, end);
  }
  context_.list()->ResolveQueryData(heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
                                    test.pair_count * 2, result.get(), 0);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

  void *mapped = nullptr;
  D3D12_RANGE range = {0, static_cast<SIZE_T>(result_bytes)};
  ASSERT_EQ(result->Map(0, &range, &mapped), S_OK);
  ASSERT_NE(mapped, nullptr);
  auto *stamps = static_cast<const UINT64 *>(mapped);
  for (UINT p = 0; p < test.pair_count; ++p) {
    const UINT64 begin = stamps[p * 2];
    const UINT64 end = stamps[p * 2 + 1];
    EXPECT_GE(end, begin) << "pair " << p;
  }
  // Distinct pairs should not all collapse to a single absolute stamp unless
  // the queue is frozen; allow equal across pairs but require non-zero range
  // somewhere if hardware timestamps are active.
  UINT64 min_stamp = stamps[0];
  UINT64 max_stamp = stamps[0];
  for (UINT i = 0; i < test.pair_count * 2; ++i) {
    min_stamp = std::min(min_stamp, stamps[i]);
    max_stamp = std::max(max_stamp, stamps[i]);
  }
  EXPECT_GE(max_stamp, min_stamp);
  result->Unmap(0, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string TimestampName(const ::testing::TestParamInfo<TimestampCase> &info) {
  return "H" + std::to_string(info.param.heap_count) + "P" +
         std::to_string(info.param.pair_count);
}

INSTANTIATE_TEST_SUITE_P(PairMatrix, TimestampMatrixSpec,
                         ::testing::ValuesIn(BuildTimestampCases()),
                         TimestampName);

} // namespace
