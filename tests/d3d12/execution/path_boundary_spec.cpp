#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <cstring>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;

TEST(ExecutionPathBoundarySpec, DoesNotReplayDrawTwice) {
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
