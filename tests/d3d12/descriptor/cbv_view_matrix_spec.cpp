#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §16.2: CreateConstantBufferView offset and size matrix.
// Public D3D12 API only — table-bound CBV read through a pixel shader.

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;

struct CbvViewCase {
  UINT64 buffer_size;
  UINT64 view_offset;
  UINT view_size;
};

std::vector<CbvViewCase> BuildCbvViewCases() {
  // CBV SizeInBytes must be a multiple of 256. Offsets are also 256-aligned.
  std::vector<CbvViewCase> cases;
  constexpr UINT sizes[] = {256, 512, 1024};
  for (const UINT size : sizes) {
    cases.push_back({1024, 0, size});
    if (size < 1024)
      cases.push_back({1024, 256, size});
    if (size <= 512)
      cases.push_back({1024, 512, size});
  }
  // Last valid 256-byte CBV window in a 1024-byte buffer.
  cases.push_back({1024, 768, 256});
  // Larger backing store with mid-window views.
  cases.push_back({2048, 0, 256});
  cases.push_back({2048, 1024, 256});
  cases.push_back({2048, 1024, 512});
  cases.push_back({2048, 1536, 512});
  return cases;
}

class CbvViewMatrixSpec : public ::testing::TestWithParam<CbvViewCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);

    auto ps = CompileShader(R"(
      cbuffer Color : register(b0) { float4 value; };
      float4 main() : SV_Target { return value; }
    )",
                            "ps_5_0");
    ASSERT_EQ(ps.result, S_OK) << ps.diagnostic_text();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = root_.get();
    pso.VS = FullscreenVertexShader();
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
  static constexpr UINT kSize = 8;
  // Distinct unorm color: (1,0,1,1) → 0xffff00ff.
  static constexpr std::array<float, 4> kColor = {1.0f, 0.0f, 1.0f, 1.0f};
  static constexpr std::uint32_t kExpected = 0xffff00ffu;
};

TEST_P(CbvViewMatrixSpec, TableBoundViewSamplesColorAtOffsetAndSize) {
  const auto &test = GetParam();
  ASSERT_EQ(test.view_size % 256u, 0u);
  ASSERT_EQ(test.view_offset % 256ull, 0ull);
  ASSERT_LE(test.view_offset + test.view_size, test.buffer_size);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(test.buffer_size),
                                  0xcd);
  // Place the cbuffer payload at the start of the viewed range only.
  std::memcpy(bytes.data() + static_cast<std::size_t>(test.view_offset),
              kColor.data(), sizeof(kColor));
  // Poison just before the view (when possible) so an offset error is visible.
  if (test.view_offset >= 4) {
    const float poison[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    std::memcpy(bytes.data() + static_cast<std::size_t>(test.view_offset - 16),
                poison, sizeof(poison));
  }

  auto constants =
      context_.CreateUploadBuffer(test.buffer_size, bytes.data(), bytes.size());
  ASSERT_TRUE(constants);

  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(heap);

  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
  cbv.BufferLocation = constants->GetGPUVirtualAddress() + test.view_offset;
  cbv.SizeInBytes = test.view_size;
  context_.device()->CreateConstantBufferView(
      &cbv, heap->GetCPUDescriptorHandleForHeapStart());

  auto target = context_.CreateTexture2D(
      kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
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
  D3D12_VIEWPORT vp = {0, 0, float(kSize), float(kSize), 0, 1};
  D3D12_RECT sc = {0, 0, LONG(kSize), LONG(kSize)};
  context_.list()->RSSetViewports(1, &vp);
  context_.list()->RSSetScissorRects(1, &sc);
  context_.list()->SetGraphicsRootSignature(root_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetGraphicsRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->DrawInstanced(3, 1, 0, 0);

  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);

  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, kExpected, 1))
          << "x=" << x << " y=" << y << " actual=0x" << std::hex << pixel
          << std::dec << " offset=" << test.view_offset
          << " size=" << test.view_size << " buf=" << test.buffer_size;
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string CbvViewName(const ::testing::TestParamInfo<CbvViewCase> &info) {
  return "Buf" + std::to_string(info.param.buffer_size) + "Off" +
         std::to_string(info.param.view_offset) + "Sz" +
         std::to_string(info.param.view_size);
}

INSTANTIATE_TEST_SUITE_P(OffsetSizeMatrix, CbvViewMatrixSpec,
                         ::testing::ValuesIn(BuildCbvViewCases()), CbvViewName);

} // namespace
