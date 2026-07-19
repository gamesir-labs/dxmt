#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <cstring>
#include <string>
#include <vector>

// Public D3D12 RSSetScissorRects matrix.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;

struct ScissorCase {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
};

std::vector<ScissorCase> BuildScissorCases() {
  return {
      {0, 0, 64, 64},   {0, 0, 1, 1},     {1, 1, 2, 2},
      {0, 0, 8, 64},    {0, 0, 64, 8},    {8, 8, 56, 56},
      {16, 24, 48, 40}, {31, 31, 32, 32}, {32, 32, 33, 33},
      {33, 33, 64, 64}, {0, 0, 63, 63},   {1, 2, 63, 62},
      {2, 4, 40, 48},   {4, 8, 24, 32},   {24, 16, 56, 48},
      {40, 48, 64, 64},
  };
}

class ScissorMatrixSpec : public ::testing::TestWithParam<ScissorCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto pixel = CompileShader(
        "float4 main() : SV_Target { return float4(0,1,0,1); }", "ps_5_0");
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.VS = FullscreenVertexShader();
    desc.PS = {pixel.bytecode->GetBufferPointer(),
               pixel.bytecode->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
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
    target_ = context_.CreateTexture2D(
        64, 64, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv_heap_ =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(target_);
    ASSERT_TRUE(rtv_heap_);
    rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target_.get(), nullptr, rtv_);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12Resource> target_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
};

TEST_P(ScissorMatrixSpec, RestrictsDrawCoverageToScissorRect) {
  const auto &test = GetParam();
  const FLOAT clear[4] = {0, 0, 0, 1};
  context_.list()->OMSetRenderTargets(1, &rtv_, FALSE, nullptr);
  context_.list()->ClearRenderTargetView(rtv_, clear, 0, nullptr);
  context_.list()->SetGraphicsRootSignature(root_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, 64, 64, 0, 1};
  const D3D12_RECT scissor = {test.left, test.top, test.right, test.bottom};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), target_.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target_.get(), &readback), S_OK);

  const int cx = (test.left + test.right) / 2;
  const int cy = (test.top + test.bottom) / 2;
  auto read = [&](int x, int y) {
    std::uint32_t pixel = 0;
    std::memcpy(&pixel,
                readback.data.data() +
                    static_cast<size_t>(y) * readback.row_pitch +
                    static_cast<size_t>(x) * 4,
                sizeof(pixel));
    return pixel & 0x00ffffffu;
  };
  if (cx >= 0 && cy >= 0 && cx < 64 && cy < 64) {
    EXPECT_NE(read(cx, cy), 0u) << "center inside scissor";
  }
  if (test.left > 0) {
    EXPECT_EQ(read(test.left - 1, cy), 0u) << "left outside";
  }
  if (test.top > 0) {
    EXPECT_EQ(read(cx, test.top - 1), 0u) << "top outside";
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ScissorName(const ::testing::TestParamInfo<ScissorCase> &info) {
  return "L" + std::to_string(info.param.left) + "T" +
         std::to_string(info.param.top) + "R" +
         std::to_string(info.param.right) + "B" +
         std::to_string(info.param.bottom);
}

INSTANTIATE_TEST_SUITE_P(ScissorMatrix, ScissorMatrixSpec,
                         ::testing::ValuesIn(BuildScissorCases()),
                         ScissorName);

} // namespace
