#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr UINT kSize = 8;

class FormatMsaaExecutionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);

    auto vertex = CompileShader(R"(
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        const float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[vertex_id], 0.5, 1.0);
      }
    )",
                                "vs_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    vertex_ = std::move(vertex.bytecode);

    // Referencing SV_SampleIndex forces sample-frequency invocation. The red
    // channel is 16, 48, 80, ... in UNORM8 space, so an N-sample resolve must
    // produce exactly 16 * N (within normal conversion rounding).
    auto pixel = CompileShader(R"(
      float4 main(uint sample_index : SV_SampleIndex) : SV_Target {
        float red = (sample_index * 32.0 + 16.0) / 255.0;
        return float4(red, 64.0 / 255.0, 0.0, 1.0);
      }
    )",
                               "ps_5_0");
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    pixel_ = std::move(pixel.bytecode);
  }

  D3D12_FEATURE_DATA_FORMAT_SUPPORT Support() {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = kFormat;
    EXPECT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)),
              S_OK);
    return support;
  }

  ComPtr<ID3D12PipelineState> CreatePipeline(UINT sample_count) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.VS = {vertex_->GetBufferPointer(), vertex_->GetBufferSize()};
    desc.PS = {pixel_->GetBufferPointer(), pixel_->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = std::numeric_limits<UINT>::max();
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kFormat;
    desc.SampleDesc.Count = sample_count;

    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(pipeline.put())),
              S_OK);
    return pipeline;
  }

  ComPtr<ID3D12Resource> CreateTexture(UINT sample_count,
                                       D3D12_RESOURCE_FLAGS flags,
                                       D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kSize;
    desc.Height = kSize;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kFormat;
    desc.SampleDesc.Count = sample_count;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;

    ComPtr<ID3D12Resource> resource;
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(resource.put()));
    EXPECT_EQ(hr, S_OK);
    return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>{};
  }

  void Execute(UINT sample_count) {
    auto pipeline = CreatePipeline(sample_count);
    auto source =
        CreateTexture(sample_count, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                      D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto resolved = CreateTexture(1, D3D12_RESOURCE_FLAG_NONE,
                                  D3D12_RESOURCE_STATE_RESOLVE_DEST);
    auto heap =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(pipeline);
    ASSERT_TRUE(source);
    ASSERT_TRUE(resolved);
    ASSERT_TRUE(heap);

    D3D12_RENDER_TARGET_VIEW_DESC view = {};
    view.Format = kFormat;
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(source.get(), &view, rtv);
    constexpr FLOAT black[4] = {};
    context_.list()->ClearRenderTargetView(rtv, black, 0, nullptr);
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    context_.list()->SetGraphicsRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<FLOAT>(kSize), static_cast<FLOAT>(kSize),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(kSize),
                                static_cast<LONG>(kSize)};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);

    D3D12TestContext::Transition(context_.list(), source.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    context_.list()->ResolveSubresource(resolved.get(), 0, source.get(), 0,
                                        kFormat);
    D3D12TestContext::Transition(context_.list(), resolved.get(),
                                 D3D12_RESOURCE_STATE_RESOLVE_DEST,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);

    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(resolved.get(), &readback), S_OK);
    ASSERT_EQ(readback.width, kSize);
    ASSERT_EQ(readback.height, kSize);
    const UINT expected_red = 16 * sample_count;
    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        std::array<std::uint8_t, 4> pixel = {};
        std::memcpy(pixel.data(),
                    readback.data.data() + y * readback.row_pitch + x * 4,
                    pixel.size());
        EXPECT_NEAR(pixel[0], expected_red, 1)
            << sample_count << "x red at (" << x << ", " << y << ")";
        EXPECT_NEAR(pixel[1], 64, 1)
            << sample_count << "x green at (" << x << ", " << y << ")";
        EXPECT_EQ(pixel[2], 0u)
            << sample_count << "x blue at (" << x << ", " << y << ")";
        EXPECT_EQ(pixel[3], 255u)
            << sample_count << "x alpha at (" << x << ", " << y << ")";
      }
    }
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3DBlob> vertex_;
  ComPtr<ID3DBlob> pixel_;
};

TEST_F(FormatMsaaExecutionSpec,
       EveryAdvertisedTwoFourAndEightSampleModeUsesPerSampleOutput) {
  constexpr D3D12_FORMAT_SUPPORT1 required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;
  const auto support = Support();
  if ((support.Support1 & required) != required)
    GTEST_SKIP() << "RGBA8 MSAA render/resolve is not advertised";

  UINT executed = 0;
  UINT skipped = 0;
  for (const UINT sample_count : {2u, 4u, 8u}) {
    SCOPED_TRACE(::testing::Message() << "sample_count=" << sample_count);
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    quality.Format = kFormat;
    quality.SampleCount = sample_count;
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                  sizeof(quality)),
              S_OK);
    if (!quality.NumQualityLevels) {
      ++skipped;
      continue;
    }

    ASSERT_NO_FATAL_FAILURE(Execute(sample_count));
    ++executed;
  }

  RecordProperty("sample_counts_executed", static_cast<int>(executed));
  RecordProperty("sample_counts_skipped", static_cast<int>(skipped));
  EXPECT_EQ(executed + skipped, 3u);
  EXPECT_GT(executed, 0u);
}

} // namespace
