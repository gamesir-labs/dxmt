#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan graphics: DrawInstanced vertex/instance count matrix.
// Public D3D12 API only.

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct DrawCase {
  UINT vertex_count;
  UINT instance_count;
  UINT start_vertex;
  UINT start_instance;
};

std::vector<DrawCase> BuildDrawCases() {
  std::vector<DrawCase> cases;
  // Fullscreen triangle uses 3 vertices from SV_VertexID 0..2.
  // Matrix covers vertex_count and instance_count combinations; non-3 vertex
  // counts still must not remove the device (coverage may be empty).
  const UINT vertices[] = {0, 1, 2, 3, 4, 6, 9, 12, 24, 30, 33, 48, 64, 96,
                           128, 192, 256, 384, 512};
  const UINT instances[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 64};
  for (const UINT vertex_count : vertices)
    cases.push_back({vertex_count, 1, 0, 0});
  for (const UINT instance_count : instances)
    cases.push_back({3, instance_count, 0, 0});
  for (const UINT vertex_count : {3u, 33u, 512u}) {
    cases.push_back({vertex_count, 1, 0, 1});
    cases.push_back({vertex_count, 1, 3, 0});
  }
  return cases;
}

class DrawInstancedMatrixSpec : public ::testing::TestWithParam<DrawCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    root_ = context_.CreateRootSignature(D3D12_ROOT_SIGNATURE_DESC{});
    ASSERT_TRUE(root_);
    auto vs = CompileShader(R"(
      float4 main(uint id : SV_VertexID) : SV_Position {
        // Fullscreen oversized triangle for ids 0,1,2; other ids offscreen.
        float2 p = float2(-2.0, -2.0);
        if (id == 0) p = float2(-1.0, -1.0);
        else if (id == 1) p = float2(-1.0, 3.0);
        else if (id == 2) p = float2(3.0, -1.0);
        return float4(p, 0.0, 1.0);
      }
    )",
                            "vs_5_0");
    auto ps = CompileShader(
        "float4 main() : SV_Target { return float4(0, 1, 0, 1); }", "ps_5_0");
    ASSERT_EQ(vs.result, S_OK) << vs.diagnostic_text();
    ASSERT_EQ(ps.result, S_OK) << ps.diagnostic_text();
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = root_.get();
    pso.VS = {vs.bytecode->GetBufferPointer(), vs.bytecode->GetBufferSize()};
    pso.PS = {ps.bytecode->GetBufferPointer(), ps.bytecode->GetBufferSize()};
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &pso, IID_PPV_ARGS(pipeline_.put())),
              S_OK);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_P(DrawInstancedMatrixSpec, DrawDoesNotRemoveDeviceAndCoversWhenPossible) {
  const auto &test = GetParam();
  constexpr UINT kW = 8, kH = 8;
  auto target = context_.CreateTexture2D(
      kW, kH, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  constexpr float clear[4] = {0, 0, 0, 1};
  context_.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  D3D12_VIEWPORT vp = {0, 0, float(kW), float(kH), 0, 1};
  D3D12_RECT sc = {0, 0, LONG(kW), LONG(kH)};
  context_.list()->RSSetViewports(1, &vp);
  context_.list()->RSSetScissorRects(1, &sc);
  context_.list()->SetGraphicsRootSignature(root_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->DrawInstanced(test.vertex_count, test.instance_count,
                                 test.start_vertex, test.start_instance);
  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);

  UINT green = 0;
  for (UINT y = 0; y < kH; ++y) {
    for (UINT x = 0; x < kW; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      if (ColorsMatch(pixel, 0xff00ff00, 1))
        ++green;
    }
  }
  // When the draw includes the fullscreen triangle verts (ids 0..2 relative
  // to start_vertex) and at least one instance, coverage should be non-empty.
  const bool covers = test.instance_count > 0 && test.vertex_count >= 3 &&
                      test.start_vertex == 0;
  if (covers) {
    EXPECT_GT(green, 0u) << "v=" << test.vertex_count
                         << " i=" << test.instance_count;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string DrawName(const ::testing::TestParamInfo<DrawCase> &info) {
  return "V" + std::to_string(info.param.vertex_count) + "I" +
         std::to_string(info.param.instance_count) + "SV" +
         std::to_string(info.param.start_vertex) + "SI" +
         std::to_string(info.param.start_instance) + "N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(CountMatrix, DrawInstancedMatrixSpec,
                         ::testing::ValuesIn(BuildDrawCases()), DrawName);

} // namespace
