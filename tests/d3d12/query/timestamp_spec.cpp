#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class TimestampSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  ComPtr<ID3D12QueryHeap> CreateTimestampHeap(UINT count) {
    D3D12_QUERY_HEAP_DESC desc = {};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = count;
    ComPtr<ID3D12QueryHeap> heap;
    EXPECT_TRUE(SUCCEEDED(
        context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put()))));
    return heap;
  }

  D3D12TestContext context_;
};

TEST_F(TimestampSpec, ResolvesMultipleQueriesAtNonzeroDestinationOffset) {
  constexpr UINT64 sentinel = std::numeric_limits<UINT64>::max();
  constexpr UINT first_query = 1;
  constexpr UINT query_count = 3;
  constexpr UINT64 destination_offset = 2 * sizeof(UINT64);
  auto query_heap = CreateTimestampHeap(first_query + query_count);
  auto result = context_.CreateBuffer(
      6 * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(query_heap);
  ASSERT_TRUE(result);

  UINT64 *mapping = nullptr;
  const D3D12_RANGE no_read = {0, 0};
  ASSERT_TRUE(
      SUCCEEDED(result->Map(0, &no_read, reinterpret_cast<void **>(&mapping))));
  std::fill(mapping, mapping + 6, sentinel);
  const D3D12_RANGE initialized = {0, 6 * sizeof(UINT64)};
  result->Unmap(0, &initialized);

  for (UINT index = 0; index < query_count; ++index) {
    context_.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP,
                              first_query + index);
  }
  context_.list()->ResolveQueryData(
      query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, first_query, query_count,
      result.get(), destination_offset);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  const D3D12_RANGE read_range = {0, 6 * sizeof(UINT64)};
  ASSERT_TRUE(SUCCEEDED(
      result->Map(0, &read_range, reinterpret_cast<void **>(&mapping))));
  EXPECT_EQ(mapping[0], sentinel);
  EXPECT_EQ(mapping[1], sentinel);
  for (UINT index = 0; index < query_count; ++index)
    EXPECT_NE(mapping[2 + index], sentinel) << "query " << index;
  EXPECT_EQ(mapping[5], sentinel);
  const D3D12_RANGE no_write = {0, 0};
  result->Unmap(0, &no_write);
}

TEST_F(TimestampSpec, ValuesAreMonotonicWithinQueue) {
  constexpr UINT query_count = 4;
  auto query_heap = CreateTimestampHeap(query_count);
  auto result = context_.CreateBuffer(
      query_count * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(query_heap);
  ASSERT_TRUE(result);

  for (UINT index = 0; index < query_count; ++index) {
    context_.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP,
                              index);
  }
  context_.list()->ResolveQueryData(query_heap.get(),
                                    D3D12_QUERY_TYPE_TIMESTAMP, 0, query_count,
                                    result.get(), 0);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  UINT64 *mapping = nullptr;
  const D3D12_RANGE read_range = {0, query_count * sizeof(UINT64)};
  ASSERT_TRUE(SUCCEEDED(
      result->Map(0, &read_range, reinterpret_cast<void **>(&mapping))));
  for (UINT index = 1; index < query_count; ++index)
    EXPECT_LE(mapping[index - 1], mapping[index]) << "query " << index;
  const D3D12_RANGE no_write = {0, 0};
  result->Unmap(0, &no_write);
}

TEST_F(TimestampSpec, QueryAcrossCommandLists) {
  constexpr UINT query_count = 2;
  auto query_heap = CreateTimestampHeap(query_count);
  auto result = context_.CreateBuffer(
      query_count * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(query_heap);
  ASSERT_TRUE(result);

  std::array<ComPtr<ID3D12CommandAllocator>, 2> allocators;
  std::array<ComPtr<ID3D12GraphicsCommandList>, 2> lists;
  for (UINT index = 0; index < lists.size(); ++index) {
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(allocators[index].put()))));
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[index].get(), nullptr,
        IID_PPV_ARGS(lists[index].put()))));
    lists[index]->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
  }
  ASSERT_TRUE(SUCCEEDED(lists[0]->Close()));
  lists[1]->ResolveQueryData(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
                             query_count, result.get(), 0);
  ASSERT_TRUE(SUCCEEDED(lists[1]->Close()));

  ID3D12CommandList *submission[] = {lists[0].get(), lists[1].get()};
  context_.queue()->ExecuteCommandLists(ARRAYSIZE(submission), submission);
  ASSERT_TRUE(SUCCEEDED(context_.SignalAndWait()));

  UINT64 *mapping = nullptr;
  const D3D12_RANGE read_range = {0, query_count * sizeof(UINT64)};
  ASSERT_TRUE(SUCCEEDED(
      result->Map(0, &read_range, reinterpret_cast<void **>(&mapping))));
  EXPECT_LE(mapping[0], mapping[1]);
  const D3D12_RANGE no_write = {0, 0};
  result->Unmap(0, &no_write);
}

TEST_F(TimestampSpec, ReusesQueryAfterCompletion) {
  constexpr UINT64 sentinel = std::numeric_limits<UINT64>::max();
  auto query_heap = CreateTimestampHeap(1);
  auto result = context_.CreateBuffer(
      2 * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(query_heap);
  ASSERT_TRUE(result);

  UINT64 *mapping = nullptr;
  const D3D12_RANGE no_read = {0, 0};
  ASSERT_TRUE(
      SUCCEEDED(result->Map(0, &no_read, reinterpret_cast<void **>(&mapping))));
  std::fill(mapping, mapping + 2, sentinel);
  const D3D12_RANGE initialized = {0, 2 * sizeof(UINT64)};
  result->Unmap(0, &initialized);

  for (UINT iteration = 0; iteration < 2; ++iteration) {
    context_.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    context_.list()->ResolveQueryData(
        query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, result.get(),
        iteration * sizeof(UINT64));
    ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
    if (iteration == 0) {
      ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
    }
  }

  const D3D12_RANGE read_range = {0, 2 * sizeof(UINT64)};
  ASSERT_TRUE(SUCCEEDED(
      result->Map(0, &read_range, reinterpret_cast<void **>(&mapping))));
  EXPECT_NE(mapping[0], sentinel);
  EXPECT_NE(mapping[1], sentinel);
  EXPECT_LE(mapping[0], mapping[1]);
  const D3D12_RANGE no_write = {0, 0};
  result->Unmap(0, &no_write);
}

TEST_F(TimestampSpec, ClockCalibrationUsesPerformanceCounterDomain) {
  LARGE_INTEGER before = {};
  LARGE_INTEGER after = {};
  ASSERT_TRUE(QueryPerformanceCounter(&before));

  UINT64 gpu_timestamp = 0;
  UINT64 cpu_timestamp = 0;
  ASSERT_TRUE(SUCCEEDED(
      context_.queue()->GetClockCalibration(&gpu_timestamp, &cpu_timestamp)));

  ASSERT_TRUE(QueryPerformanceCounter(&after));
  EXPECT_GT(gpu_timestamp, 0ull);
  EXPECT_GE(cpu_timestamp, static_cast<UINT64>(before.QuadPart));
  EXPECT_LE(cpu_timestamp, static_cast<UINT64>(after.QuadPart));
}

} // namespace
