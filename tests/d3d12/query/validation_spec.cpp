#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <limits>
#include <string>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class QueryValidationSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12QueryHeap> CreateHeap(D3D12_QUERY_HEAP_TYPE type,
                                     UINT count = 4) {
    const D3D12_QUERY_HEAP_DESC desc = {type, count, 0};
    ComPtr<ID3D12QueryHeap> heap;
    EXPECT_EQ(context_.device()->CreateQueryHeap(&desc,
                                                 IID_PPV_ARGS(heap.put())),
              S_OK);
    return heap;
  }

  D3D12TestContext context_;
};


TEST_F(QueryValidationSpec, TimestampEndDoesNotRequireBegin) {
  auto timestamp = CreateHeap(D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1);
  ASSERT_TRUE(timestamp);
  context_.list()->EndQuery(timestamp.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
  EXPECT_EQ(context_.list()->Close(), S_OK);
}

TEST(QueryCrossListSequenceSpec, OcclusionBeginAndEndMaySpanCommandLists) {
  D3D12TestContext first;
  ASSERT_EQ(first.Initialize(), S_OK);
  const D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 1, 0};
  ComPtr<ID3D12QueryHeap> heap;
  ASSERT_EQ(first.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put())),
            S_OK);
  first.list()->BeginQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
  EXPECT_EQ(first.list()->Close(), S_OK);

  D3D12TestContext second;
  ASSERT_EQ(second.Initialize(), S_OK);
  second.list()->EndQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
  EXPECT_EQ(second.list()->Close(), S_OK);
}

TEST_F(QueryValidationSpec, ZeroCountResolveIsNoOpBeforeRangeValidation) {
  auto timestamp = CreateHeap(D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1);
  auto destination = context_.CreateBuffer(
      8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(timestamp);
  ASSERT_TRUE(destination);
  context_.list()->ResolveQueryData(
      timestamp.get(), D3D12_QUERY_TYPE_TIMESTAMP,
      std::numeric_limits<UINT>::max(), 0, destination.get(), 1);
  EXPECT_EQ(context_.list()->Close(), S_OK);
}

} // namespace
