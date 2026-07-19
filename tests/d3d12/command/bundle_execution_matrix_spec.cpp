#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §9.7: bundle execute repeatedly / from multiple lists / indexed draw.
// Public D3D12 API only.

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct BundleExecCase {
  UINT execute_count;
  bool second_list;
};

std::vector<BundleExecCase> BuildBundleExecCases() {
  return {
      {1, false}, {2, false}, {3, false}, {4, false}, {8, false},
      {1, true},  {2, true},  {3, true},
  };
}

class BundleExecutionMatrixSpec
    : public ::testing::TestWithParam<BundleExecCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto vs = CompileShader(
        "float4 main(uint id : SV_VertexID) : SV_Position {"
        "  float2 p = float2((id == 1) ? 3.0 : -1.0, (id == 2) ? 3.0 : -1.0);"
        "  return float4(p, 0.0, 1.0);"
        "}",
        "vs_5_0");
    const auto ps = CompileShader(
        "float4 main() : SV_Target { return float4(0, 1, 0, 1); }", "ps_5_0");
    ASSERT_EQ(vs.result, S_OK) << vs.diagnostic_text();
    ASSERT_EQ(ps.result, S_OK) << ps.diagnostic_text();

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = root_signature_.get();
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
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_P(BundleExecutionMatrixSpec, BundleDrawExecutesAsRequested) {
  const auto &test = GetParam();
  constexpr UINT kWidth = 8;
  constexpr UINT kHeight = 8;
  auto target = context_.CreateTexture2D(
      kWidth, kHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  ComPtr<ID3D12CommandAllocator> bundle_alloc;
  ComPtr<ID3D12GraphicsCommandList> bundle;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(bundle_alloc.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_BUNDLE, bundle_alloc.get(),
                pipeline_.get(), IID_PPV_ARGS(bundle.put())),
            S_OK);
  bundle->SetGraphicsRootSignature(root_signature_.get());
  bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  bundle->DrawInstanced(3, 1, 0, 0);
  ASSERT_EQ(bundle->Close(), S_OK);

  constexpr float clear[4] = {0, 0, 0, 1};
  context_.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  D3D12_VIEWPORT vp = {0, 0, float(kWidth), float(kHeight), 0, 1};
  D3D12_RECT sc = {0, 0, LONG(kWidth), LONG(kHeight)};
  context_.list()->RSSetViewports(1, &vp);
  context_.list()->RSSetScissorRects(1, &sc);
  for (UINT i = 0; i < test.execute_count; ++i)
    context_.list()->ExecuteBundle(bundle.get());

  if (test.second_list) {
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    context_.list()->RSSetViewports(1, &vp);
    context_.list()->RSSetScissorRects(1, &sc);
    context_.list()->ExecuteBundle(bundle.get());
  }

  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  // Fullscreen green triangle covering the RT after at least one execute.
  UINT green_pixels = 0;
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      UINT pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      if (ColorsMatch(pixel, 0xff00ff00, 1))
        ++green_pixels;
    }
  }
  EXPECT_GT(green_pixels, 0u) << "exec=" << test.execute_count
                              << " second=" << test.second_list;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string BundleExecName(
    const ::testing::TestParamInfo<BundleExecCase> &info) {
  return "N" + std::to_string(info.param.execute_count) +
         (info.param.second_list ? "TwoLists" : "OneList");
}

INSTANTIATE_TEST_SUITE_P(ExecMatrix, BundleExecutionMatrixSpec,
                         ::testing::ValuesIn(BuildBundleExecCases()),
                         BundleExecName);

} // namespace
