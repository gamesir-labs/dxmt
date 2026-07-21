#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;

class OcclusionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));

    target_ = context_.CreateTexture2D(16, 16, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                       D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                       D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv_heap_ =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_signature_ = context_.CreateRootSignature(root_desc);
    const auto pixel = CompileShader(
        "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
        "ps_5_0");
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    const D3D12_SHADER_BYTECODE pixel_bytecode = {
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize()};
    pipeline_ = context_.CreateGraphicsPipeline(root_signature_.get(),
                                                DXGI_FORMAT_R8G8B8A8_UNORM,
                                                pixel_bytecode);
    ASSERT_TRUE(target_);
    ASSERT_TRUE(rtv_heap_);
    ASSERT_TRUE(root_signature_);
    ASSERT_TRUE(pipeline_);

    rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target_.get(), nullptr, rtv_);
    context_.list()->OMSetRenderTargets(1, &rtv_, FALSE, nullptr);
    context_.list()->SetGraphicsRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline_.get());
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    constexpr D3D12_VIEWPORT viewport = {0.0f, 0.0f, 16.0f, 16.0f, 0.0f, 1.0f};
    constexpr D3D12_RECT scissor = {0, 0, 16, 16};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
  }

  ComPtr<ID3D12QueryHeap> CreateHeap(UINT count) {
    const D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, count,
                                        0};
    ComPtr<ID3D12QueryHeap> heap;
    EXPECT_TRUE(SUCCEEDED(
        context_.device()->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put()))));
    return heap;
  }

  ComPtr<ID3D12Resource> CreateResultBuffer(UINT64 size) {
    return context_.CreateBuffer(size, D3D12_HEAP_TYPE_READBACK,
                                 D3D12_RESOURCE_FLAG_NONE,
                                 D3D12_RESOURCE_STATE_COPY_DEST);
  }

  UINT64 ReadResult(ID3D12Resource *buffer, UINT64 offset = 0) {
    UINT64 *mapping = nullptr;
    const D3D12_RANGE read_range = {
        static_cast<SIZE_T>(offset),
        static_cast<SIZE_T>(offset + sizeof(UINT64))};
    EXPECT_TRUE(SUCCEEDED(
        buffer->Map(0, &read_range, reinterpret_cast<void **>(&mapping))));
    const UINT64 result = mapping ? mapping[offset / sizeof(UINT64)] : 0;
    const D3D12_RANGE no_write = {0, 0};
    if (mapping)
      buffer->Unmap(0, &no_write);
    return result;
  }

  void RecordQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE type, UINT index,
                   const D3D12_RECT &scissor) {
    context_.list()->BeginQuery(heap, type, index);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);
    context_.list()->EndQuery(heap, type, index);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12Resource> target_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
};

TEST_F(OcclusionSpec, CountsVisibleSamples) {
  auto heap = CreateHeap(1);
  auto result = CreateResultBuffer(sizeof(UINT64));
  ASSERT_TRUE(heap);
  ASSERT_TRUE(result);
  constexpr D3D12_RECT scissor = {0, 0, 16, 16};

  RecordQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0, scissor);
  context_.list()->ResolveQueryData(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0,
                                    1, result.get(), 0);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  EXPECT_GT(ReadResult(result.get()), 0u);
}

TEST_F(OcclusionSpec, ReportsZeroForClippedGeometry) {
  auto heap = CreateHeap(1);
  auto result = CreateResultBuffer(sizeof(UINT64));
  ASSERT_TRUE(heap);
  ASSERT_TRUE(result);
  constexpr D3D12_RECT empty_scissor = {};

  RecordQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0, empty_scissor);
  context_.list()->ResolveQueryData(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0,
                                    1, result.get(), 0);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  EXPECT_EQ(ReadResult(result.get()), 0u);
}

TEST_F(OcclusionSpec, PartialScissorReportsSubsetOfVisibleSamples) {
  auto heap = CreateHeap(2);
  auto result = CreateResultBuffer(2 * sizeof(UINT64));
  ASSERT_TRUE(heap);
  ASSERT_TRUE(result);
  constexpr D3D12_RECT full_scissor = {0, 0, 16, 16};
  constexpr D3D12_RECT half_scissor = {0, 0, 8, 16};

  RecordQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0, full_scissor);
  RecordQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 1, half_scissor);
  context_.list()->ResolveQueryData(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0,
                                    2, result.get(), 0);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  const UINT64 fully_visible = ReadResult(result.get());
  const UINT64 partially_visible = ReadResult(result.get(), sizeof(UINT64));
  EXPECT_GT(partially_visible, 0u);
  EXPECT_LT(partially_visible, fully_visible);
}

TEST_F(OcclusionSpec, ZeroVertexDrawReportsNoVisibleSamples) {
  auto heap = CreateHeap(1);
  auto result = CreateResultBuffer(sizeof(UINT64));
  ASSERT_TRUE(heap);
  ASSERT_TRUE(result);

  context_.list()->BeginQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
  context_.list()->DrawInstanced(0, 1, 0, 0);
  context_.list()->EndQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
  context_.list()->ResolveQueryData(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0,
                                    1, result.get(), 0);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  EXPECT_EQ(ReadResult(result.get()), 0u);
}

TEST_F(OcclusionSpec, BinaryResultsAreNormalized) {
  auto heap = CreateHeap(2);
  auto result = CreateResultBuffer(2 * sizeof(UINT64));
  ASSERT_TRUE(heap);
  ASSERT_TRUE(result);
  constexpr D3D12_RECT visible = {0, 0, 16, 16};
  constexpr D3D12_RECT invisible = {};

  RecordQuery(heap.get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, visible);
  RecordQuery(heap.get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1, invisible);
  context_.list()->ResolveQueryData(
      heap.get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, 2, result.get(), 0);
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  EXPECT_EQ(ReadResult(result.get()), 1u);
  EXPECT_EQ(ReadResult(result.get(), sizeof(UINT64)), 0u);
}

TEST_F(OcclusionSpec, ResolvesMultipleQueriesAtNonzeroDestinationOffset) {
  constexpr UINT64 sentinel = std::numeric_limits<UINT64>::max();
  auto heap = CreateHeap(3);
  auto result = CreateResultBuffer(4 * sizeof(UINT64));
  ASSERT_TRUE(heap);
  ASSERT_TRUE(result);
  UINT64 *mapping = nullptr;
  const D3D12_RANGE no_read = {0, 0};
  ASSERT_TRUE(
      SUCCEEDED(result->Map(0, &no_read, reinterpret_cast<void **>(&mapping))));
  std::fill(mapping, mapping + 4, sentinel);
  const D3D12_RANGE initialized = {0, 4 * sizeof(UINT64)};
  result->Unmap(0, &initialized);
  constexpr D3D12_RECT visible = {0, 0, 16, 16};

  RecordQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 1, visible);
  RecordQuery(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 2, visible);
  context_.list()->ResolveQueryData(heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 1,
                                    2, result.get(), sizeof(UINT64));
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  const D3D12_RANGE read_range = {0, 4 * sizeof(UINT64)};
  ASSERT_TRUE(SUCCEEDED(
      result->Map(0, &read_range, reinterpret_cast<void **>(&mapping))));
  EXPECT_EQ(mapping[0], sentinel);
  EXPECT_GT(mapping[1], 0u);
  EXPECT_GT(mapping[2], 0u);
  EXPECT_EQ(mapping[3], sentinel);
  const D3D12_RANGE no_write = {0, 0};
  result->Unmap(0, &no_write);
}

} // namespace
