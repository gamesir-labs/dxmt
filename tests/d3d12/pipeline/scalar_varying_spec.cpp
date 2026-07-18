#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include <d3d12shader.h>

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

constexpr char kWideVaryingVertexShader[] = R"(
struct Output {
  float4 position : SV_Position;
  float4 varying : TEXCOORD0;
};

Output main(uint vertex_id : SV_VertexID) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  Output output;
  output.position = float4(positions[vertex_id], 0.0, 1.0);
  output.varying = float4(0.25, 0.5, 0.75, 1.0);
  return output;
}
)";

constexpr char kNarrowVaryingPixelShader[] = R"(
float4 main(float2 varying : TEXCOORD0) : SV_Target {
  return float4(varying, 0.0, 1.0);
}
)";

constexpr char kNarrowVaryingVertexShader[] = R"(
struct Output {
  float4 position : SV_Position;
  float2 varying : TEXCOORD0;
};

Output main(uint vertex_id : SV_VertexID) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  Output output;
  output.position = float4(positions[vertex_id], 0.0, 1.0);
  output.varying = float2(0.25, 0.5);
  return output;
}
)";

constexpr char kWideVaryingPixelShader[] = R"(
float4 main(float4 varying : TEXCOORD0) : SV_Target {
  return varying;
}
)";

constexpr char kMissingSemanticPixelShader[] = R"(
float4 main(float4 varying : COLOR0) : SV_Target {
  return varying;
}
)";

constexpr char kIntegerVaryingPixelShader[] = R"(
float4 main(nointerpolation uint4 varying : TEXCOORD0) : SV_Target {
  return float4(varying);
}
)";

constexpr char kOrderedVaryingVertexShader[] = R"(
struct Output {
  float4 position : SV_Position;
  float2 first : TEXCOORD0;
  float2 second : TEXCOORD1;
};

Output main(uint vertex_id : SV_VertexID) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  Output output;
  output.position = float4(positions[vertex_id], 0.0, 1.0);
  output.first = float2(0.25, 0.5);
  output.second = float2(0.75, 1.0);
  return output;
}
)";

constexpr char kReversedVaryingPixelShader[] = R"(
float4 main(float2 second : TEXCOORD1,
            float2 first : TEXCOORD0) : SV_Target {
  return float4(first, second);
}
)";

constexpr char kPackedVaryingVertexShader[] = R"(
struct Output {
  float4 position : SV_Position;
  float3 first : TEXCOORD0;
  float packed : COLOR0;
  float2 boundary : TEXCOORD1;
};

Output main(uint vertex_id : SV_VertexID) {
  const float2 positions[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
  };
  Output output;
  output.position = float4(positions[vertex_id], 0.0, 1.0);
  output.first = float3(0.125, 0.25, 0.5);
  output.packed = 0.75;
  output.boundary = float2(0.375, 0.625);
  return output;
}
)";

constexpr char kPackedVaryingPixelShader[] = R"(
struct Input {
  float3 first : TEXCOORD0;
  float packed : COLOR0;
  float2 boundary : TEXCOORD1;
};

float4 main(Input input) : SV_Target {
  return float4(input.first.x, input.packed, input.boundary);
}
)";

class D3D12ScalarVaryingSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
  }

  void RenderAndExpectCenter(const char *vertex_source,
                             const char *pixel_source,
                             std::uint32_t expected) {
    const auto vertex = CompileShader(vertex_source, "vs_5_0");
    const auto pixel = CompileShader(pixel_source, "ps_5_0");
    ASSERT_TRUE(SUCCEEDED(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(SUCCEEDED(pixel.result)) << pixel.diagnostic_text();

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
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
    pipeline_desc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
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
    context_.device()->CreateRenderTargetView(render_target.get(), nullptr,
                                              rtv);

    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, 32, 32};
    const float clear_color[4] = {};
    context_.list()->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->SetGraphicsRootSignature(root_signature.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
    EXPECT_TRUE(ColorsMatch(center, expected, 1))
        << "center pixel was 0x" << std::hex << center;
  }

  void ExpectLinkageRejected(const char *vertex_source,
                             const char *pixel_source) {
    const auto vertex = CompileShader(vertex_source, "vs_5_0");
    const auto pixel = CompileShader(pixel_source, "ps_5_0");
    ASSERT_TRUE(SUCCEEDED(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(SUCCEEDED(pixel.result)) << pixel.diagnostic_text();

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
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
    pipeline_desc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.NumRenderTargets = 1;
    pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline_desc.SampleDesc.Count = 1;

    void *output = reinterpret_cast<void *>(std::uintptr_t{1});
    const HRESULT result = context_.device()->CreateGraphicsPipelineState(
        &pipeline_desc, __uuidof(ID3D12PipelineState), &output);
    EXPECT_EQ(result, E_INVALIDARG);
    EXPECT_EQ(output, nullptr);
    if (SUCCEEDED(result) && output)
      static_cast<ID3D12PipelineState *>(output)->Release();
  }

  D3D12TestContext context_;
};

TEST_F(D3D12ScalarVaryingSpec, PreservesActiveScalarLaneAcrossGraphicsStages) {
  RenderAndExpectCenter(kScalarVaryingVertexShader,
                        kScalarVaryingPixelShader, 0xff000040u);
}

TEST_F(D3D12ScalarVaryingSpec, AcceptsWiderProducerMaskThanConsumerMask) {
  RenderAndExpectCenter(kWideVaryingVertexShader,
                        kNarrowVaryingPixelShader, 0xff008040u);
}

TEST_F(D3D12ScalarVaryingSpec, RejectsConsumerMaskWiderThanProducerMask) {
  ExpectLinkageRejected(kNarrowVaryingVertexShader,
                        kWideVaryingPixelShader);
}

TEST_F(D3D12ScalarVaryingSpec, RejectsConsumerSemanticMissingFromProducer) {
  ExpectLinkageRejected(kWideVaryingVertexShader,
                        kMissingSemanticPixelShader);
}

TEST_F(D3D12ScalarVaryingSpec, RejectsConsumerComponentTypeMismatch) {
  ExpectLinkageRejected(kWideVaryingVertexShader,
                        kIntegerVaryingPixelShader);
}

TEST_F(D3D12ScalarVaryingSpec, RejectsReorderedConsumerSignature) {
  ExpectLinkageRejected(kOrderedVaryingVertexShader,
                        kReversedVaryingPixelShader);
}

TEST_F(D3D12ScalarVaryingSpec,
       PreservesPackedSemanticsAcrossRegisterBoundary) {
  const auto vertex = CompileShader(kPackedVaryingVertexShader, "vs_5_0");
  ASSERT_TRUE(SUCCEEDED(vertex.result)) << vertex.diagnostic_text();

  dxmt::test::ComPtr<ID3D12ShaderReflection> reflection;
  ASSERT_EQ(D3DReflect(vertex.bytecode->GetBufferPointer(),
                       vertex.bytecode->GetBufferSize(),
                       __uuidof(ID3D12ShaderReflection),
                       reinterpret_cast<void **>(reflection.put())),
            S_OK);
  D3D12_SHADER_DESC shader_desc = {};
  ASSERT_EQ(reflection->GetDesc(&shader_desc), S_OK);

  D3D12_SIGNATURE_PARAMETER_DESC first = {};
  D3D12_SIGNATURE_PARAMETER_DESC packed = {};
  D3D12_SIGNATURE_PARAMETER_DESC boundary = {};
  bool found_first = false;
  bool found_packed = false;
  bool found_boundary = false;
  for (UINT index = 0; index < shader_desc.OutputParameters; ++index) {
    D3D12_SIGNATURE_PARAMETER_DESC parameter = {};
    ASSERT_EQ(reflection->GetOutputParameterDesc(index, &parameter), S_OK);
    if (!std::strcmp(parameter.SemanticName, "TEXCOORD") &&
        parameter.SemanticIndex == 0) {
      first = parameter;
      found_first = true;
    } else if (!std::strcmp(parameter.SemanticName, "COLOR") &&
               parameter.SemanticIndex == 0) {
      packed = parameter;
      found_packed = true;
    } else if (!std::strcmp(parameter.SemanticName, "TEXCOORD") &&
               parameter.SemanticIndex == 1) {
      boundary = parameter;
      found_boundary = true;
    }
  }
  ASSERT_TRUE(found_first);
  ASSERT_TRUE(found_packed);
  ASSERT_TRUE(found_boundary);
  EXPECT_EQ(first.Register, packed.Register);
  EXPECT_EQ(first.Mask & packed.Mask, 0u);
  EXPECT_EQ(first.Mask | packed.Mask, 0xfu);
  EXPECT_NE(boundary.Register, first.Register);

  RenderAndExpectCenter(kPackedVaryingVertexShader,
                        kPackedVaryingPixelShader, 0x9f60bf20u);
}

} // namespace
