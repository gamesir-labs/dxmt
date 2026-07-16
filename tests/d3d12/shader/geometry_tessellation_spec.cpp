#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <limits>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::ShaderCompilation;
using dxmt::test::TextureReadback;

class ShaderAdvancedStageSpec : public ::testing::Test {
protected:
  static constexpr UINT kSize = 16;

  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_ = context_.CreateRootSignature(root_desc);
    target_ =
        context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv_heap_ =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(root_);
    ASSERT_TRUE(target_);
    ASSERT_TRUE(rtv_heap_);
    rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target_.get(), nullptr, rtv_);
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(const ShaderCompilation &vs, const ShaderCompilation &ps,
                 const ShaderCompilation *gs = nullptr,
                 const ShaderCompilation *hs = nullptr,
                 const ShaderCompilation *ds = nullptr) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.VS = {vs.bytecode->GetBufferPointer(), vs.bytecode->GetBufferSize()};
    desc.PS = {ps.bytecode->GetBufferPointer(), ps.bytecode->GetBufferSize()};
    if (gs)
      desc.GS = {gs->bytecode->GetBufferPointer(),
                 gs->bytecode->GetBufferSize()};
    if (hs)
      desc.HS = {hs->bytecode->GetBufferPointer(),
                 hs->bytecode->GetBufferSize()};
    if (ds)
      desc.DS = {ds->bytecode->GetBufferPointer(),
                 ds->bytecode->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.SampleMask = std::numeric_limits<UINT>::max();
    desc.PrimitiveTopologyType = hs ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH
                                    : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(pipeline.put())),
              S_OK);
    return pipeline;
  }

  void DrawAndExpectCenter(ID3D12PipelineState *pipeline,
                           D3D12_PRIMITIVE_TOPOLOGY topology) {
    constexpr FLOAT clear[4] = {};
    context_.list()->ClearRenderTargetView(rtv_, clear, 0, nullptr);
    context_.list()->OMSetRenderTargets(1, &rtv_, FALSE, nullptr);
    context_.list()->SetGraphicsRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->IASetPrimitiveTopology(topology);
    const D3D12_VIEWPORT viewport = {0.0f,         0.0f, float(kSize),
                                     float(kSize), 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, LONG(kSize), LONG(kSize)};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);
    D3D12TestContext::Transition(context_.list(), target_.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(target_.get(), &readback), S_OK);
    std::uint32_t center = 0;
    std::memcpy(&center,
                readback.data.data() + (kSize / 2) * readback.row_pitch +
                    (kSize / 2) * sizeof(center),
                sizeof(center));
    EXPECT_TRUE(ColorsMatch(center, 0xffffffffu, 1));
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12Resource> target_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
};

TEST_F(ShaderAdvancedStageSpec, GeometryShaderEmitsTrianglePrimitive) {
  const auto vs = CompileShader(R"(
    struct Output { float4 position : SV_Position; };
    Output main(uint id : SV_VertexID) {
      float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      Output output;
      output.position = float4(positions[id], 0.0, 1.0);
      return output;
    })",
                                "vs_5_0");
  const auto gs = CompileShader(R"(
    struct Data { float4 position : SV_Position; };
    [maxvertexcount(3)]
    void main(triangle Data input[3], inout TriangleStream<Data> stream) {
      stream.Append(input[0]);
      stream.Append(input[1]);
      stream.Append(input[2]);
      stream.RestartStrip();
    })",
                                "gs_5_0");
  const auto ps =
      CompileShader("float4 main() : SV_Target { return 1.0.xxxx; }", "ps_5_0");
  ASSERT_EQ(vs.result, S_OK) << vs.diagnostic_text();
  ASSERT_EQ(gs.result, S_OK) << gs.diagnostic_text();
  ASSERT_EQ(ps.result, S_OK) << ps.diagnostic_text();
  auto pipeline = CreatePipeline(vs, ps, &gs);
  ASSERT_TRUE(pipeline);
  DrawAndExpectCenter(pipeline.get(), D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

TEST_F(ShaderAdvancedStageSpec, HullAndDomainShadersTessellateTriangle) {
  const auto vs = CompileShader(R"(
    struct Control { float2 position : POSITION; };
    Control main(uint id : SV_VertexID) {
      float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      Control output;
      output.position = positions[id];
      return output;
    })",
                                "vs_5_0");
  const auto hs = CompileShader(R"(
    struct Control { float2 position : POSITION; };
    struct Constants {
      float edges[3] : SV_TessFactor;
      float inside : SV_InsideTessFactor;
    };
    Constants PatchConstants(InputPatch<Control, 3> patch) {
      Constants output;
      output.edges[0] = 1.0;
      output.edges[1] = 1.0;
      output.edges[2] = 1.0;
      output.inside = 1.0;
      return output;
    }
    [domain("tri")]
    [partitioning("integer")]
    [outputtopology("triangle_cw")]
    [outputcontrolpoints(3)]
    [patchconstantfunc("PatchConstants")]
    Control main(InputPatch<Control, 3> patch,
                 uint id : SV_OutputControlPointID) {
      return patch[id];
    })",
                                "hs_5_0");
  const auto ds = CompileShader(R"(
    struct Control { float2 position : POSITION; };
    struct Constants {
      float edges[3] : SV_TessFactor;
      float inside : SV_InsideTessFactor;
    };
    [domain("tri")]
    float4 main(Constants constants, float3 bary : SV_DomainLocation,
                const OutputPatch<Control, 3> patch) : SV_Position {
      float2 position = patch[0].position * bary.x +
                        patch[1].position * bary.y +
                        patch[2].position * bary.z;
      return float4(position, 0.0, 1.0);
    })",
                                "ds_5_0");
  const auto ps =
      CompileShader("float4 main() : SV_Target { return 1.0.xxxx; }", "ps_5_0");
  ASSERT_EQ(vs.result, S_OK) << vs.diagnostic_text();
  ASSERT_EQ(hs.result, S_OK) << hs.diagnostic_text();
  ASSERT_EQ(ds.result, S_OK) << ds.diagnostic_text();
  ASSERT_EQ(ps.result, S_OK) << ps.diagnostic_text();
  auto pipeline = CreatePipeline(vs, ps, nullptr, &hs, &ds);
  ASSERT_TRUE(pipeline);
  DrawAndExpectCenter(pipeline.get(),
                      D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
}

} // namespace
