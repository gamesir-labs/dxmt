#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// Public D3D12 RSSetViewports size/position matrix with RTV oracle.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;

struct ViewportCase {
  FLOAT top_left_x;
  FLOAT top_left_y;
  FLOAT width;
  FLOAT height;
};

std::vector<ViewportCase> BuildViewportCases() {
  std::vector<ViewportCase> cases;
  const FLOAT positions[] = {0.f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 48.f};
  const FLOAT sizes[] = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f};
  for (const FLOAT x : positions) {
    for (const FLOAT y : positions) {
      for (const FLOAT w : sizes) {
        for (const FLOAT h : sizes) {
          if (x + w > 64.f || y + h > 64.f)
            continue;
          cases.push_back({x, y, w, h});
        }
      }
    }
  }
  return cases;
}

class ViewportMatrixSpec : public ::testing::TestWithParam<ViewportCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto pixel = CompileShader(
        "float4 main() : SV_Target { return float4(1,0,0,1); }", "ps_5_0");
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

TEST_P(ViewportMatrixSpec, DrawsOnlyInsideConfiguredViewport) {
  const auto &test = GetParam();
  const FLOAT clear[4] = {0, 0, 0, 1};
  context_.list()->OMSetRenderTargets(1, &rtv_, FALSE, nullptr);
  context_.list()->ClearRenderTargetView(rtv_, clear, 0, nullptr);
  context_.list()->SetGraphicsRootSignature(root_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {test.top_left_x, test.top_left_y,
                                   test.width, test.height, 0.f, 1.f};
  const D3D12_RECT scissor = {0, 0, 64, 64};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), target_.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target_.get(), &readback), S_OK);

  const int x0 = static_cast<int>(std::floor(test.top_left_x));
  const int y0 = static_cast<int>(std::floor(test.top_left_y));
  const int x1 = static_cast<int>(std::ceil(test.top_left_x + test.width));
  const int y1 = static_cast<int>(std::ceil(test.top_left_y + test.height));
  // Sample a few interior and exterior pixels rather than the whole 64x64.
  const int samples[][2] = {
      {x0, y0},
      {(x0 + x1) / 2, (y0 + y1) / 2},
      {x1 - 1, y1 - 1},
      {0, 0},
      {63, 63},
      {x0 > 0 ? x0 - 1 : 63, y0},
      {x0, y0 > 0 ? y0 - 1 : 63},
  };
  for (const auto &sample : samples) {
    const int x = sample[0];
    const int y = sample[1];
    if (x < 0 || y < 0 || x >= 64 || y >= 64)
      continue;
    std::uint32_t pixel = 0;
    std::memcpy(&pixel,
                readback.data.data() +
                    static_cast<size_t>(y) * readback.row_pitch +
                    static_cast<size_t>(x) * 4,
                sizeof(pixel));
    const bool inside = x >= x0 && y >= y0 && x < x1 && y < y1 && x1 > x0 &&
                        y1 > y0;
    if (inside) {
      EXPECT_NE(pixel & 0x00ffffffu, 0u) << "inside (" << x << "," << y << ")";
    } else if (x == 0 && y == 0 && (x0 > 0 || y0 > 0)) {
      EXPECT_EQ(pixel & 0x00ffffffu, 0u) << "outside origin";
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ViewportName(const ::testing::TestParamInfo<ViewportCase> &info) {
  return "X" + std::to_string(static_cast<int>(info.param.top_left_x)) + "Y" +
         std::to_string(static_cast<int>(info.param.top_left_y)) + "W" +
         std::to_string(static_cast<int>(info.param.width)) + "H" +
         std::to_string(static_cast<int>(info.param.height));
}

INSTANTIATE_TEST_SUITE_P(ViewportMatrix, ViewportMatrixSpec,
                         ::testing::ValuesIn(BuildViewportCases()),
                         ViewportName);

} // namespace
