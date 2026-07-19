#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

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

TEST(QuerySequenceSpec, OcclusionBeginAndEndOnOneCommandListCloses) {
  D3D12TestContext context;
  ASSERT_EQ(context.Initialize(), S_OK);
  const D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 1, 0};
  ComPtr<ID3D12QueryHeap> heap;
  ASSERT_EQ(context.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put())),
            S_OK);
  context.list()->BeginQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
  context.list()->EndQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
  EXPECT_EQ(context.list()->Close(), S_OK);
}


} // namespace
