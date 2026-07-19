#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_pipeline_stream.hpp"
#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ClearBufferComputeShader;
using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::CopyTextureComputeShader;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::PipelineSubobject;
using dxmt::test::TextureReadback;
using dxmt::test::TextureUavPixelShader;

class PipelineStreamSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
    ASSERT_TRUE(SUCCEEDED(context_.device()->QueryInterface(
        __uuidof(ID3D12Device2), reinterpret_cast<void **>(device2_.put()))));
  }

  HRESULT CreatePipeline(void *stream, SIZE_T size,
                         ID3D12PipelineState **pipeline) {
    D3D12_PIPELINE_STATE_STREAM_DESC desc = {size, stream};
    return device2_->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState),
                                         reinterpret_cast<void **>(pipeline));
  }

  template <typename Stream>
  HRESULT CreatePipeline(Stream &stream, ID3D12PipelineState **pipeline) {
    return CreatePipeline(&stream, sizeof(stream), pipeline);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12Device2> device2_;
};

TEST_F(PipelineStreamSpec, DuplicateSubobjectRejected) {
  struct Stream {
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,
                      D3D12_SHADER_BYTECODE>
        first;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,
                      D3D12_SHADER_BYTECODE>
        second;
  } stream;
  stream.first.value = CopyTextureComputeShader();
  stream.second.value = CopyTextureComputeShader();
  ComPtr<ID3D12PipelineState> pipeline;

  EXPECT_EQ(CreatePipeline(stream, pipeline.put()), E_INVALIDARG);
  EXPECT_FALSE(pipeline);
}

TEST_F(PipelineStreamSpec, ComputeAndGraphicsSubobjectsMixedRejected) {
  struct Stream {
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,
                      D3D12_SHADER_BYTECODE>
        compute;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,
                      D3D12_SHADER_BYTECODE>
        pixel;
  } stream;
  stream.compute.value = CopyTextureComputeShader();
  stream.pixel.value = TextureUavPixelShader();
  ComPtr<ID3D12PipelineState> pipeline;

  EXPECT_EQ(CreatePipeline(stream, pipeline.put()), E_INVALIDARG);
  EXPECT_FALSE(pipeline);
}

TEST_F(PipelineStreamSpec, ComputeStreamDispatches) {
  constexpr std::uint32_t kExpected = 0x13579bdf;
  constexpr std::size_t kElementCount = 64;

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.Num32BitValues = 1;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = std::size(parameters);
  root_desc.pParameters = parameters;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  struct Stream {
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
                      ID3D12RootSignature *>
        root_signature;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,
                      D3D12_SHADER_BYTECODE>
        compute;
  } stream;
  stream.root_signature.value = root_signature.get();
  stream.compute.value = ClearBufferComputeShader();
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_EQ(CreatePipeline(stream, pipeline.put()), S_OK);
  ASSERT_TRUE(pipeline);

  auto output = context_.CreateBuffer(
      kElementCount * sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kElementCount;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(std::size(heaps), heaps);
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetComputeRoot32BitConstant(0, kExpected, 0);
  context_.list()->SetComputeRootDescriptorTable(
      1, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(
                output.get(), kElementCount * sizeof(std::uint32_t), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), kElementCount * sizeof(std::uint32_t));
  for (std::size_t index = 0; index < kElementCount; ++index) {
    std::uint32_t actual = 0;
    std::memcpy(&actual, bytes.data() + index * sizeof(actual), sizeof(actual));
    EXPECT_EQ(actual, kExpected) << "element " << index;
  }
}

TEST_F(PipelineStreamSpec, GraphicsStreamDraws) {
  constexpr UINT kSize = 8;
  constexpr std::uint32_t kExpected = 0xffbf8040u;
  const auto pixel_shader = CompileShader(
      "float4 main() : SV_Target { return float4(0.25, 0.5, 0.75, 1); }",
      "ps_5_0");
  ASSERT_EQ(pixel_shader.result, S_OK) << pixel_shader.diagnostic_text();

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  struct Stream {
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
                      ID3D12RootSignature *>
        root_signature;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,
                      D3D12_SHADER_BYTECODE>
        vertex;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,
                      D3D12_SHADER_BYTECODE>
        pixel;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
                      D3D12_BLEND_DESC>
        blend;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
                      D3D12_RASTERIZER_DESC>
        rasterizer;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
                      D3D12_PRIMITIVE_TOPOLOGY_TYPE>
        topology;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
                      D3D12_RT_FORMAT_ARRAY>
        render_target_formats;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
                      DXGI_SAMPLE_DESC>
        sample_desc;
  } stream;
  stream.root_signature.value = root_signature.get();
  stream.vertex.value = FullscreenVertexShader();
  stream.pixel.value = {pixel_shader.bytecode->GetBufferPointer(),
                        pixel_shader.bytecode->GetBufferSize()};
  stream.blend.value.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  stream.rasterizer.value.FillMode = D3D12_FILL_MODE_SOLID;
  stream.rasterizer.value.CullMode = D3D12_CULL_MODE_NONE;
  stream.rasterizer.value.DepthClipEnable = TRUE;
  stream.topology.value = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  stream.render_target_formats.value.NumRenderTargets = 1;
  stream.render_target_formats.value.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  stream.sample_desc.value.Count = 1;
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_EQ(CreatePipeline(stream, pipeline.put()), S_OK);
  ASSERT_TRUE(pipeline);

  auto target =
      context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  constexpr std::array<float, 4> kClear = {};
  context_.list()->ClearRenderTargetView(rtv, kClear.data(), 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {
      0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
      0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, kSize, kSize};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_TRUE(ColorsMatch(actual, kExpected, 2))
          << "pixel (" << x << ", " << y << ") actual=0x" << std::hex << actual;
    }
  }
}

} // namespace
