#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <limits>
#include <string>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;

enum class InvalidQueryCase {
  BeginTimestamp,
  BeginOutOfRange,
  BeginTypeMismatch,
  EndOutOfRange,
  EndTypeMismatch,
  NestedBegin,
  MismatchedBeginEndType,
  ResolveStartOutOfRange,
  ResolveRangeOutOfRange,
  ResolveMisalignedDestination,
  ResolveDestinationOutOfRange,
  ResolveTypeMismatch,
  ResolveToTexture,
  ForeignHeap,
  ForeignDestination,
};

class QueryValidationSpec
    : public ::testing::TestWithParam<InvalidQueryCase> {
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

TEST_P(QueryValidationSpec, InvalidSequenceFailsCommandListClose) {
  auto timestamp = CreateHeap(D3D12_QUERY_HEAP_TYPE_TIMESTAMP);
  auto occlusion = CreateHeap(D3D12_QUERY_HEAP_TYPE_OCCLUSION);
  auto destination = context_.CreateBuffer(
      32, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto texture = context_.CreateTexture2D(
      2, 2, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(timestamp);
  ASSERT_TRUE(occlusion);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(texture);

  switch (GetParam()) {
  case InvalidQueryCase::BeginTimestamp:
    context_.list()->BeginQuery(timestamp.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    break;
  case InvalidQueryCase::BeginOutOfRange:
    context_.list()->BeginQuery(occlusion.get(), D3D12_QUERY_TYPE_OCCLUSION, 4);
    break;
  case InvalidQueryCase::BeginTypeMismatch:
    context_.list()->BeginQuery(timestamp.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
    break;
  case InvalidQueryCase::EndOutOfRange:
    context_.list()->EndQuery(timestamp.get(), D3D12_QUERY_TYPE_TIMESTAMP, 4);
    break;
  case InvalidQueryCase::EndTypeMismatch:
    context_.list()->EndQuery(timestamp.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
    break;
  case InvalidQueryCase::NestedBegin:
    context_.list()->BeginQuery(occlusion.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
    context_.list()->BeginQuery(occlusion.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
    break;
  case InvalidQueryCase::MismatchedBeginEndType:
    context_.list()->BeginQuery(occlusion.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
    context_.list()->EndQuery(occlusion.get(),
                              D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
    break;
  case InvalidQueryCase::ResolveStartOutOfRange:
    context_.list()->ResolveQueryData(timestamp.get(),
                                      D3D12_QUERY_TYPE_TIMESTAMP, 4, 1,
                                      destination.get(), 0);
    break;
  case InvalidQueryCase::ResolveRangeOutOfRange:
    context_.list()->ResolveQueryData(timestamp.get(),
                                      D3D12_QUERY_TYPE_TIMESTAMP, 3, 2,
                                      destination.get(), 0);
    break;
  case InvalidQueryCase::ResolveMisalignedDestination:
    context_.list()->ResolveQueryData(timestamp.get(),
                                      D3D12_QUERY_TYPE_TIMESTAMP, 0, 1,
                                      destination.get(), 1);
    break;
  case InvalidQueryCase::ResolveDestinationOutOfRange:
    context_.list()->ResolveQueryData(timestamp.get(),
                                      D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                      destination.get(), 24);
    break;
  case InvalidQueryCase::ResolveTypeMismatch:
    context_.list()->ResolveQueryData(timestamp.get(),
                                      D3D12_QUERY_TYPE_OCCLUSION, 0, 1,
                                      destination.get(), 0);
    break;
  case InvalidQueryCase::ResolveToTexture:
    context_.list()->ResolveQueryData(timestamp.get(),
                                      D3D12_QUERY_TYPE_TIMESTAMP, 0, 1,
                                      texture.get(), 0);
    break;
  case InvalidQueryCase::ForeignHeap: {
    auto foreign_device = CreateIsolatedD3D12Device();
    ASSERT_TRUE(foreign_device);
    const D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1, 0};
    ComPtr<ID3D12QueryHeap> foreign_heap;
    ASSERT_EQ(foreign_device->CreateQueryHeap(&desc,
                                              IID_PPV_ARGS(foreign_heap.put())),
              S_OK);
    context_.list()->EndQuery(foreign_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP,
                              0);
    break;
  }
  case InvalidQueryCase::ForeignDestination: {
    auto foreign_device = CreateIsolatedD3D12Device();
    ASSERT_TRUE(foreign_device);
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 8;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> foreign_destination;
    ASSERT_EQ(foreign_device->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc,
                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                  IID_PPV_ARGS(foreign_destination.put())),
              S_OK);
    context_.list()->ResolveQueryData(
        timestamp.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 1,
        foreign_destination.get(), 0);
    break;
  }
  }

  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);
}

std::string InvalidQueryCaseName(
    const ::testing::TestParamInfo<InvalidQueryCase> &info) {
  switch (info.param) {
  case InvalidQueryCase::BeginTimestamp:
    return "BeginTimestamp";
  case InvalidQueryCase::BeginOutOfRange:
    return "BeginOutOfRange";
  case InvalidQueryCase::BeginTypeMismatch:
    return "BeginTypeMismatch";
  case InvalidQueryCase::EndOutOfRange:
    return "EndOutOfRange";
  case InvalidQueryCase::EndTypeMismatch:
    return "EndTypeMismatch";
  case InvalidQueryCase::NestedBegin:
    return "NestedBegin";
  case InvalidQueryCase::MismatchedBeginEndType:
    return "MismatchedBeginEndType";
  case InvalidQueryCase::ResolveStartOutOfRange:
    return "ResolveStartOutOfRange";
  case InvalidQueryCase::ResolveRangeOutOfRange:
    return "ResolveRangeOutOfRange";
  case InvalidQueryCase::ResolveMisalignedDestination:
    return "ResolveMisalignedDestination";
  case InvalidQueryCase::ResolveDestinationOutOfRange:
    return "ResolveDestinationOutOfRange";
  case InvalidQueryCase::ResolveTypeMismatch:
    return "ResolveTypeMismatch";
  case InvalidQueryCase::ResolveToTexture:
    return "ResolveToTexture";
  case InvalidQueryCase::ForeignHeap:
    return "ForeignHeap";
  case InvalidQueryCase::ForeignDestination:
    return "ForeignDestination";
  }
  return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    InvalidMatrix, QueryValidationSpec,
    ::testing::Values(
        InvalidQueryCase::BeginTimestamp,
        InvalidQueryCase::BeginOutOfRange,
        InvalidQueryCase::BeginTypeMismatch,
        InvalidQueryCase::EndOutOfRange,
        InvalidQueryCase::EndTypeMismatch,
        InvalidQueryCase::NestedBegin,
        InvalidQueryCase::MismatchedBeginEndType,
        InvalidQueryCase::ResolveStartOutOfRange,
        InvalidQueryCase::ResolveRangeOutOfRange,
        InvalidQueryCase::ResolveMisalignedDestination,
        InvalidQueryCase::ResolveDestinationOutOfRange,
        InvalidQueryCase::ResolveTypeMismatch,
        InvalidQueryCase::ResolveToTexture,
        InvalidQueryCase::ForeignHeap,
        InvalidQueryCase::ForeignDestination),
    InvalidQueryCaseName);

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
