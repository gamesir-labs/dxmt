#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstring>
#include <limits>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::DescriptorTablePixelShader;
using dxmt::test::CopyTextureComputeShader;
using dxmt::test::TextureReadback;
using dxmt::test::TextureUavPixelShader;

struct RenderTarget {
  ComPtr<ID3D12Resource> texture;
  ComPtr<ID3D12DescriptorHeap> heap;
  D3D12_CPU_DESCRIPTOR_HANDLE view = {};
  D3D12_VIEWPORT viewport = {};
  D3D12_RECT scissor = {};
};

RenderTarget CreateRenderTarget(D3D12TestContext &context) {
  RenderTarget target;
  target.texture = context.CreateTexture2D(
      32, 32, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  target.heap = context.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                             false);
  if (!target.texture || !target.heap)
    return target;

  target.view = target.heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.texture.get(), nullptr,
                                           target.view);
  target.viewport.Width = 32.0f;
  target.viewport.Height = 32.0f;
  target.viewport.MaxDepth = 1.0f;
  target.scissor.right = 32;
  target.scissor.bottom = 32;
  return target;
}

void ExpectSolidColor(const TextureReadback &readback, std::uint32_t expected,
                      unsigned int max_channel_difference) {
  ASSERT_EQ(readback.width, 32u);
  ASSERT_EQ(readback.height, 32u);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      ASSERT_TRUE(ColorsMatch(actual, expected, max_channel_difference))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex
          << actual << ", expected 0x" << expected;
    }
  }
}

class D3D12DescriptorSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
  }

  D3D12TestContext context_;
};

void RunDescriptorTableDraw(D3D12TestContext &context, bool execute_indirect) {
  RenderTarget render_target = CreateRenderTarget(context);
  ASSERT_TRUE(render_target.texture);
  ASSERT_TRUE(render_target.heap);

  D3D12_DESCRIPTOR_RANGE ranges[4] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 2;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 1;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[2].NumDescriptors = 2;
  ranges[2].BaseShaderRegister = 2;
  ranges[2].OffsetInDescriptorsFromTableStart = 0;
  ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  ranges[3].NumDescriptors = 1;
  ranges[3].BaseShaderRegister = 0;
  ranges[3].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER parameters[3] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[2].DescriptorTable.NumDescriptorRanges = 2;
  parameters[2].DescriptorTable.pDescriptorRanges = &ranges[2];
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 3;
  root_desc.pParameters = parameters;
  ComPtr<ID3D12RootSignature> root_signature =
      context.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  ComPtr<ID3D12PipelineState> pipeline = context.CreateGraphicsPipeline(
      root_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM,
      DescriptorTablePixelShader());
  ASSERT_TRUE(pipeline);

  std::array<std::uint8_t, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>
      constant_data = {};
  const std::array<float, 4> constant = {0.1f, 0.2f, 0.3f, 0.1f};
  std::memcpy(constant_data.data(), constant.data(), sizeof(constant));
  ComPtr<ID3D12Resource> constant_buffer = context.CreateUploadBuffer(
      constant_data.size(), constant_data.data(), constant_data.size());
  ASSERT_TRUE(constant_buffer);

  ComPtr<ID3D12CommandSignature> command_signature;
  ComPtr<ID3D12Resource> argument_buffer;
  struct IndirectArguments {
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer;
    D3D12_DRAW_ARGUMENTS draw;
  };
  if (execute_indirect) {
    D3D12_INDIRECT_ARGUMENT_DESC argument_descs[2] = {};
    argument_descs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
    argument_descs[0].VertexBuffer.Slot = 0;
    argument_descs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
    signature_desc.ByteStride = sizeof(IndirectArguments);
    signature_desc.NumArgumentDescs = 2;
    signature_desc.pArgumentDescs = argument_descs;
    HRESULT hr = context.device()->CreateCommandSignature(
        &signature_desc, nullptr, __uuidof(ID3D12CommandSignature),
        reinterpret_cast<void **>(command_signature.put()));
    ASSERT_TRUE(SUCCEEDED(hr));

    IndirectArguments arguments = {};
    arguments.vertex_buffer.BufferLocation =
        constant_buffer->GetGPUVirtualAddress();
    arguments.vertex_buffer.SizeInBytes = sizeof(constant);
    arguments.vertex_buffer.StrideInBytes = sizeof(constant);
    arguments.draw.VertexCountPerInstance = 3;
    arguments.draw.InstanceCount = 1;
    argument_buffer = context.CreateUploadBuffer(sizeof(arguments), &arguments,
                                                 sizeof(arguments));
    ASSERT_TRUE(argument_buffer);
  }

  ComPtr<ID3D12DescriptorHeap> resource_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 6, true);
  ComPtr<ID3D12DescriptorHeap> sampler_heap = context.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  ASSERT_TRUE(resource_heap);
  ASSERT_TRUE(sampler_heap);

  const std::array<std::uint32_t, 4> texture_data = {
      0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffff00};
  std::array<ComPtr<ID3D12Resource>, 4> textures;
  for (std::size_t i = 0; i < textures.size(); ++i) {
    textures[i] = context.CreateTexture2D(
        1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(textures[i]);
    ASSERT_TRUE(SUCCEEDED(context.UploadTextureAndReset(
        textures[i].get(), &texture_data[i], sizeof(texture_data[i]),
        sizeof(texture_data[i]))));
  }

  for (auto &texture : textures) {
    D3D12TestContext::Transition(
        context.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }

  for (UINT i = 0; i < textures.size(); ++i) {
    context.device()->CreateShaderResourceView(
        textures[i].get(), nullptr,
        context.CpuDescriptorHandle(resource_heap.get(), i + 1));
  }
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
  cbv_desc.BufferLocation = constant_buffer->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = constant_data.size();
  context.device()->CreateConstantBufferView(
      &cbv_desc, context.CpuDescriptorHandle(resource_heap.get(), 5));

  D3D12_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  context.device()->CreateSampler(
      &sampler_desc, sampler_heap->GetCPUDescriptorHandleForHeapStart());

  const float clear_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  context.list()->ClearRenderTargetView(render_target.view, clear_color, 0,
                                        nullptr);
  context.list()->OMSetRenderTargets(1, &render_target.view, FALSE, nullptr);
  context.list()->SetGraphicsRootSignature(root_signature.get());
  context.list()->SetPipelineState(pipeline.get());
  ID3D12DescriptorHeap *heaps[] = {resource_heap.get(), sampler_heap.get()};
  context.list()->SetDescriptorHeaps(2, heaps);
  context.list()->SetGraphicsRootDescriptorTable(
      0, context.GpuDescriptorHandle(resource_heap.get(), 0));
  context.list()->SetGraphicsRootDescriptorTable(
      1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
  context.list()->SetGraphicsRootDescriptorTable(
      2, context.GpuDescriptorHandle(resource_heap.get(), 3));
  context.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context.list()->RSSetViewports(1, &render_target.viewport);
  context.list()->RSSetScissorRects(1, &render_target.scissor);
  if (execute_indirect) {
    context.list()->ExecuteIndirect(command_signature.get(), 1,
                                    argument_buffer.get(), 0, nullptr, 0);
  } else {
    context.list()->DrawInstanced(3, 1, 0, 0);
  }

  D3D12TestContext::Transition(
      context.list(), render_target.texture.get(),
      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context.ReadbackTexture(render_target.texture.get(), &readback)));
  ExpectSolidColor(readback, 0xb2664c19, 2);
}

TEST_F(D3D12DescriptorSpec, DrawsWithSplitDescriptorTables) {
  RunDescriptorTableDraw(context_, false);
}

TEST_F(D3D12DescriptorSpec, DrawsIndirectWithSplitDescriptorTables) {
  RunDescriptorTableDraw(context_, true);
}

TEST_F(D3D12DescriptorSpec, CopiesTextureThroughComputeDescriptorTable) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 1;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;

  ComPtr<ID3D12RootSignature> root_signature =
      context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  ComPtr<ID3D12PipelineState> pipeline = context_.CreateComputePipeline(
      root_signature.get(), CopyTextureComputeShader());
  ASSERT_TRUE(pipeline);

  const std::array<float, 4> expected = {1.0f, 2.0f, 3.0f, 4.0f};
  std::array<std::array<float, 4>, 16> input;
  input.fill(expected);
  ComPtr<ID3D12Resource> source = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12Resource> destination = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      source.get(), input.data(), 4 * sizeof(input[0]), sizeof(input))));
  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  ComPtr<ID3D12DescriptorHeap> heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(heap);
  context_.device()->CreateShaderResourceView(
      source.get(), nullptr, context_.CpuDescriptorHandle(heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      destination.get(), nullptr, nullptr,
      context_.CpuDescriptorHandle(heap.get(), 1));
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), destination.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(
      SUCCEEDED(context_.ReadbackTexture(destination.get(), &readback)));
  for (UINT y = 0; y < 4; ++y) {
    for (UINT x = 0; x < 4; ++x) {
      std::array<float, 4> actual = {};
      std::memcpy(actual.data(),
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected) << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(D3D12DescriptorSpec, CopiesTextureUavIntoFallbackDescriptorTable) {
  RenderTarget render_target = CreateRenderTarget(context_);
  ASSERT_TRUE(render_target.texture);
  ASSERT_TRUE(render_target.heap);

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = std::numeric_limits<UINT>::max();
  range.BaseShaderRegister = 1;
  range.OffsetInDescriptorsFromTableStart = 0;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  ComPtr<ID3D12RootSignature> root_signature =
      context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  ComPtr<ID3D12PipelineState> pipeline = context_.CreateGraphicsPipeline(
      root_signature.get(), DXGI_FORMAT_R8G8B8A8_UNORM,
      TextureUavPixelShader());
  ASSERT_TRUE(pipeline);

  const float value = 1.0f;
  ComPtr<ID3D12Resource> texture = context_.CreateTexture2D(
      1, 1, 1, DXGI_FORMAT_R32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      texture.get(), &value, sizeof(value), sizeof(value))));
  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  ComPtr<ID3D12DescriptorHeap> cpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
  ComPtr<ID3D12DescriptorHeap> gpu_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(cpu_heap);
  ASSERT_TRUE(gpu_heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  context_.device()->CreateUnorderedAccessView(
      texture.get(), nullptr, &uav_desc,
      cpu_heap->GetCPUDescriptorHandleForHeapStart());
  context_.device()->CopyDescriptorsSimple(
      1, gpu_heap->GetCPUDescriptorHandleForHeapStart(),
      cpu_heap->GetCPUDescriptorHandleForHeapStart(),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  ID3D12DescriptorHeap *heaps[] = {gpu_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetGraphicsRootDescriptorTable(
      0, gpu_heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetGraphicsRoot32BitConstant(1, 0, 0);
  context_.list()->OMSetRenderTargets(1, &render_target.view, FALSE, nullptr);
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->RSSetViewports(1, &render_target.viewport);
  context_.list()->RSSetScissorRects(1, &render_target.scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), render_target.texture.get(),
      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackTexture(render_target.texture.get(), &readback)));
  ExpectSolidColor(readback, 0xffffffff, 0);
}

} // namespace
