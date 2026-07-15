#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstring>
#include <limits>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

constexpr char kScalarVaryingVertexShader[] = R"(
struct Output {
  float4 position : SV_Position;
  float scalar : TEXCOORD1;
};

Output main(uint vertex_id : SV_VertexID) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  Output output;
  output.position = float4(positions[vertex_id], 0.0, 1.0);
  output.scalar = 0.25;
  return output;
}
)";

constexpr char kScalarVaryingPixelShader[] = R"(
float4 main(float scalar : TEXCOORD1) : SV_Target {
  return float4(scalar, 0.0, 0.0, 1.0);
}
)";

class D3D12ScalarVaryingSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
  }

  D3D12TestContext context_;
};

TEST_F(D3D12ScalarVaryingSpec, PreservesActiveScalarLaneAcrossGraphicsStages) {
  const auto vertex = CompileShader(kScalarVaryingVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kScalarVaryingPixelShader, "ps_5_0");
  ASSERT_TRUE(SUCCEEDED(vertex.result)) << vertex.diagnostic_text();
  ASSERT_TRUE(SUCCEEDED(pixel.result)) << pixel.diagnostic_text();

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root_signature.get();
  pipeline_desc.VS = {vertex.bytecode->GetBufferPointer(),
                      vertex.bytecode->GetBufferSize()};
  pipeline_desc.PS = {pixel.bytecode->GetBufferPointer(),
                      pixel.bytecode->GetBufferSize()};
  pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline_desc.SampleMask = std::numeric_limits<UINT>::max();
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline_desc.SampleDesc.Count = 1;

  dxmt::test::ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateGraphicsPipelineState(
      &pipeline_desc, IID_PPV_ARGS(pipeline.put()))));

  auto render_target = context_.CreateTexture2D(
      32, 32, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(render_target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(render_target.get(), nullptr, rtv);

  const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, 32, 32};
  const float clear_color[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), render_target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackTexture(render_target.get(), &readback)));
  ASSERT_EQ(readback.width, 32u);
  ASSERT_EQ(readback.height, 32u);

  std::uint32_t center = 0;
  std::memcpy(&center,
              readback.data.data() + 16 * readback.row_pitch +
                  16 * sizeof(center),
              sizeof(center));
  EXPECT_TRUE(ColorsMatch(center, 0xff000040u, 1))
      << "center pixel was 0x" << std::hex << center;
}

} // namespace
