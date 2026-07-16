#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

enum class FramePath {
  SingleQueue,
  CopyComputeDirect,
};

struct CommandListPair {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
};

class IntegrationFrameSpec : public ::testing::TestWithParam<FramePath> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  CommandListPair CreateList(D3D12_COMMAND_LIST_TYPE type) {
    CommandListPair pair;
    EXPECT_EQ(context_.device()->CreateCommandAllocator(
                  type, __uuidof(ID3D12CommandAllocator),
                  reinterpret_cast<void **>(pair.allocator.put())),
              S_OK);
    if (!pair.allocator)
      return pair;
    EXPECT_EQ(context_.device()->CreateCommandList(
                  0, type, pair.allocator.get(), nullptr,
                  __uuidof(ID3D12GraphicsCommandList),
                  reinterpret_cast<void **>(pair.list.put())),
              S_OK);
    return pair;
  }

  ComPtr<ID3D12CommandQueue> CreateQueue(D3D12_COMMAND_LIST_TYPE type) {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    ComPtr<ID3D12CommandQueue> queue;
    EXPECT_EQ(context_.device()->CreateCommandQueue(
                  &desc, __uuidof(ID3D12CommandQueue),
                  reinterpret_cast<void **>(queue.put())),
              S_OK);
    return queue;
  }

  D3D12TestContext context_;
};

TEST_P(IntegrationFrameSpec, UploadComputeRenderReadback) {
  constexpr UINT kSize = 8;
  constexpr std::array<float, 4> input_color = {0.25f, 0.5f, 0.75f, 1.0f};
  constexpr std::uint32_t expected_pixel = 0xff4080bf;
  const auto compute_shader = CompileShader(R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);

    [numthreads(1, 1, 1)]
    void main() {
      uint4 value = input.Load4(0);
      output.Store(0, value.z);
      output.Store(4, value.y);
      output.Store(8, value.x);
      output.Store(12, value.w);
    }
  )",
                                            "cs_5_0");
  const auto pixel_shader = CompileShader(R"(
    ByteAddressBuffer color : register(t0);

    float4 main() : SV_Target {
      return asfloat(color.Load4(0));
    }
  )",
                                          "ps_5_0");
  ASSERT_EQ(compute_shader.result, S_OK)
      << compute_shader.diagnostic_text();
  ASSERT_EQ(pixel_shader.result, S_OK) << pixel_shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE compute_ranges[2] = {};
  compute_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  compute_ranges[0].NumDescriptors = 1;
  compute_ranges[0].OffsetInDescriptorsFromTableStart = 0;
  compute_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  compute_ranges[1].NumDescriptors = 1;
  compute_ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_ROOT_PARAMETER compute_parameter = {};
  compute_parameter.ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  compute_parameter.DescriptorTable.NumDescriptorRanges = 2;
  compute_parameter.DescriptorTable.pDescriptorRanges = compute_ranges;
  D3D12_ROOT_SIGNATURE_DESC compute_root_desc = {};
  compute_root_desc.NumParameters = 1;
  compute_root_desc.pParameters = &compute_parameter;
  auto compute_root = context_.CreateRootSignature(compute_root_desc);

  D3D12_DESCRIPTOR_RANGE graphics_range = {};
  graphics_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  graphics_range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER graphics_parameter = {};
  graphics_parameter.ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  graphics_parameter.DescriptorTable.NumDescriptorRanges = 1;
  graphics_parameter.DescriptorTable.pDescriptorRanges = &graphics_range;
  graphics_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC graphics_root_desc = {};
  graphics_root_desc.NumParameters = 1;
  graphics_root_desc.pParameters = &graphics_parameter;
  graphics_root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto graphics_root = context_.CreateRootSignature(graphics_root_desc);
  ASSERT_TRUE(compute_root);
  ASSERT_TRUE(graphics_root);

  const D3D12_SHADER_BYTECODE compute_bytecode = {
      compute_shader.bytecode->GetBufferPointer(),
      compute_shader.bytecode->GetBufferSize()};
  const D3D12_SHADER_BYTECODE pixel_bytecode = {
      pixel_shader.bytecode->GetBufferPointer(),
      pixel_shader.bytecode->GetBufferSize()};
  auto compute_pipeline =
      context_.CreateComputePipeline(compute_root.get(), compute_bytecode);
  auto graphics_pipeline = context_.CreateGraphicsPipeline(
      graphics_root.get(), DXGI_FORMAT_R8G8B8A8_UNORM, pixel_bytecode);
  ASSERT_TRUE(compute_pipeline);
  ASSERT_TRUE(graphics_pipeline);

  auto upload = context_.CreateUploadBuffer(
      sizeof(input_color), input_color.data(), sizeof(input_color));
  auto input = context_.CreateBuffer(
      sizeof(input_color), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto compute_output = context_.CreateBuffer(
      sizeof(input_color), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto target = context_.CreateTexture2D(
      kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto descriptor_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, true);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(input);
  ASSERT_TRUE(compute_output);
  ASSERT_TRUE(target);
  ASSERT_TRUE(descriptor_heap);
  ASSERT_TRUE(rtv_heap);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 4;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 4;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateShaderResourceView(
      input.get(), &srv, context_.CpuDescriptorHandle(descriptor_heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      compute_output.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(descriptor_heap.get(), 1));
  context_.device()->CreateShaderResourceView(
      compute_output.get(), &srv,
      context_.CpuDescriptorHandle(descriptor_heap.get(), 2));
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  const auto target_desc = target->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  context_.device()->GetCopyableFootprints(
      &target_desc, 0, 1, 0, &footprint, &row_count, &row_size, &total_size);
  auto readback = context_.CreateBuffer(
      total_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(readback);

  auto record_compute = [&](ID3D12GraphicsCommandList *list,
                            D3D12_RESOURCE_STATES input_before,
                            D3D12_RESOURCE_STATES output_after) {
    D3D12TestContext::Transition(
        list, input.get(), input_before,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap *heaps[] = {descriptor_heap.get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetPipelineState(compute_pipeline.get());
    list->SetComputeRootSignature(compute_root.get());
    list->SetComputeRootDescriptorTable(
        0, context_.GpuDescriptorHandle(descriptor_heap.get(), 0));
    list->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        list, compute_output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        output_after);
  };

  auto record_graphics = [&](ID3D12GraphicsCommandList *list,
                             bool transition_output) {
    if (transition_output) {
      D3D12TestContext::Transition(
          list, compute_output.get(), D3D12_RESOURCE_STATE_COMMON,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    ID3D12DescriptorHeap *heaps[] = {descriptor_heap.get()};
    list->SetDescriptorHeaps(1, heaps);
    constexpr FLOAT clear_color[4] = {};
    list->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->SetPipelineState(graphics_pipeline.get());
    list->SetGraphicsRootSignature(graphics_root.get());
    list->SetGraphicsRootDescriptorTable(
        0, context_.GpuDescriptorHandle(descriptor_heap.get(), 2));
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<FLOAT>(kSize), static_cast<FLOAT>(kSize),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, kSize, kSize};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->DrawInstanced(3, 1, 0, 0);
    D3D12TestContext::Transition(
        list, target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = target.get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = readback.get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  };

  if (GetParam() == FramePath::SingleQueue) {
    context_.list()->CopyBufferRegion(input.get(), 0, upload.get(), 0,
                                      sizeof(input_color));
    record_compute(context_.list(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    record_graphics(context_.list(), false);
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  } else {
    auto copy_queue = CreateQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto compute_queue = CreateQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    auto direct_queue = CreateQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto copy = CreateList(D3D12_COMMAND_LIST_TYPE_COPY);
    auto compute = CreateList(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    auto direct = CreateList(D3D12_COMMAND_LIST_TYPE_DIRECT);
    ASSERT_TRUE(copy_queue);
    ASSERT_TRUE(compute_queue);
    ASSERT_TRUE(direct_queue);
    ASSERT_TRUE(copy.list);
    ASSERT_TRUE(compute.list);
    ASSERT_TRUE(direct.list);
    copy.list->CopyBufferRegion(input.get(), 0, upload.get(), 0,
                                sizeof(input_color));
    record_compute(compute.list.get(), D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_COMMON);
    record_graphics(direct.list.get(), true);
    ASSERT_EQ(copy.list->Close(), S_OK);
    ASSERT_EQ(compute.list->Close(), S_OK);
    ASSERT_EQ(direct.list->Close(), S_OK);

    ComPtr<ID3D12Fence> fence;
    ASSERT_EQ(context_.device()->CreateFence(
                  0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                  reinterpret_cast<void **>(fence.put())),
              S_OK);
    ID3D12CommandList *copy_lists[] = {copy.list.get()};
    ID3D12CommandList *compute_lists[] = {compute.list.get()};
    ID3D12CommandList *direct_lists[] = {direct.list.get()};
    copy_queue->ExecuteCommandLists(1, copy_lists);
    ASSERT_EQ(copy_queue->Signal(fence.get(), 1), S_OK);
    ASSERT_EQ(compute_queue->Wait(fence.get(), 1), S_OK);
    compute_queue->ExecuteCommandLists(1, compute_lists);
    ASSERT_EQ(compute_queue->Signal(fence.get(), 2), S_OK);
    ASSERT_EQ(direct_queue->Wait(fence.get(), 2), S_OK);
    direct_queue->ExecuteCommandLists(1, direct_lists);
    ASSERT_EQ(direct_queue->Signal(fence.get(), 3), S_OK);
    ASSERT_EQ(context_.WaitForFence(fence.get(), 3), S_OK);
  }

  void *mapped = nullptr;
  const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(total_size)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapped), S_OK);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  static_cast<const std::uint8_t *>(mapped) +
                      footprint.Offset + y * footprint.Footprint.RowPitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, expected_pixel, 1))
          << "pixel (" << x << ", " << y << ")";
    }
  }
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
}

INSTANTIATE_TEST_SUITE_P(
    FramePaths, IntegrationFrameSpec,
    ::testing::Values(FramePath::SingleQueue, FramePath::CopyComputeDirect),
    [](const ::testing::TestParamInfo<FramePath> &info) {
      return info.param == FramePath::SingleQueue ? "SingleQueue"
                                                   : "CopyComputeDirect";
    });

} // namespace
