#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;

struct LogicOpCase {
  D3D12_LOGIC_OP operation;
  const char *name;
};

class GraphicsLogicOpSpec : public ::testing::TestWithParam<LogicOpCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)),
              S_OK);

    D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support = {
        DXGI_FORMAT_R8G8B8A8_UNORM};
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_FORMAT_SUPPORT, &format_support,
                  sizeof(format_support)),
              S_OK);
    if (!options.OutputMergerLogicOp) {
      ASSERT_EQ(format_support.Support2 &
                    D3D12_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP,
                0u);
      GTEST_SKIP() << "output-merger logic operations are not supported";
    }
    if ((format_support.Support2 &
         D3D12_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP) == 0) {
      GTEST_SKIP() << "R8G8B8A8_UNORM does not advertise logic operations";
    }

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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = FullscreenVertexShader();
    desc.PS = {pixel_shader.bytecode->GetBufferPointer(),
               pixel_shader.bytecode->GetBufferSize()};
    auto &blend = desc.BlendState.RenderTarget[0];
    blend.LogicOpEnable = TRUE;
    blend.LogicOp = GetParam().operation;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    const HRESULT create_hr = context_.device()->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(pipeline_.put()));
    ASSERT_EQ(create_hr, S_OK);

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

  static std::uint32_t Apply(D3D12_LOGIC_OP operation, std::uint32_t source,
                             std::uint32_t destination) {
    switch (operation) {
    case D3D12_LOGIC_OP_CLEAR:
      return 0;
    case D3D12_LOGIC_OP_SET:
      return UINT32_MAX;
    case D3D12_LOGIC_OP_COPY:
      return source;
    case D3D12_LOGIC_OP_COPY_INVERTED:
      return ~source;
    case D3D12_LOGIC_OP_NOOP:
      return destination;
    case D3D12_LOGIC_OP_INVERT:
      return ~destination;
    case D3D12_LOGIC_OP_AND:
      return source & destination;
    case D3D12_LOGIC_OP_NAND:
      return ~(source & destination);
    case D3D12_LOGIC_OP_OR:
      return source | destination;
    case D3D12_LOGIC_OP_NOR:
      return ~(source | destination);
    case D3D12_LOGIC_OP_XOR:
      return source ^ destination;
    case D3D12_LOGIC_OP_EQUIV:
      return ~(source ^ destination);
    case D3D12_LOGIC_OP_AND_REVERSE:
      return source & ~destination;
    case D3D12_LOGIC_OP_AND_INVERTED:
      return ~source & destination;
    case D3D12_LOGIC_OP_OR_REVERSE:
      return source | ~destination;
    case D3D12_LOGIC_OP_OR_INVERTED:
      return ~source | destination;
    default:
      return 0;
    }
  }

public:
  static const char *Name(const ::testing::TestParamInfo<LogicOpCase> &info) {
    return info.param.name;
  }

protected:
  static constexpr UINT kSize = 8;
  // The four channels cover every (source, destination) truth-table input:
  // (1, 0), (0, 1), (1, 1), and (0, 0).
  static constexpr std::array<float, 4> kSource = {1.0f, 0.0f, 1.0f, 0.0f};
  static constexpr std::array<float, 4> kDestination = {0.0f, 1.0f, 1.0f, 0.0f};
  static constexpr std::uint32_t kPackedSource = 0x00ff00ff;
  static constexpr std::uint32_t kPackedDestination = 0x00ffff00;
  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12Resource> target_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
};

TEST_P(GraphicsLogicOpSpec, ProducesBitwiseResult) {
  context_.list()->ClearRenderTargetView(rtv_, kDestination.data(), 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv_, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_signature_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  for (UINT i = 0; i < kSource.size(); ++i) {
    context_.list()->SetGraphicsRoot32BitConstant(
        0, std::bit_cast<UINT>(kSource[i]), i);
  }
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
  const std::uint32_t expected =
      Apply(GetParam().operation, kPackedSource, kPackedDestination);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected)
          << "pixel (" << x << ", " << y << ") actual=0x" << std::hex << actual
          << " expected=0x" << expected;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    Operations, GraphicsLogicOpSpec,
    ::testing::Values(LogicOpCase{D3D12_LOGIC_OP_CLEAR, "Clear"},
                      LogicOpCase{D3D12_LOGIC_OP_SET, "Set"},
                      LogicOpCase{D3D12_LOGIC_OP_COPY, "Copy"},
                      LogicOpCase{D3D12_LOGIC_OP_COPY_INVERTED, "CopyInverted"},
                      LogicOpCase{D3D12_LOGIC_OP_NOOP, "Noop"},
                      LogicOpCase{D3D12_LOGIC_OP_INVERT, "Invert"},
                      LogicOpCase{D3D12_LOGIC_OP_AND, "And"},
                      LogicOpCase{D3D12_LOGIC_OP_NAND, "Nand"},
                      LogicOpCase{D3D12_LOGIC_OP_OR, "Or"},
                      LogicOpCase{D3D12_LOGIC_OP_NOR, "Nor"},
                      LogicOpCase{D3D12_LOGIC_OP_XOR, "Xor"},
                      LogicOpCase{D3D12_LOGIC_OP_EQUIV, "Equiv"},
                      LogicOpCase{D3D12_LOGIC_OP_AND_REVERSE, "AndReverse"},
                      LogicOpCase{D3D12_LOGIC_OP_AND_INVERTED, "AndInverted"},
                      LogicOpCase{D3D12_LOGIC_OP_OR_REVERSE, "OrReverse"},
                      LogicOpCase{D3D12_LOGIC_OP_OR_INVERTED, "OrInverted"}),
    GraphicsLogicOpSpec::Name);

} // namespace
