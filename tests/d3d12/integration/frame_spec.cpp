#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;

enum class FramePath {
  SingleQueue,
  CopyComputeDirect,
};

struct CommandListPair {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
};

class IntegrationTestBase : public ::testing::Test {
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

class IntegrationFrameSpec
    : public IntegrationTestBase,
      public ::testing::WithParamInterface<FramePath> {};

TEST_P(IntegrationFrameSpec, UploadComputeRenderReadback) {
  constexpr UINT kSize = 16;
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

class IntegrationCompositionSpec : public IntegrationTestBase {};

TEST_F(IntegrationCompositionSpec, RecyclesDescriptorRingAcrossFrames) {
  constexpr UINT kFrameCount = 8;
  constexpr UINT kRingSize = 3;
  constexpr std::uint32_t kMask = 0x5a5a5a5a;
  const auto shader = CompileShader(R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);

    [numthreads(1, 1, 1)]
    void main() {
      output.Store(0, input.Load(0) ^ 0x5a5a5a5a);
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 1;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);

  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root.get(), bytecode);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kRingSize * 2, true);
  auto readback = context_.CreateBuffer(
      kFrameCount * sizeof(std::uint32_t), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(heap);
  ASSERT_TRUE(readback);

  std::array<std::uint32_t, kFrameCount> input_values = {};
  std::array<ComPtr<ID3D12Resource>, kFrameCount> inputs;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> outputs;
  std::array<CommandListPair, kFrameCount> commands;
  for (UINT frame = 0; frame < kFrameCount; ++frame) {
    input_values[frame] = 0x10203040u + frame * 0x01010101u;
    inputs[frame] = context_.CreateUploadBuffer(
        sizeof(input_values[frame]), &input_values[frame],
        sizeof(input_values[frame]));
    outputs[frame] = context_.CreateBuffer(
        sizeof(input_values[frame]), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_TRUE(inputs[frame]);
    ASSERT_TRUE(outputs[frame]);
  }

  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(fence.put())),
            S_OK);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 1;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

  for (UINT frame = 0; frame < kFrameCount; ++frame) {
    const UINT slot = frame % kRingSize;
    if (frame >= kRingSize) {
      ASSERT_EQ(context_.WaitForFence(fence.get(), frame - kRingSize + 1),
                S_OK);
    }

    context_.device()->CreateShaderResourceView(
        inputs[frame].get(), &srv,
        context_.CpuDescriptorHandle(heap.get(), slot * 2));
    context_.device()->CreateUnorderedAccessView(
        outputs[frame].get(), nullptr, &uav,
        context_.CpuDescriptorHandle(heap.get(), slot * 2 + 1));
    commands[frame] = CreateList(D3D12_COMMAND_LIST_TYPE_DIRECT);
    ASSERT_TRUE(commands[frame].list);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    commands[frame].list->SetDescriptorHeaps(1, heaps);
    commands[frame].list->SetPipelineState(pipeline.get());
    commands[frame].list->SetComputeRootSignature(root.get());
    commands[frame].list->SetComputeRootDescriptorTable(
        0, context_.GpuDescriptorHandle(heap.get(), slot * 2));
    commands[frame].list->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        commands[frame].list.get(), outputs[frame].get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands[frame].list->CopyBufferRegion(
        readback.get(), frame * sizeof(std::uint32_t), outputs[frame].get(), 0,
        sizeof(std::uint32_t));
    ASSERT_EQ(commands[frame].list->Close(), S_OK);
    ID3D12CommandList *lists[] = {commands[frame].list.get()};
    context_.queue()->ExecuteCommandLists(1, lists);
    ASSERT_EQ(context_.queue()->Signal(fence.get(), frame + 1), S_OK);
  }
  ASSERT_EQ(context_.WaitForFence(fence.get(), kFrameCount), S_OK);

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0, kFrameCount * sizeof(std::uint32_t)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
  const auto *actual = static_cast<const std::uint32_t *>(mapping);
  for (UINT frame = 0; frame < kFrameCount; ++frame)
    EXPECT_EQ(actual[frame], input_values[frame] ^ kMask) << "frame " << frame;
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
}

TEST_F(IntegrationCompositionSpec, ExecutesAliasingFrameGraph) {
  constexpr UINT kValueCount = 8;
  const auto shader = CompileShader(R"(
    RWByteAddressBuffer output : register(u0);

    [numthreads(8, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      output.Store(id.x * 4, 0x12340000 + id.x);
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameter.Descriptor.ShaderRegister = 0;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root.get(), bytecode);
  ASSERT_TRUE(pipeline);

  D3D12_RESOURCE_DESC texture_desc = {};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = 4;
  texture_desc.Height = 4;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_RESOURCE_DESC buffer_desc = {};
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  const auto texture_info =
      context_.device()->GetResourceAllocationInfo(0, 1, &texture_desc);
  const auto buffer_info =
      context_.device()->GetResourceAllocationInfo(0, 1, &buffer_desc);

  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = texture_info.SizeInBytes > buffer_info.SizeInBytes
                              ? texture_info.SizeInBytes
                              : buffer_info.SizeInBytes;
  heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
  ComPtr<ID3D12Heap> heap;
  ASSERT_EQ(context_.device()->CreateHeap(&heap_desc, IID_PPV_ARGS(heap.put())),
            S_OK);

  ComPtr<ID3D12Resource> target;
  ComPtr<ID3D12Resource> compute;
  ComPtr<ID3D12Resource> copy_source;
  ASSERT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), 0, &texture_desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                IID_PPV_ARGS(target.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), 0, &buffer_desc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                IID_PPV_ARGS(compute.put())),
            S_OK);
  buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  ASSERT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), 0, &buffer_desc,
                D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                IID_PPV_ARGS(copy_source.put())),
            S_OK);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto readback = context_.CreateBuffer(
      kValueCount * sizeof(std::uint32_t), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(readback);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  constexpr FLOAT clear_color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  barrier.Aliasing.pResourceBefore = target.get();
  barrier.Aliasing.pResourceAfter = compute.get();
  context_.list()->ResourceBarrier(1, &barrier);
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetComputeRootUnorderedAccessView(
      0, compute->GetGPUVirtualAddress());
  context_.list()->Dispatch(1, 1, 1);
  barrier.Aliasing.pResourceBefore = compute.get();
  barrier.Aliasing.pResourceAfter = copy_source.get();
  context_.list()->ResourceBarrier(1, &barrier);
  context_.list()->CopyBufferRegion(
      readback.get(), 0, copy_source.get(), 0,
      kValueCount * sizeof(std::uint32_t));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0,
                                  kValueCount * sizeof(std::uint32_t)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
  const auto *actual = static_cast<const std::uint32_t *>(mapping);
  for (UINT index = 0; index < kValueCount; ++index)
    EXPECT_EQ(actual[index], 0x12340000u + index) << "value " << index;
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
}

TEST_F(IntegrationCompositionSpec,
       ExecutesMixedFrameWithCopyResolve) {
  constexpr UINT kSize = 16;
  constexpr UINT kSampleCount = 4;
  constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
  quality.Format = kFormat;
  quality.SampleCount = kSampleCount;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                sizeof(quality)),
            S_OK);
  if (!quality.NumQualityLevels)
    GTEST_SKIP() << "4x MSAA is unavailable";

  const auto first_pixel = CompileShader(R"(
    float4 main() : SV_Target {
      return float4(0.125, 0.0, 0.5, 1.0);
    }
  )",
                                         "ps_5_0");
  const auto compute = CompileShader(R"(
    RWBuffer<uint> output : register(u0);

    [numthreads(1, 1, 1)]
    void main() {
      uint ignored;
      output[0] = 64;
      InterlockedAdd(output[1], 1, ignored);
    }
  )",
                                     "cs_5_0");
  const auto fallback_pixel = CompileShader(R"(
    Buffer<uint> state : register(t0);
    Texture2D<float4> copied_draw : register(t1);

    float4 main() : SV_Target {
      float4 source = copied_draw.Load(int3(0, 0, 0));
      return float4(source.r, state[0] / 255.0, source.b, 0.5);
    }
  )",
                                            "ps_5_0");
  ASSERT_EQ(first_pixel.result, S_OK) << first_pixel.diagnostic_text();
  ASSERT_EQ(compute.result, S_OK) << compute.diagnostic_text();
  ASSERT_EQ(fallback_pixel.result, S_OK)
      << fallback_pixel.diagnostic_text();

  D3D12_ROOT_SIGNATURE_DESC first_root_desc = {};
  first_root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto first_root = context_.CreateRootSignature(first_root_desc);

  D3D12_DESCRIPTOR_RANGE compute_range = {};
  compute_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  compute_range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER compute_parameter = {};
  compute_parameter.ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  compute_parameter.DescriptorTable.NumDescriptorRanges = 1;
  compute_parameter.DescriptorTable.pDescriptorRanges = &compute_range;
  D3D12_ROOT_SIGNATURE_DESC compute_root_desc = {};
  compute_root_desc.NumParameters = 1;
  compute_root_desc.pParameters = &compute_parameter;
  auto compute_root = context_.CreateRootSignature(compute_root_desc);

  D3D12_DESCRIPTOR_RANGE texture_range = {};
  texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  texture_range.NumDescriptors = 2;
  D3D12_ROOT_PARAMETER fallback_parameter = {};
  fallback_parameter.ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  fallback_parameter.DescriptorTable.NumDescriptorRanges = 1;
  fallback_parameter.DescriptorTable.pDescriptorRanges = &texture_range;
  fallback_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC fallback_root_desc = {};
  fallback_root_desc.NumParameters = 1;
  fallback_root_desc.pParameters = &fallback_parameter;
  fallback_root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto fallback_root = context_.CreateRootSignature(fallback_root_desc);
  ASSERT_TRUE(first_root);
  ASSERT_TRUE(compute_root);
  ASSERT_TRUE(fallback_root);

  const auto create_graphics_pipeline =
      [&](ID3D12RootSignature *root, const auto &shader, UINT sample_count,
          bool additive) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root;
        desc.VS = FullscreenVertexShader();
        desc.PS = {shader.bytecode->GetBufferPointer(),
                   shader.bytecode->GetBufferSize()};
        auto &blend = desc.BlendState.RenderTarget[0];
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (additive) {
          blend.BlendEnable = TRUE;
          blend.SrcBlend = D3D12_BLEND_ONE;
          blend.DestBlend = D3D12_BLEND_ONE;
          blend.BlendOp = D3D12_BLEND_OP_ADD;
          blend.SrcBlendAlpha = D3D12_BLEND_ONE;
          blend.DestBlendAlpha = D3D12_BLEND_ONE;
          blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = kFormat;
        desc.SampleDesc.Count = sample_count;
        ComPtr<ID3D12PipelineState> pipeline;
        EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                      &desc, IID_PPV_ARGS(pipeline.put())),
                  S_OK);
        return pipeline;
      };
  auto first_pipeline =
      create_graphics_pipeline(first_root.get(), first_pixel, 1, true);
  auto fallback_pipeline = create_graphics_pipeline(
      fallback_root.get(), fallback_pixel, kSampleCount, true);
  const D3D12_SHADER_BYTECODE compute_bytecode = {
      compute.bytecode->GetBufferPointer(), compute.bytecode->GetBufferSize()};
  auto compute_pipeline =
      context_.CreateComputePipeline(compute_root.get(), compute_bytecode);
  ASSERT_TRUE(first_pipeline);
  ASSERT_TRUE(fallback_pipeline);
  ASSERT_TRUE(compute_pipeline);

  const auto create_texture =
      [&](UINT sample_count, D3D12_RESOURCE_FLAGS flags,
          D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES properties = {};
        properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kSize;
        desc.Height = kSize;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = kFormat;
        desc.SampleDesc.Count = sample_count;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = flags;
        ComPtr<ID3D12Resource> texture;
        EXPECT_EQ(context_.device()->CreateCommittedResource(
                      &properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                      IID_PPV_ARGS(texture.put())),
                  S_OK);
        return texture;
      };
  auto first_target = create_texture(
      1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto copied_draw =
      create_texture(1, D3D12_RESOURCE_FLAG_NONE,
                     D3D12_RESOURCE_STATE_COPY_DEST);
  auto multisample_target = create_texture(
      kSampleCount, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto resolved = create_texture(1, D3D12_RESOURCE_FLAG_NONE,
                                 D3D12_RESOURCE_STATE_RESOLVE_DEST);
  constexpr std::array<std::uint32_t, 2> kZeroState = {};
  auto state_upload = context_.CreateUploadBuffer(
      sizeof(kZeroState), kZeroState.data(), sizeof(kZeroState));
  auto state = context_.CreateBuffer(
      sizeof(kZeroState), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  constexpr UINT64 kExecutePredicate = 1;
  auto predicate = context_.CreateUploadBuffer(
      sizeof(kExecutePredicate), &kExecutePredicate,
      sizeof(kExecutePredicate));
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
  auto srv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, true);
  ASSERT_TRUE(first_target);
  ASSERT_TRUE(copied_draw);
  ASSERT_TRUE(multisample_target);
  ASSERT_TRUE(resolved);
  ASSERT_TRUE(state_upload);
  ASSERT_TRUE(state);
  ASSERT_TRUE(predicate);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(srv_heap);

  const auto first_rtv = context_.CpuDescriptorHandle(rtv_heap.get(), 0);
  const auto multisample_rtv =
      context_.CpuDescriptorHandle(rtv_heap.get(), 1);
  context_.device()->CreateRenderTargetView(first_target.get(), nullptr,
                                            first_rtv);
  D3D12_RENDER_TARGET_VIEW_DESC multisample_rtv_desc = {};
  multisample_rtv_desc.Format = kFormat;
  multisample_rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
  context_.device()->CreateRenderTargetView(
      multisample_target.get(), &multisample_rtv_desc, multisample_rtv);
  D3D12_UNORDERED_ACCESS_VIEW_DESC state_uav = {};
  state_uav.Format = DXGI_FORMAT_R32_UINT;
  state_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  state_uav.Buffer.NumElements = kZeroState.size();
  context_.device()->CreateUnorderedAccessView(
      state.get(), nullptr, &state_uav,
      context_.CpuDescriptorHandle(srv_heap.get(), 0));
  D3D12_SHADER_RESOURCE_VIEW_DESC state_srv = {};
  state_srv.Format = DXGI_FORMAT_R32_UINT;
  state_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  state_srv.Shader4ComponentMapping =
      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  state_srv.Buffer.NumElements = kZeroState.size();
  context_.device()->CreateShaderResourceView(
      state.get(), &state_srv,
      context_.CpuDescriptorHandle(srv_heap.get(), 1));
  D3D12_SHADER_RESOURCE_VIEW_DESC texture_srv = {};
  texture_srv.Format = kFormat;
  texture_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  texture_srv.Shader4ComponentMapping =
      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  texture_srv.Texture2D.MipLevels = 1;
  context_.device()->CreateShaderResourceView(
      copied_draw.get(), &texture_srv,
      context_.CpuDescriptorHandle(srv_heap.get(), 2));

  const auto resolved_desc = resolved->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 texture_readback_size = 0;
  context_.device()->GetCopyableFootprints(
      &resolved_desc, 0, 1, 0, &footprint, &row_count, &row_size,
      &texture_readback_size);
  auto texture_readback = context_.CreateBuffer(
      texture_readback_size, D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  auto state_readback = context_.CreateBuffer(
      sizeof(kZeroState), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture_readback);
  ASSERT_TRUE(state_readback);

  context_.list()->CopyBufferRegion(state.get(), 0, state_upload.get(), 0,
                                     sizeof(kZeroState));
  D3D12TestContext::Transition(context_.list(), state.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  constexpr FLOAT kBlack[4] = {};
  context_.list()->ClearRenderTargetView(first_rtv, kBlack, 0, nullptr);
  context_.list()->ClearRenderTargetView(multisample_rtv, kBlack, 0, nullptr);

  // Native draw: additive blending makes replaying this packet twice visible.
  context_.list()->OMSetRenderTargets(1, &first_rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(first_root.get());
  context_.list()->SetPipelineState(first_pipeline.get());
  context_.list()->IASetPrimitiveTopology(
      D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, kSize, kSize, 0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, kSize, kSize};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);

  // Predication deliberately selects the fallback replay path for dispatch.
  context_.list()->SetPredication(predicate.get(), 0,
                                   D3D12_PREDICATION_OP_EQUAL_ZERO);
  context_.list()->SetComputeRootSignature(compute_root.get());
  context_.list()->SetPipelineState(compute_pipeline.get());
  ID3D12DescriptorHeap *heaps[] = {srv_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootDescriptorTable(
      0, context_.GpuDescriptorHandle(srv_heap.get(), 0));
  context_.list()->Dispatch(1, 1, 1);
  context_.list()->SetPredication(nullptr, 0,
                                   D3D12_PREDICATION_OP_EQUAL_ZERO);
  D3D12TestContext::Transition(context_.list(), state.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  // The GPU copy carries the first compiled draw into the fallback draw.
  D3D12TestContext::Transition(context_.list(), first_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION copy_source = {};
  copy_source.pResource = first_target.get();
  copy_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION copy_destination = {};
  copy_destination.pResource = copied_draw.get();
  copy_destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  context_.list()->CopyTextureRegion(&copy_destination, 0, 0, 0, &copy_source,
                                     nullptr);
  D3D12TestContext::Transition(context_.list(), copied_draw.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  // A second predicated work command is an intentional fallback draw.
  context_.list()->OMSetRenderTargets(1, &multisample_rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(fallback_root.get());
  context_.list()->SetPipelineState(fallback_pipeline.get());
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetGraphicsRootDescriptorTable(
      0, context_.GpuDescriptorHandle(srv_heap.get(), 1));
  context_.list()->SetPredication(predicate.get(), 0,
                                   D3D12_PREDICATION_OP_EQUAL_ZERO);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  context_.list()->SetPredication(nullptr, 0,
                                   D3D12_PREDICATION_OP_EQUAL_ZERO);

  // Resolve the four identical samples and stage both observable results.
  D3D12TestContext::Transition(context_.list(), multisample_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  context_.list()->ResolveSubresource(resolved.get(), 0,
                                      multisample_target.get(), 0, kFormat);
  D3D12TestContext::Transition(context_.list(), resolved.get(),
                               D3D12_RESOURCE_STATE_RESOLVE_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION resolved_source = {};
  resolved_source.pResource = resolved.get();
  resolved_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION readback_destination = {};
  readback_destination.pResource = texture_readback.get();
  readback_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  readback_destination.PlacedFootprint = footprint;
  context_.list()->CopyTextureRegion(&readback_destination, 0, 0, 0,
                                     &resolved_source, nullptr);
  D3D12TestContext::Transition(context_.list(), state.get(),
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyBufferRegion(state_readback.get(), 0, state.get(), 0,
                                     sizeof(kZeroState));

  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                            IID_PPV_ARGS(fence.put())),
            S_OK);
  ASSERT_EQ(context_.list()->Close(), S_OK);
  ID3D12CommandList *submission = context_.list();
  context_.queue()->ExecuteCommandLists(1, &submission);
  constexpr UINT64 kFrameFence = 0x4d4958454446524dull;
  ASSERT_EQ(context_.queue()->Signal(fence.get(), kFrameFence), S_OK);
  ASSERT_EQ(context_.WaitForFence(fence.get(), kFrameFence), S_OK);
  EXPECT_GE(fence->GetCompletedValue(), kFrameFence);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);

  std::array<std::uint32_t, 2> state_values = {};
  void *mapped_state = nullptr;
  const D3D12_RANGE state_read_range = {0, sizeof(state_values)};
  ASSERT_EQ(state_readback->Map(0, &state_read_range, &mapped_state), S_OK);
  ASSERT_NE(mapped_state, nullptr);
  std::memcpy(state_values.data(), mapped_state, sizeof(state_values));
  const D3D12_RANGE no_write = {};
  state_readback->Unmap(0, &no_write);
  EXPECT_EQ(state_values[0], 64u);
  EXPECT_EQ(state_values[1], 1u) << "fallback dispatch must execute once";

  void *mapped_texture = nullptr;
  const D3D12_RANGE texture_read_range = {
      0, static_cast<SIZE_T>(texture_readback_size)};
  ASSERT_EQ(texture_readback->Map(0, &texture_read_range, &mapped_texture),
            S_OK);
  ASSERT_NE(mapped_texture, nullptr);
  constexpr std::uint32_t kExpectedPixel = 0x80804020u;
  UINT mismatch_count = 0;
  UINT first_mismatch_x = 0;
  UINT first_mismatch_y = 0;
  std::uint32_t first_mismatch_value = 0;
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  static_cast<const std::uint8_t *>(mapped_texture) +
                      footprint.Offset + y * footprint.Footprint.RowPitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      if (!ColorsMatch(pixel, kExpectedPixel, 1)) {
        if (!mismatch_count) {
          first_mismatch_x = x;
          first_mismatch_y = y;
          first_mismatch_value = pixel;
        }
        ++mismatch_count;
      }
    }
  }
  EXPECT_EQ(mismatch_count, 0u)
      << "first mismatch at (" << first_mismatch_x << ", "
      << first_mismatch_y << ") actual=0x" << std::hex
      << first_mismatch_value;
  texture_readback->Unmap(0, &no_write);
}

} // namespace
