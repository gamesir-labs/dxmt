#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

enum class CoverageShape { Full, LeftHalf, RightHalf, Empty };

struct ViewportScissorCase {
  D3D12_VIEWPORT viewport;
  D3D12_RECT scissor;
  CoverageShape expected;
  const char *name;
};

class GraphicsViewportScissorSpec
    : public ::testing::TestWithParam<ViewportScissorCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const D3D12_ROOT_SIGNATURE_DESC desc = {};
    root_signature_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_signature_);
    const auto pixel = CompileShader(
        "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
        "ps_5_0");
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    const D3D12_SHADER_BYTECODE bytecode = {pixel.bytecode->GetBufferPointer(),
                                            pixel.bytecode->GetBufferSize()};
    pipeline_ = context_.CreateGraphicsPipeline(
        root_signature_.get(), DXGI_FORMAT_R8G8B8A8_UNORM, bytecode);
    ASSERT_TRUE(pipeline_);
  }

  D3D12TestContext context_;
  dxmt::test::ComPtr<ID3D12RootSignature> root_signature_;
  dxmt::test::ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_P(GraphicsViewportScissorSpec, ClipsRasterizationToIntersection) {
  constexpr UINT kWidth = 8;
  constexpr UINT kHeight = 8;
  auto target = context_.CreateTexture2D(
      kWidth, kHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                            false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  constexpr FLOAT clear[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_signature_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const auto &test = GetParam();
  context_.list()->RSSetViewports(1, &test.viewport);
  context_.list()->RSSetScissorRects(1, &test.scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      bool covered = false;
      switch (test.expected) {
      case CoverageShape::Full:
        covered = true;
        break;
      case CoverageShape::LeftHalf:
        covered = x < kWidth / 2;
        break;
      case CoverageShape::RightHalf:
        covered = x >= kWidth / 2;
        break;
      case CoverageShape::Empty:
        break;
      }
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      const std::uint32_t expected = covered ? 0xffffffffu : 0u;
      EXPECT_TRUE(ColorsMatch(actual, expected, 1))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

std::string ViewportScissorCaseName(
    const ::testing::TestParamInfo<ViewportScissorCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    CoverageMatrix, GraphicsViewportScissorSpec,
    ::testing::Values(
        ViewportScissorCase{{0, 0, 8, 8, 0, 1}, {0, 0, 8, 8},
                            CoverageShape::Full, "Full"},
        ViewportScissorCase{{0, 0, 4, 8, 0, 1}, {0, 0, 8, 8},
                            CoverageShape::LeftHalf, "LeftViewport"},
        ViewportScissorCase{{0, 0, 8, 8, 0, 1}, {4, 0, 8, 8},
                            CoverageShape::RightHalf, "RightScissor"},
        ViewportScissorCase{{0, 0, 8, 8, 0, 1}, {4, 0, 4, 8},
                            CoverageShape::Empty, "EmptyScissor"},
        ViewportScissorCase{{0, 0, 8, 8, 0, 1}, {-4, 0, 4, 8},
                            CoverageShape::LeftHalf,
                            "PartiallyOutsideScissor"},
        ViewportScissorCase{{-4, 0, 8, 8, 0, 1}, {0, 0, 8, 8},
                            CoverageShape::LeftHalf,
                            "NegativeViewportOrigin"}),
    ViewportScissorCaseName);

} // namespace
