#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;

class ExecutionPathBoundarySpec : public ::testing::Test {
protected:
  enum class DispatchPath {
    Native,
    PredicatedFallback,
    NativeFallbackNative,
  };

  void RunDescriptorDispatch(DispatchPath path,
                             std::vector<std::uint32_t> *values) {
    D3D12TestContext context;
    ASSERT_TRUE(SUCCEEDED(context.Initialize()));

    const auto shader = CompileShader(R"(
      Buffer<uint> input : register(t0);
      RWBuffer<uint> output : register(u0);

      [numthreads(8, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID) {
        output[id.x] += input[id.x];
      }
    )",
                                      "cs_5_0");
    ASSERT_TRUE(SUCCEEDED(shader.result)) << shader.diagnostic_text();

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
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    auto root_signature = context.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature);

    const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                            shader.bytecode->GetBufferSize()};
    auto pipeline =
        context.CreateComputePipeline(root_signature.get(), bytecode);
    ASSERT_TRUE(pipeline);

    auto upload = context.CreateUploadBuffer(kInput.size() * sizeof(kInput[0]),
                                             kInput.data(), sizeof(kInput));
    auto input = context.CreateBuffer(sizeof(kInput), D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
    auto output =
        context.CreateBuffer(sizeof(kInput), D3D12_HEAP_TYPE_DEFAULT,
                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto heap = context.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(input);
    ASSERT_TRUE(output);
    ASSERT_TRUE(heap);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = kInput.size();
    context.device()->CreateShaderResourceView(
        input.get(), &srv, context.CpuDescriptorHandle(heap.get(), 0));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = kInput.size();
    context.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        context.CpuDescriptorHandle(heap.get(), 1));

    context.list()->CopyBufferRegion(input.get(), 0, upload.get(), 0,
                                     sizeof(kInput));
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context.list()->SetDescriptorHeaps(1, heaps);
    const UINT clear[4] = {};
    context.list()->ClearUnorderedAccessViewUint(
        context.GpuDescriptorHandle(heap.get(), 1),
        context.CpuDescriptorHandle(heap.get(), 1), output.get(), clear, 0,
        nullptr);
    context.list()->SetComputeRootSignature(root_signature.get());
    context.list()->SetPipelineState(pipeline.get());
    context.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    D3D12TestContext::Transition(
        context.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ComPtr<ID3D12Resource> predicate;
    if (path == DispatchPath::PredicatedFallback) {
      constexpr UINT64 kExecute = 0;
      predicate = context.CreateUploadBuffer(sizeof(kExecute), &kExecute,
                                             sizeof(kExecute));
      ASSERT_TRUE(predicate);
      context.list()->SetPredication(predicate.get(), 0,
                                     D3D12_PREDICATION_OP_EQUAL_ZERO);
    }
    context.list()->Dispatch(1, 1, 1);
    if (path == DispatchPath::PredicatedFallback) {
      context.list()->SetPredication(nullptr, 0,
                                     D3D12_PREDICATION_OP_EQUAL_ZERO);
    } else if (path == DispatchPath::NativeFallbackNative) {
      D3D12TestContext::UavBarrier(context.list(), output.get());
      context.list()->Dispatch(1, 1, 1);
    }

    D3D12TestContext::Transition(context.list(), output.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    ASSERT_EQ(context.ReadbackBuffer(output.get(), sizeof(kInput), &bytes),
              S_OK);
    ASSERT_EQ(bytes.size(), sizeof(kInput));
    values->resize(kInput.size());
    std::memcpy(values->data(), bytes.data(), bytes.size());
  }

  static constexpr std::array<std::uint32_t, 8> kInput = {1,  3,  7,   15,
                                                          31, 63, 127, 255};
};

class DescriptorParitySpec : public ExecutionPathBoundarySpec {};

TEST_F(DescriptorParitySpec, NativeAndFallbackResultsMatch) {
  std::vector<std::uint32_t> native;
  std::vector<std::uint32_t> fallback;
  ASSERT_NO_FATAL_FAILURE(RunDescriptorDispatch(DispatchPath::Native, &native));
  ASSERT_NO_FATAL_FAILURE(
      RunDescriptorDispatch(DispatchPath::PredicatedFallback, &fallback));

  EXPECT_EQ(native, fallback);
  EXPECT_EQ(native, (std::vector<std::uint32_t>(kInput.begin(), kInput.end())));
}

TEST_F(ExecutionPathBoundarySpec, PreservesDescriptorState) {
  std::vector<std::uint32_t> actual;
  ASSERT_NO_FATAL_FAILURE(
      RunDescriptorDispatch(DispatchPath::NativeFallbackNative, &actual));

  std::vector<std::uint32_t> expected;
  expected.reserve(kInput.size());
  for (const auto value : kInput)
    expected.push_back(value * 2);
  EXPECT_EQ(actual, expected);
}

TEST_F(ExecutionPathBoundarySpec, FlushesPendingBarriers) {
  std::vector<std::uint32_t> actual;
  ASSERT_NO_FATAL_FAILURE(RunDescriptorDispatch(DispatchPath::Native, &actual));

  EXPECT_EQ(actual, (std::vector<std::uint32_t>(kInput.begin(), kInput.end())));
}

TEST_F(ExecutionPathBoundarySpec, DoesNotReplayDrawTwice) {
  D3D12TestContext context;
  ASSERT_TRUE(SUCCEEDED(context.Initialize()));

  const auto pixel_shader = CompileShader(R"(
    cbuffer Color : register(b0) { float value; };
    float4 main() : SV_Target { return value.xxxx; }
  )",
                                          "ps_5_0");
  ASSERT_TRUE(SUCCEEDED(pixel_shader.result)) << pixel_shader.diagnostic_text();

  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.ShaderRegister = 0;
  parameter.Constants.Num32BitValues = 1;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root_signature = context.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root_signature.get();
  pipeline_desc.VS = FullscreenVertexShader();
  pipeline_desc.PS = {pixel_shader.bytecode->GetBufferPointer(),
                      pixel_shader.bytecode->GetBufferSize()};
  auto &blend = pipeline_desc.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_ONE;
  blend.DestBlend = D3D12_BLEND_ONE;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.DestBlendAlpha = D3D12_BLEND_ONE;
  blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline_desc.SampleMask = UINT_MAX;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline_desc.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_EQ(context.device()->CreateGraphicsPipelineState(
                &pipeline_desc, IID_PPV_ARGS(pipeline.put())),
            S_OK);

  constexpr UINT kSize = 8;
  auto target =
      context.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                              D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                              D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap =
      context.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  const FLOAT clear[4] = {};
  context.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context.list()->SetGraphicsRootSignature(root_signature.get());
  context.list()->SetPipelineState(pipeline.get());
  context.list()->SetGraphicsRoot32BitConstant(0, 0x3e800000, 0);
  context.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, kSize, kSize, 0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, kSize, kSize};
  context.list()->RSSetViewports(1, &viewport);
  context.list()->RSSetScissorRects(1, &scissor);

  // State and clear records form a fallback segment, the draw is compiled,
  // and the transition returns to fallback. Additive blending exposes replay.
  context.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context.ReadbackTexture(target.get(), &readback), S_OK);
  constexpr std::uint32_t kQuarterGray = 0x40404040;
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, kQuarterGray, 1))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex << pixel;
    }
  }
}

} // namespace
