#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

// Plan §15.8: ResolveSubresource MSAA sample-count matrix.
// Public D3D12 API only.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct ResolveMsaaCase {
  UINT sample_count;
  UINT width;
  UINT height;
};

std::vector<ResolveMsaaCase> BuildResolveMsaaCases() {
  std::vector<ResolveMsaaCase> cases;
  for (UINT samples : {2u, 4u, 8u}) {
    for (UINT size : {1u, 2u, 4u, 7u, 8u, 16u, 32u})
      cases.push_back({samples, size, size});
    cases.push_back({samples, 8, 4});
    cases.push_back({samples, 4, 8});
  }
  return cases;
}

class ResolveMsaaMatrixSpec
    : public ::testing::TestWithParam<ResolveMsaaCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    root_ = context_.CreateRootSignature(D3D12_ROOT_SIGNATURE_DESC{});
    ASSERT_TRUE(root_);
    auto vs = CompileShader(R"(
      float4 main(uint id : SV_VertexID) : SV_Position {
        const float2 p[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
        return float4(p[id], 0.5, 1.0);
      }
    )",
                            "vs_5_0");
    auto ps = CompileShader(
        "float4 main() : SV_Target { return float4(0, 1, 0, 1); }", "ps_5_0");
    ASSERT_EQ(vs.result, S_OK) << vs.diagnostic_text();
    ASSERT_EQ(ps.result, S_OK) << ps.diagnostic_text();
    vs_ = std::move(vs.bytecode);
    ps_ = std::move(ps.bytecode);
  }

  bool SupportsSamples(UINT sample_count) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = {};
    levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    levels.SampleCount = sample_count;
    if (FAILED(context_.device()->CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))))
      return false;
    return levels.NumQualityLevels > 0;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3DBlob> vs_;
  ComPtr<ID3DBlob> ps_;
};

TEST_P(ResolveMsaaMatrixSpec, DrawThenResolveProducesGreen) {
  const auto &test = GetParam();
  if (!SupportsSamples(test.sample_count))
    GTEST_SKIP() << "MSAA " << test.sample_count << " unsupported";

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
  pso.pRootSignature = root_.get();
  pso.VS = {vs_->GetBufferPointer(), vs_->GetBufferSize()};
  pso.PS = {ps_->GetBufferPointer(), ps_->GetBufferSize()};
  pso.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  pso.SampleMask = std::numeric_limits<UINT>::max();
  pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets = 1;
  pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso.SampleDesc.Count = test.sample_count;
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                &pso, IID_PPV_ARGS(pipeline.put())),
            S_OK);

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC msaa_desc = {};
  msaa_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  msaa_desc.Width = test.width;
  msaa_desc.Height = test.height;
  msaa_desc.DepthOrArraySize = 1;
  msaa_desc.MipLevels = 1;
  msaa_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  msaa_desc.SampleDesc.Count = test.sample_count;
  msaa_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  ComPtr<ID3D12Resource> msaa;
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &msaa_desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                IID_PPV_ARGS(msaa.put())),
            S_OK);
  auto resolved = context_.CreateTexture2D(
      test.width, test.height, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
  ASSERT_TRUE(resolved);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(msaa.get(), nullptr, rtv);

  constexpr float clear[4] = {0, 0, 0, 1};
  context_.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  D3D12_VIEWPORT vp = {0, 0, float(test.width), float(test.height), 0, 1};
  D3D12_RECT sc = {0, 0, LONG(test.width), LONG(test.height)};
  context_.list()->RSSetViewports(1, &vp);
  context_.list()->RSSetScissorRects(1, &sc);
  context_.list()->SetGraphicsRootSignature(root_.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(context_.list(), msaa.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  context_.list()->ResolveSubresource(resolved.get(), 0, msaa.get(), 0,
                                      DXGI_FORMAT_R8G8B8A8_UNORM);
  D3D12TestContext::Transition(context_.list(), resolved.get(),
                               D3D12_RESOURCE_STATE_RESOLVE_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(resolved.get(), &readback), S_OK);
  UINT greenish = 0;
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      // Green dominant after resolve of solid green draw.
      const UINT8 r = pixel & 0xff;
      const UINT8 g = (pixel >> 8) & 0xff;
      const UINT8 b = (pixel >> 16) & 0xff;
      if (g > r && g > b && g > 32)
        ++greenish;
    }
  }
  EXPECT_GT(greenish, 0u) << "samples=" << test.sample_count << " "
                          << test.width << "x" << test.height;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ResolveName(const ::testing::TestParamInfo<ResolveMsaaCase> &info) {
  return "S" + std::to_string(info.param.sample_count) + "W" +
         std::to_string(info.param.width) + "H" +
         std::to_string(info.param.height);
}

INSTANTIATE_TEST_SUITE_P(SampleMatrix, ResolveMsaaMatrixSpec,
                         ::testing::ValuesIn(BuildResolveMsaaCases()),
                         ResolveName);

} // namespace
