#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class QueryHeapSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12TestContext context_;
};

TEST_F(QueryHeapSpec, CreatesTimestampHeap) {
  const D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1, 0};
  ComPtr<ID3D12QueryHeap> heap;

  ASSERT_TRUE(SUCCEEDED(
      context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put()))));
  EXPECT_TRUE(heap);
}

TEST_F(QueryHeapSpec, CreatesLargeOcclusionHeap) {
  const D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 257, 0};
  ComPtr<ID3D12QueryHeap> heap;

  ASSERT_TRUE(SUCCEEDED(
      context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put()))));
  EXPECT_TRUE(heap);
}

TEST_F(QueryHeapSpec, RejectsZeroCount) {
  const D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 0, 0};
  ComPtr<ID3D12QueryHeap> heap;

  EXPECT_EQ(context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put())),
            E_INVALIDARG);
  EXPECT_FALSE(heap);
}

TEST_F(QueryHeapSpec, RejectsInvalidNodeMask) {
  const D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1, 2};
  ComPtr<ID3D12QueryHeap> heap;

  EXPECT_EQ(context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put())),
            E_INVALIDARG);
  EXPECT_FALSE(heap);
}

TEST_F(QueryHeapSpec, RejectsInvalidType) {
  const D3D12_QUERY_HEAP_DESC desc = {
      static_cast<D3D12_QUERY_HEAP_TYPE>(0x7fffffff), 1, 0};
  ComPtr<ID3D12QueryHeap> heap;

  EXPECT_EQ(context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put())),
            E_INVALIDARG);
  EXPECT_FALSE(heap);
}

} // namespace
