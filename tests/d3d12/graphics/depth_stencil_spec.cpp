#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <utility>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct DepthCompareCase {
  D3D12_COMPARISON_FUNC function;
  float incoming;
  float stored;
  bool passes;
  const char *name;
};

struct StencilCompareCase {
  D3D12_COMPARISON_FUNC function;
  UINT8 reference;
  UINT8 stored;
  bool passes;
  const char *name;
};

struct StencilOperationCase {
  D3D12_STENCIL_OP operation;
  UINT8 initial;
  UINT8 reference;
  UINT8 expected;
  const char *name;
};

class DepthStencilFixture : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    auto vertex_shader = CompileShader(R"(
      cbuffer Depth : register(b0) { float depth; };
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[vertex_id], depth, 1.0);
      })",
                                       "vs_5_0");
    ASSERT_EQ(vertex_shader.result, S_OK) << vertex_shader.diagnostic_text();
    vertex_shader_ = std::move(vertex_shader.bytecode);
    auto pixel_shader = CompileShader(
        "float4 main() : SV_Target { return 1.0.xxxx; }", "ps_5_0");
    ASSERT_EQ(pixel_shader.result, S_OK) << pixel_shader.diagnostic_text();
    pixel_shader_ = std::move(pixel_shader.bytecode);

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.ShaderRegister = 0;
    parameter.Constants.Num32BitValues = 1;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    target_ =
        context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    depth_stencil_ =
        context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_D24_UNORM_S8_UINT,
                                 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                                 D3D12_RESOURCE_STATE_DEPTH_WRITE);
    rtv_heap_ =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    dsv_heap_ =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
    ASSERT_TRUE(target_);
    ASSERT_TRUE(depth_stencil_);
    ASSERT_TRUE(rtv_heap_);
    ASSERT_TRUE(dsv_heap_);
    rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    dsv_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target_.get(), nullptr, rtv_);
    context_.device()->CreateDepthStencilView(depth_stencil_.get(), nullptr,
                                              dsv_);
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(const D3D12_DEPTH_STENCIL_DESC &depth_stencil,
                 UINT8 write_mask = D3D12_COLOR_WRITE_ENABLE_ALL) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = {vertex_shader_->GetBufferPointer(),
               vertex_shader_->GetBufferSize()};
    desc.PS = {pixel_shader_->GetBufferPointer(),
               pixel_shader_->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = write_mask;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState = depth_stencil;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(pipeline.put())),
              S_OK);
    return pipeline;
  }

  void Clear(float depth, UINT8 stencil) {
    const FLOAT black[4] = {};
    context_.list()->ClearRenderTargetView(rtv_, black, 0, nullptr);
    context_.list()->ClearDepthStencilView(
        dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth, stencil,
        0, nullptr);
  }

  void Draw(ID3D12PipelineState *pipeline, float depth, UINT stencil_ref) {
    context_.list()->OMSetRenderTargets(1, &rtv_, FALSE, &dsv_);
    context_.list()->SetGraphicsRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetGraphicsRoot32BitConstant(0, std::bit_cast<UINT>(depth),
                                                  0);
    context_.list()->OMSetStencilRef(stencil_ref);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, kSize, kSize};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);
  }

  void ExpectTarget(bool white) {
    D3D12TestContext::Transition(context_.list(), target_.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(target_.get(), &readback), S_OK);
    const std::uint32_t expected = white ? 0xffffffff : 0;
    for (UINT y = 0; y < kSize; ++y) {
      for (UINT x = 0; x < kSize; ++x) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel,
                    readback.data.data() + y * readback.row_pitch +
                        x * sizeof(pixel),
                    sizeof(pixel));
        EXPECT_TRUE(ColorsMatch(pixel, expected, 0))
            << "pixel (" << x << ", " << y << ") was 0x" << std::hex << pixel;
      }
    }
  }

  static D3D12_DEPTH_STENCILOP_DESC
  StencilFace(D3D12_COMPARISON_FUNC function,
              D3D12_STENCIL_OP pass = D3D12_STENCIL_OP_KEEP) {
    D3D12_DEPTH_STENCILOP_DESC face = {};
    face.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    face.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    face.StencilPassOp = pass;
    face.StencilFunc = function;
    return face;
  }

  static constexpr UINT kSize = 8;
  D3D12TestContext context_;
  ComPtr<ID3DBlob> vertex_shader_;
  ComPtr<ID3DBlob> pixel_shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12Resource> target_;
  ComPtr<ID3D12Resource> depth_stencil_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv_ = {};
};

class DepthCompareSpec
    : public DepthStencilFixture,
      public ::testing::WithParamInterface<DepthCompareCase> {
public:
  static const char *
  Name(const ::testing::TestParamInfo<DepthCompareCase> &info) {
    return info.param.name;
  }
};

TEST_P(DepthCompareSpec, CompareMatrix) {
  const auto &test_case = GetParam();
  D3D12_DEPTH_STENCIL_DESC desc = {};
  desc.DepthEnable = TRUE;
  desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  desc.DepthFunc = test_case.function;
  auto pipeline = CreatePipeline(desc);
  ASSERT_TRUE(pipeline);

  Clear(test_case.stored, 0);
  Draw(pipeline.get(), test_case.incoming, 0);
  ExpectTarget(test_case.passes);
}

INSTANTIATE_TEST_SUITE_P(
    Functions, DepthCompareSpec,
    ::testing::Values(DepthCompareCase{D3D12_COMPARISON_FUNC_NEVER, 0.25f, 0.5f,
                                       false, "Never"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_LESS, 0.25f, 0.5f,
                                       true, "LessPass"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_LESS, 0.75f, 0.5f,
                                       false, "LessFail"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_EQUAL, 0.5f, 0.5f,
                                       true, "EqualPass"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_EQUAL, 0.25f, 0.5f,
                                       false, "EqualFail"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_LESS_EQUAL, 0.5f,
                                       0.5f, true, "LessEqualPass"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_LESS_EQUAL, 0.75f,
                                       0.5f, false, "LessEqualFail"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_GREATER, 0.75f,
                                       0.5f, true, "GreaterPass"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_GREATER, 0.25f,
                                       0.5f, false, "GreaterFail"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_NOT_EQUAL, 0.25f,
                                       0.5f, true, "NotEqualPass"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_NOT_EQUAL, 0.5f,
                                       0.5f, false, "NotEqualFail"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_GREATER_EQUAL,
                                       0.5f, 0.5f, true, "GreaterEqualPass"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_GREATER_EQUAL,
                                       0.25f, 0.5f, false, "GreaterEqualFail"},
                      DepthCompareCase{D3D12_COMPARISON_FUNC_ALWAYS, 0.75f,
                                       0.5f, true, "Always"}),
    DepthCompareSpec::Name);

class StencilCompareSpec
    : public DepthStencilFixture,
      public ::testing::WithParamInterface<StencilCompareCase> {
public:
  static const char *
  Name(const ::testing::TestParamInfo<StencilCompareCase> &info) {
    return info.param.name;
  }
};

TEST_P(StencilCompareSpec, CompareMatrix) {
  const auto &test_case = GetParam();
  D3D12_DEPTH_STENCIL_DESC desc = {};
  desc.DepthEnable = FALSE;
  desc.StencilEnable = TRUE;
  desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
  desc.StencilWriteMask = 0;
  desc.FrontFace = StencilFace(test_case.function);
  desc.BackFace = desc.FrontFace;
  auto pipeline = CreatePipeline(desc);
  ASSERT_TRUE(pipeline);

  Clear(1.0f, test_case.stored);
  Draw(pipeline.get(), 0.0f, test_case.reference);
  ExpectTarget(test_case.passes);
}

INSTANTIATE_TEST_SUITE_P(
    Functions, StencilCompareSpec,
    ::testing::Values(
        StencilCompareCase{D3D12_COMPARISON_FUNC_NEVER, 3, 3, false, "Never"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_LESS, 2, 3, true, "LessPass"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_LESS, 3, 2, false, "LessFail"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_EQUAL, 3, 3, true,
                           "EqualPass"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_EQUAL, 3, 2, false,
                           "EqualFail"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_LESS_EQUAL, 3, 3, true,
                           "LessEqualPass"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_LESS_EQUAL, 3, 2, false,
                           "LessEqualFail"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_GREATER, 3, 2, true,
                           "GreaterPass"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_GREATER, 2, 3, false,
                           "GreaterFail"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_NOT_EQUAL, 2, 3, true,
                           "NotEqualPass"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_NOT_EQUAL, 3, 3, false,
                           "NotEqualFail"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_GREATER_EQUAL, 3, 3, true,
                           "GreaterEqualPass"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_GREATER_EQUAL, 2, 3, false,
                           "GreaterEqualFail"},
        StencilCompareCase{D3D12_COMPARISON_FUNC_ALWAYS, 3, 2, true, "Always"}),
    StencilCompareSpec::Name);

class StencilOperationSpec
    : public DepthStencilFixture,
      public ::testing::WithParamInterface<StencilOperationCase> {
public:
  static const char *
  Name(const ::testing::TestParamInfo<StencilOperationCase> &info) {
    return info.param.name;
  }
};

TEST_P(StencilOperationSpec, OperationMatrix) {
  const auto &test_case = GetParam();
  D3D12_DEPTH_STENCIL_DESC operation_desc = {};
  operation_desc.DepthEnable = FALSE;
  operation_desc.StencilEnable = TRUE;
  operation_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
  operation_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
  operation_desc.FrontFace =
      StencilFace(D3D12_COMPARISON_FUNC_ALWAYS, test_case.operation);
  operation_desc.BackFace = operation_desc.FrontFace;
  auto operation_pipeline = CreatePipeline(operation_desc, 0);
  ASSERT_TRUE(operation_pipeline);

  D3D12_DEPTH_STENCIL_DESC validation_desc = operation_desc;
  validation_desc.StencilWriteMask = 0;
  validation_desc.FrontFace = StencilFace(D3D12_COMPARISON_FUNC_EQUAL);
  validation_desc.BackFace = validation_desc.FrontFace;
  auto validation_pipeline = CreatePipeline(validation_desc);
  ASSERT_TRUE(validation_pipeline);

  Clear(1.0f, test_case.initial);
  Draw(operation_pipeline.get(), 0.0f, test_case.reference);
  Draw(validation_pipeline.get(), 0.0f, test_case.expected);
  ExpectTarget(true);
}

INSTANTIATE_TEST_SUITE_P(
    Operations, StencilOperationSpec,
    ::testing::Values(
        StencilOperationCase{D3D12_STENCIL_OP_KEEP, 4, 3, 4, "Keep"},
        StencilOperationCase{D3D12_STENCIL_OP_ZERO, 4, 3, 0, "Zero"},
        StencilOperationCase{D3D12_STENCIL_OP_REPLACE, 4, 3, 3, "Replace"},
        StencilOperationCase{D3D12_STENCIL_OP_INCR_SAT, 255, 3, 255,
                             "IncrementSaturate"},
        StencilOperationCase{D3D12_STENCIL_OP_DECR_SAT, 0, 3, 0,
                             "DecrementSaturate"},
        StencilOperationCase{D3D12_STENCIL_OP_INVERT, 4, 3, 251, "Invert"},
        StencilOperationCase{D3D12_STENCIL_OP_INCR, 255, 3, 0, "IncrementWrap"},
        StencilOperationCase{D3D12_STENCIL_OP_DECR, 0, 3, 255,
                             "DecrementWrap"}),
    StencilOperationSpec::Name);

} // namespace
