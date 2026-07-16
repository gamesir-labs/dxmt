#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

enum class GraphicsPipelineCase {
  VertexAndPixel,
  VertexOnlyNoAttachments,
  AttachmentlessZeroSampleCount,
  HullWithoutDomain,
  DomainWithoutHull,
  ComputeBytecodeAsVertex,
  PixelWithoutVertex,
  TooManyRenderTargets,
  NonUnknownInactiveRenderTarget,
  UndefinedTopology,
  ZeroSampleCountWithAttachment,
  UnsupportedSampleCount,
  NonzeroSampleQuality,
  MultiNodeMask,
  NullInputLayoutArray,
  BlendAndLogicOpTogether,
  IndependentLogicOp,
  NullCachedBlob,
};

class GraphicsPipelineCreationSpec
    : public ::testing::TestWithParam<GraphicsPipelineCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    auto vertex = CompileShader(R"(
      float4 main(uint id : SV_VertexID) : SV_Position {
        const float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[id], 0.0, 1.0);
      }
    )", "vs_5_0");
    auto pixel = CompileShader(
        "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
        "ps_5_0");
    auto compute = CompileShader(
        "[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    ASSERT_EQ(compute.result, S_OK) << compute.diagnostic_text();
    vertex_ = std::move(vertex.bytecode);
    pixel_ = std::move(pixel.bytecode);
    compute_ = std::move(compute.bytecode);
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);
  }

  D3D12_SHADER_BYTECODE Bytecode(ID3DBlob *blob) const {
    return {blob->GetBufferPointer(), blob->GetBufferSize()};
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC BaseDesc() const {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = Bytecode(vertex_.get());
    desc.PS = Bytecode(pixel_.get());
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.SampleMask = UINT_MAX;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    return desc;
  }

  D3D12TestContext context_;
  ComPtr<ID3DBlob> vertex_;
  ComPtr<ID3DBlob> pixel_;
  ComPtr<ID3DBlob> compute_;
  ComPtr<ID3D12RootSignature> root_signature_;
};

TEST_P(GraphicsPipelineCreationSpec, AcceptsOrRejectsContractMatrix) {
  auto desc = BaseDesc();
  HRESULT expected = E_INVALIDARG;
  switch (GetParam()) {
  case GraphicsPipelineCase::VertexAndPixel:
    expected = S_OK;
    break;
  case GraphicsPipelineCase::VertexOnlyNoAttachments:
    desc.PS = {};
    desc.NumRenderTargets = 0;
    desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    expected = S_OK;
    break;
  case GraphicsPipelineCase::AttachmentlessZeroSampleCount:
    desc.PS = {};
    desc.NumRenderTargets = 0;
    desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 0;
    expected = S_OK;
    break;
  case GraphicsPipelineCase::HullWithoutDomain:
    desc.HS = Bytecode(vertex_.get());
    break;
  case GraphicsPipelineCase::DomainWithoutHull:
    desc.DS = Bytecode(vertex_.get());
    break;
  case GraphicsPipelineCase::ComputeBytecodeAsVertex:
    desc.VS = Bytecode(compute_.get());
    break;
  case GraphicsPipelineCase::PixelWithoutVertex:
    desc.VS = {};
    break;
  case GraphicsPipelineCase::TooManyRenderTargets:
    desc.NumRenderTargets = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1;
    break;
  case GraphicsPipelineCase::NonUnknownInactiveRenderTarget:
    desc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
    break;
  case GraphicsPipelineCase::UndefinedTopology:
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    break;
  case GraphicsPipelineCase::ZeroSampleCountWithAttachment:
    desc.SampleDesc.Count = 0;
    break;
  case GraphicsPipelineCase::UnsupportedSampleCount:
    desc.SampleDesc.Count = 3;
    break;
  case GraphicsPipelineCase::NonzeroSampleQuality:
    desc.SampleDesc.Quality = 1;
    break;
  case GraphicsPipelineCase::MultiNodeMask:
    desc.NodeMask = 2;
    break;
  case GraphicsPipelineCase::NullInputLayoutArray:
    desc.InputLayout.NumElements = 1;
    break;
  case GraphicsPipelineCase::BlendAndLogicOpTogether:
    desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    desc.BlendState.RenderTarget[0].LogicOpEnable = TRUE;
    break;
  case GraphicsPipelineCase::IndependentLogicOp:
    desc.BlendState.IndependentBlendEnable = TRUE;
    desc.BlendState.RenderTarget[0].LogicOpEnable = TRUE;
    break;
  case GraphicsPipelineCase::NullCachedBlob:
    desc.CachedPSO.CachedBlobSizeInBytes = 16;
    break;
  }

  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  const HRESULT actual = context_.device()->CreateGraphicsPipelineState(
      &desc, __uuidof(ID3D12PipelineState), &output);
  EXPECT_EQ(actual, expected);
  if (SUCCEEDED(expected)) {
    ASSERT_NE(output, nullptr);
    static_cast<ID3D12PipelineState *>(output)->Release();
  } else {
    EXPECT_EQ(output, nullptr);
  }
}

std::string GraphicsPipelineCaseName(
    const ::testing::TestParamInfo<GraphicsPipelineCase> &info) {
  switch (info.param) {
  case GraphicsPipelineCase::VertexAndPixel:
    return "VertexAndPixel";
  case GraphicsPipelineCase::VertexOnlyNoAttachments:
    return "VertexOnlyNoAttachments";
  case GraphicsPipelineCase::AttachmentlessZeroSampleCount:
    return "AttachmentlessZeroSampleCount";
  case GraphicsPipelineCase::HullWithoutDomain:
    return "HullWithoutDomain";
  case GraphicsPipelineCase::DomainWithoutHull:
    return "DomainWithoutHull";
  case GraphicsPipelineCase::ComputeBytecodeAsVertex:
    return "ComputeBytecodeAsVertex";
  case GraphicsPipelineCase::PixelWithoutVertex:
    return "PixelWithoutVertex";
  case GraphicsPipelineCase::TooManyRenderTargets:
    return "TooManyRenderTargets";
  case GraphicsPipelineCase::NonUnknownInactiveRenderTarget:
    return "NonUnknownInactiveRenderTarget";
  case GraphicsPipelineCase::UndefinedTopology:
    return "UndefinedTopology";
  case GraphicsPipelineCase::ZeroSampleCountWithAttachment:
    return "ZeroSampleCountWithAttachment";
  case GraphicsPipelineCase::UnsupportedSampleCount:
    return "UnsupportedSampleCount";
  case GraphicsPipelineCase::NonzeroSampleQuality:
    return "NonzeroSampleQuality";
  case GraphicsPipelineCase::MultiNodeMask:
    return "MultiNodeMask";
  case GraphicsPipelineCase::NullInputLayoutArray:
    return "NullInputLayoutArray";
  case GraphicsPipelineCase::BlendAndLogicOpTogether:
    return "BlendAndLogicOpTogether";
  case GraphicsPipelineCase::IndependentLogicOp:
    return "IndependentLogicOp";
  case GraphicsPipelineCase::NullCachedBlob:
    return "NullCachedBlob";
  }
  return "Unknown";
}

INSTANTIATE_TEST_SUITE_P(
    ContractMatrix, GraphicsPipelineCreationSpec,
    ::testing::Values(
        GraphicsPipelineCase::VertexAndPixel,
        GraphicsPipelineCase::VertexOnlyNoAttachments,
        GraphicsPipelineCase::AttachmentlessZeroSampleCount,
        GraphicsPipelineCase::HullWithoutDomain,
        GraphicsPipelineCase::DomainWithoutHull,
        GraphicsPipelineCase::ComputeBytecodeAsVertex,
        GraphicsPipelineCase::PixelWithoutVertex,
        GraphicsPipelineCase::TooManyRenderTargets,
        GraphicsPipelineCase::NonUnknownInactiveRenderTarget,
        GraphicsPipelineCase::UndefinedTopology,
        GraphicsPipelineCase::ZeroSampleCountWithAttachment,
        GraphicsPipelineCase::UnsupportedSampleCount,
        GraphicsPipelineCase::NonzeroSampleQuality,
        GraphicsPipelineCase::MultiNodeMask,
        GraphicsPipelineCase::NullInputLayoutArray,
        GraphicsPipelineCase::BlendAndLogicOpTogether,
        GraphicsPipelineCase::IndependentLogicOp,
        GraphicsPipelineCase::NullCachedBlob),
    GraphicsPipelineCaseName);

} // namespace
