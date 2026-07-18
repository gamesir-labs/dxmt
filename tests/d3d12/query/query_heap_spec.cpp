#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
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

TEST_F(QueryHeapSpec, CapabilityProbeValidatesWithoutCreatingObjects) {
  for (const auto type : {D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
                          D3D12_QUERY_HEAP_TYPE_OCCLUSION}) {
    const D3D12_QUERY_HEAP_DESC desc = {type, 4, 0};
    EXPECT_EQ(context_.device()->CreateQueryHeap(
                  &desc, __uuidof(ID3D12QueryHeap), nullptr),
              S_FALSE)
        << "heap type " << static_cast<UINT>(type);
  }

  EXPECT_EQ(context_.device()->CreateQueryHeap(
                nullptr, __uuidof(ID3D12QueryHeap), nullptr),
            E_INVALIDARG);
  for (const D3D12_QUERY_HEAP_DESC desc : {
           D3D12_QUERY_HEAP_DESC{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 0, 0},
           D3D12_QUERY_HEAP_DESC{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1, 2},
           D3D12_QUERY_HEAP_DESC{
               static_cast<D3D12_QUERY_HEAP_TYPE>(0x7fffffff), 1, 0},
       }) {
    EXPECT_EQ(context_.device()->CreateQueryHeap(
                  &desc, __uuidof(ID3D12QueryHeap), nullptr),
              E_INVALIDARG);
  }
  for (const auto type : {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,
                          D3D12_QUERY_HEAP_TYPE_SO_STATISTICS}) {
    const D3D12_QUERY_HEAP_DESC desc = {type, 1, 0};
    EXPECT_EQ(context_.device()->CreateQueryHeap(
                  &desc, __uuidof(ID3D12QueryHeap), nullptr),
              E_NOTIMPL)
        << "heap type " << static_cast<UINT>(type);
  }
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

TEST_F(QueryHeapSpec, UnsupportedStatisticsHeapsFailConsistently) {
  auto device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(device);
  for (const auto type : {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,
                          D3D12_QUERY_HEAP_TYPE_SO_STATISTICS}) {
    const D3D12_QUERY_HEAP_DESC desc = {type, 4, 0};
    void *output = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
    EXPECT_EQ(device->CreateQueryHeap(&desc, __uuidof(ID3D12QueryHeap),
                                      &output),
              E_NOTIMPL)
        << "heap type " << static_cast<unsigned>(type);
    EXPECT_EQ(output, nullptr);
  }

  const D3D12_QUERY_HEAP_DESC timestamp_desc = {
      D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1, 0};
  ComPtr<ID3D12QueryHeap> timestamp;
  EXPECT_EQ(device->CreateQueryHeap(&timestamp_desc,
                                    __uuidof(ID3D12QueryHeap),
                                    reinterpret_cast<void **>(timestamp.put())),
            S_OK);
  EXPECT_TRUE(timestamp) << "statistics rejection must leave the device usable";
  EXPECT_EQ(device->GetDeviceRemovedReason(), S_OK);
}

} // namespace
