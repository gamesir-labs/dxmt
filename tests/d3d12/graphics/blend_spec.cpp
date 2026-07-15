#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;

struct BlendCase {
  bool enabled;
  D3D12_BLEND source;
  D3D12_BLEND destination;
  D3D12_BLEND_OP operation;
  D3D12_BLEND source_alpha;
  D3D12_BLEND destination_alpha;
  D3D12_BLEND_OP alpha_operation;
  UINT8 write_mask;
  std::array<float, 4> expected;
  const char *name;
};

class GraphicsBlendSpec : public ::testing::TestWithParam<BlendCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto pixel_shader = CompileShader(R"(
      cbuffer Color : register(b0) { float4 value; };
      float4 main() : SV_Target { return value; })",
                                            "ps_5_0");
    ASSERT_EQ(pixel_shader.result, S_OK) << pixel_shader.diagnostic_text();

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.ShaderRegister = 0;
    parameter.Constants.Num32BitValues = 4;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    const auto &test_case = GetParam();
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = FullscreenVertexShader();
    desc.PS = {pixel_shader.bytecode->GetBufferPointer(),
               pixel_shader.bytecode->GetBufferSize()};
    auto &blend = desc.BlendState.RenderTarget[0];
    blend.BlendEnable = test_case.enabled;
    blend.SrcBlend = test_case.source;
    blend.DestBlend = test_case.destination;
    blend.BlendOp = test_case.operation;
    blend.SrcBlendAlpha = test_case.source_alpha;
    blend.DestBlendAlpha = test_case.destination_alpha;
    blend.BlendOpAlpha = test_case.alpha_operation;
    blend.RenderTargetWriteMask = test_case.write_mask;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(pipeline_.put())),
              S_OK);

    target_ =
        context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv_heap_ =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(target_);
    ASSERT_TRUE(rtv_heap_);
    rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target_.get(), nullptr, rtv_);
  }

  static std::uint32_t PackColor(const std::array<float, 4> &color) {
    std::uint32_t result = 0;
    for (UINT channel = 0; channel < color.size(); ++channel) {
      const auto value = static_cast<std::uint32_t>(
          std::lround(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f));
      result |= value << (channel * 8);
    }
    return result;
  }

public:
  static const char *Name(const ::testing::TestParamInfo<BlendCase> &info) {
    return info.param.name;
  }

protected:
  static constexpr UINT kSize = 8;
  static constexpr std::array<float, 4> kSource = {0.75f, 0.25f, 0.5f, 0.5f};
  static constexpr std::array<float, 4> kDestination = {0.25f, 0.5f, 0.75f,
                                                        0.25f};
  static constexpr std::array<float, 4> kBlendFactor = {0.25f, 0.25f, 0.25f,
                                                        0.25f};
  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12Resource> target_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
};

TEST_P(GraphicsBlendSpec, BlendEquationMatrix) {
  context_.list()->ClearRenderTargetView(rtv_, kDestination.data(), 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv_, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_signature_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  for (UINT i = 0; i < kSource.size(); ++i)
    context_.list()->SetGraphicsRoot32BitConstant(
        0, std::bit_cast<UINT>(kSource[i]), i);
  context_.list()->OMSetBlendFactor(kBlendFactor.data());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {
      0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
      0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, kSize, kSize};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context_.list(), target_.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target_.get(), &readback), S_OK);
  const auto expected = PackColor(GetParam().expected);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, expected, 2))
          << "pixel (" << x << ", " << y << ") actual=0x" << std::hex << pixel
          << " expected=0x" << expected;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    Equations, GraphicsBlendSpec,
    ::testing::Values(BlendCase{false,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ZERO,
                                D3D12_BLEND_OP_ADD,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ZERO,
                                D3D12_BLEND_OP_ADD,
                                D3D12_COLOR_WRITE_ENABLE_ALL,
                                {0.75f, 0.25f, 0.5f, 0.5f},
                                "Opaque"},
                      BlendCase{true,
                                D3D12_BLEND_SRC_ALPHA,
                                D3D12_BLEND_INV_SRC_ALPHA,
                                D3D12_BLEND_OP_ADD,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ZERO,
                                D3D12_BLEND_OP_ADD,
                                D3D12_COLOR_WRITE_ENABLE_ALL,
                                {0.5f, 0.375f, 0.625f, 0.5f},
                                "Alpha"},
                      BlendCase{true,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_ADD,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_ADD,
                                D3D12_COLOR_WRITE_ENABLE_ALL,
                                {1.0f, 0.75f, 1.0f, 0.75f},
                                "Add"},
                      BlendCase{true,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_SUBTRACT,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_SUBTRACT,
                                D3D12_COLOR_WRITE_ENABLE_ALL,
                                {0.5f, 0.0f, 0.0f, 0.25f},
                                "Subtract"},
                      BlendCase{true,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_REV_SUBTRACT,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_REV_SUBTRACT,
                                D3D12_COLOR_WRITE_ENABLE_ALL,
                                {0.0f, 0.25f, 0.25f, 0.0f},
                                "ReverseSubtract"},
                      BlendCase{true,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_MIN,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_MIN,
                                D3D12_COLOR_WRITE_ENABLE_ALL,
                                {0.25f, 0.25f, 0.5f, 0.25f},
                                "Min"},
                      BlendCase{true,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_MAX,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_MAX,
                                D3D12_COLOR_WRITE_ENABLE_ALL,
                                {0.75f, 0.5f, 0.75f, 0.5f},
                                "Max"},
                      BlendCase{true,
                                D3D12_BLEND_BLEND_FACTOR,
                                D3D12_BLEND_INV_BLEND_FACTOR,
                                D3D12_BLEND_OP_ADD,
                                D3D12_BLEND_BLEND_FACTOR,
                                D3D12_BLEND_INV_BLEND_FACTOR,
                                D3D12_BLEND_OP_ADD,
                                D3D12_COLOR_WRITE_ENABLE_ALL,
                                {0.375f, 0.4375f, 0.6875f, 0.3125f},
                                "ConstantFactor"},
                      BlendCase{true,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ZERO,
                                D3D12_BLEND_OP_ADD,
                                D3D12_BLEND_ZERO,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_OP_ADD,
                                D3D12_COLOR_WRITE_ENABLE_ALL,
                                {0.75f, 0.25f, 0.5f, 0.25f},
                                "SeparateAlpha"},
                      BlendCase{false,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ZERO,
                                D3D12_BLEND_OP_ADD,
                                D3D12_BLEND_ONE,
                                D3D12_BLEND_ZERO,
                                D3D12_BLEND_OP_ADD,
                                D3D12_COLOR_WRITE_ENABLE_RED |
                                    D3D12_COLOR_WRITE_ENABLE_BLUE,
                                {0.75f, 0.5f, 0.5f, 0.25f},
                                "WriteMaskRedBlue"}),
    GraphicsBlendSpec::Name);

} // namespace
