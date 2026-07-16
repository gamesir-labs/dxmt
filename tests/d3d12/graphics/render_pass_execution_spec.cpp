#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::FullscreenVertexShader;
using dxmt::test::TextureReadback;

constexpr UINT kWidth = 8;
constexpr UINT kHeight = 6;
constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

ComPtr<ID3D12Resource>
CreateTexture(D3D12TestContext &context, UINT width, UINT height,
              DXGI_FORMAT format, UINT sample_count,
              D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
              const D3D12_CLEAR_VALUE *clear_value = nullptr) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = sample_count;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;

  ComPtr<ID3D12Resource> resource;
  const HRESULT hr = context.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, state, clear_value,
      IID_PPV_ARGS(resource.put()));
  return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>{};
}

std::uint32_t PixelAt(const TextureReadback &readback, UINT x, UINT y) {
  std::uint32_t pixel = 0;
  std::memcpy(&pixel,
              readback.data.data() + y * readback.row_pitch +
                  x * sizeof(pixel),
              sizeof(pixel));
  return pixel;
}

class RenderPassExecutionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.list()->QueryInterface(IID_PPV_ARGS(list4_.put())), S_OK);

    const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    auto white = CompileShader(
        "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
        "ps_5_0");
    ASSERT_EQ(white.result, S_OK) << white.diagnostic_text();
    white_pixel_shader_ = std::move(white.bytecode);

    auto yellow = CompileShader(
        "float4 main() : SV_Target { return float4(1, 1, 0, 1); }",
        "ps_5_0");
    ASSERT_EQ(yellow.result, S_OK) << yellow.diagnostic_text();
    yellow_pixel_shader_ = std::move(yellow.bytecode);

    auto depth_vertex = CompileShader(R"(
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[vertex_id], 0.5, 1.0);
      })",
                                      "vs_5_0");
    ASSERT_EQ(depth_vertex.result, S_OK) << depth_vertex.diagnostic_text();
    depth_vertex_shader_ = std::move(depth_vertex.bytecode);
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(ID3DBlob *pixel_shader, UINT sample_count = 1,
                 bool depth_enabled = false) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = depth_enabled
                  ? D3D12_SHADER_BYTECODE{depth_vertex_shader_->GetBufferPointer(),
                                          depth_vertex_shader_->GetBufferSize()}
                  : FullscreenVertexShader();
    desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = std::numeric_limits<UINT>::max();
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = depth_enabled;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kColorFormat;
    desc.DSVFormat = depth_enabled ? DXGI_FORMAT_D32_FLOAT
                                   : DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = sample_count;

    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(pipeline.put())),
              S_OK);
    return pipeline;
  }

  void SetDrawState(ID3D12PipelineState *pipeline,
                    const D3D12_RECT &scissor = {0, 0, LONG(kWidth),
                                                  LONG(kHeight)}) {
    context_.list()->SetGraphicsRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, float(kWidth), float(kHeight), 0.0f, 1.0f};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
  }

  ComPtr<ID3D12Resource> CreateColorTarget(UINT sample_count = 1,
                                           D3D12_RESOURCE_STATES state =
                                               D3D12_RESOURCE_STATE_RENDER_TARGET) {
    return CreateTexture(context_, kWidth, kHeight, kColorFormat, sample_count,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, state);
  }

  D3D12_RENDER_PASS_RENDER_TARGET_DESC
  ColorPass(D3D12_CPU_DESCRIPTOR_HANDLE rtv,
            D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE beginning,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE ending,
            const std::array<FLOAT, 4> &clear = {}) {
    D3D12_RENDER_PASS_RENDER_TARGET_DESC desc = {};
    desc.cpuDescriptor = rtv;
    desc.BeginningAccess.Type = beginning;
    if (beginning == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR) {
      desc.BeginningAccess.Clear.ClearValue.Format = kColorFormat;
      std::copy(clear.begin(), clear.end(),
                desc.BeginningAccess.Clear.ClearValue.Color);
    }
    desc.EndingAccess.Type = ending;
    return desc;
  }

  void ExpectSolid(ID3D12Resource *target, std::uint32_t expected,
                   unsigned tolerance = 1) {
    D3D12TestContext::Transition(context_.list(), target,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(target, &readback), S_OK);
    for (UINT y = 0; y < kHeight; ++y) {
      for (UINT x = 0; x < kWidth; ++x) {
        EXPECT_TRUE(ColorsMatch(PixelAt(readback, x, y), expected, tolerance))
            << "pixel (" << x << ", " << y << ")";
      }
    }
  }

  D3D12TestContext context_;
  ComPtr<ID3D12GraphicsCommandList4> list4_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3DBlob> white_pixel_shader_;
  ComPtr<ID3DBlob> yellow_pixel_shader_;
  ComPtr<ID3DBlob> depth_vertex_shader_;
};

TEST_F(RenderPassExecutionSpec, BeginningClearWritesEveryColorPixel) {
  auto target = CreateColorTarget();
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                             false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  const std::array<FLOAT, 4> teal = {0.25f, 0.5f, 0.75f, 1.0f};
  const auto pass = ColorPass(
      rtv, D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR,
      D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE, teal);
  list4_->BeginRenderPass(1, &pass, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4_->EndRenderPass();

  ExpectSolid(target.get(), 0xffbf8040u, 2);
}

TEST_F(RenderPassExecutionSpec, PreserveKeepsPrePassColorContents) {
  auto target = CreateColorTarget();
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                             false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  constexpr FLOAT kBlue[4] = {0.0f, 0.25f, 1.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, kBlue, 0, nullptr);
  const auto pass = ColorPass(
      rtv, D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE,
      D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE);
  list4_->BeginRenderPass(1, &pass, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4_->EndRenderPass();

  ExpectSolid(target.get(), 0xffff4000u, 2);
}

TEST_F(RenderPassExecutionSpec, DiscardThenDrawProducesDefinedReplacement) {
  auto target = CreateColorTarget();
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                             false);
  auto pipeline = CreatePipeline(white_pixel_shader_.get());
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  ASSERT_TRUE(pipeline);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  constexpr FLOAT kOldColor[4] = {0.75f, 0.0f, 0.25f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, kOldColor, 0, nullptr);
  const auto pass = ColorPass(
      rtv, D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD,
      D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE);
  list4_->BeginRenderPass(1, &pass, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  SetDrawState(pipeline.get());
  context_.list()->DrawInstanced(3, 1, 0, 0);
  list4_->EndRenderPass();

  ExpectSolid(target.get(), 0xffffffffu);
}

TEST_F(RenderPassExecutionSpec, DrawSequenceMatchesFlattenedCommands) {
  auto pass_target = CreateColorTarget();
  auto flat_target = CreateColorTarget();
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2,
                                             false);
  auto pipeline = CreatePipeline(yellow_pixel_shader_.get());
  ASSERT_TRUE(pass_target);
  ASSERT_TRUE(flat_target);
  ASSERT_TRUE(heap);
  ASSERT_TRUE(pipeline);
  const auto pass_rtv = context_.CpuDescriptorHandle(heap.get(), 0);
  const auto flat_rtv = context_.CpuDescriptorHandle(heap.get(), 1);
  context_.device()->CreateRenderTargetView(pass_target.get(), nullptr,
                                            pass_rtv);
  context_.device()->CreateRenderTargetView(flat_target.get(), nullptr,
                                            flat_rtv);

  const std::array<FLOAT, 4> background = {0.0f, 0.25f, 0.5f, 1.0f};
  const D3D12_RECT left_half = {0, 0, LONG(kWidth / 2), LONG(kHeight)};

  const auto pass = ColorPass(
      pass_rtv, D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR,
      D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE, background);
  list4_->BeginRenderPass(1, &pass, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  SetDrawState(pipeline.get(), left_half);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  list4_->EndRenderPass();

  context_.list()->ClearRenderTargetView(flat_rtv, background.data(), 0,
                                         nullptr);
  context_.list()->OMSetRenderTargets(1, &flat_rtv, FALSE, nullptr);
  SetDrawState(pipeline.get(), left_half);
  context_.list()->DrawInstanced(3, 1, 0, 0);

  D3D12TestContext::Transition(context_.list(), pass_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(context_.list(), flat_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback pass_readback;
  ASSERT_EQ(context_.ReadbackTexture(pass_target.get(), &pass_readback), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  TextureReadback flat_readback;
  ASSERT_EQ(context_.ReadbackTexture(flat_target.get(), &flat_readback), S_OK);

  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      const auto actual = PixelAt(pass_readback, x, y);
      const auto expected = PixelAt(flat_readback, x, y);
      EXPECT_TRUE(ColorsMatch(actual, expected, 0))
          << "flattened parity mismatch at (" << x << ", " << y << ")";
    }
  }
}

struct DepthAttachmentCase {
  float clear_depth;
  bool draw_passes;
  const char *name;
};

class RenderPassDepthAttachmentSpec
    : public RenderPassExecutionSpec,
      public ::testing::WithParamInterface<DepthAttachmentCase> {};

TEST_P(RenderPassDepthAttachmentSpec, ClearDepthControlsDrawInsidePass) {
  auto target = CreateColorTarget();
  auto depth = CreateTexture(
      context_, kWidth, kHeight, DXGI_FORMAT_D32_FLOAT, 1,
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
      D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto dsv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  auto pipeline = CreatePipeline(white_pixel_shader_.get(), 1, true);
  ASSERT_TRUE(target);
  ASSERT_TRUE(depth);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(dsv_heap);
  ASSERT_TRUE(pipeline);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  const auto dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  context_.device()->CreateDepthStencilView(depth.get(), nullptr, dsv);

  const std::array<FLOAT, 4> black = {};
  const auto color_pass = ColorPass(
      rtv, D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR,
      D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE, black);
  D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depth_pass = {};
  depth_pass.cpuDescriptor = dsv;
  depth_pass.DepthBeginningAccess.Type =
      D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
  depth_pass.DepthBeginningAccess.Clear.ClearValue.Format =
      DXGI_FORMAT_D32_FLOAT;
  depth_pass.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth =
      GetParam().clear_depth;
  depth_pass.DepthEndingAccess.Type =
      D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
  depth_pass.StencilBeginningAccess.Type =
      D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
  depth_pass.StencilEndingAccess.Type =
      D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;

  list4_->BeginRenderPass(1, &color_pass, &depth_pass,
                          D3D12_RENDER_PASS_FLAG_NONE);
  SetDrawState(pipeline.get());
  context_.list()->DrawInstanced(3, 1, 0, 0);
  list4_->EndRenderPass();

  ExpectSolid(target.get(), GetParam().draw_passes ? 0xffffffffu : 0u);
}

INSTANTIATE_TEST_SUITE_P(
    ClearValues, RenderPassDepthAttachmentSpec,
    ::testing::Values(DepthAttachmentCase{0.75f, true, "DrawPasses"},
                      DepthAttachmentCase{0.25f, false, "DrawFails"}),
    [](const ::testing::TestParamInfo<DepthAttachmentCase> &info) {
      return info.param.name;
    });

TEST_F(RenderPassExecutionSpec, EndingResolvePublishesMultisampleColor) {
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
  quality.Format = kColorFormat;
  quality.SampleCount = 4;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                sizeof(quality)),
            S_OK);
  if (!quality.NumQualityLevels)
    GTEST_SKIP() << "4x MSAA is unavailable";

  auto source = CreateColorTarget(4);
  auto destination = CreateTexture(
      context_, kWidth, kHeight, kColorFormat, 1, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_RESOLVE_DEST);
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                             false);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(source.get(), nullptr, rtv);

  const std::array<FLOAT, 4> orange = {1.0f, 0.5f, 0.125f, 1.0f};
  auto pass = ColorPass(rtv, D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR,
                        D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE, orange);
  D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS subresource =
      {};
  subresource.SrcRect = {0, 0, LONG(kWidth), LONG(kHeight)};
  pass.EndingAccess.Resolve.pSrcResource = source.get();
  pass.EndingAccess.Resolve.pDstResource = destination.get();
  pass.EndingAccess.Resolve.SubresourceCount = 1;
  pass.EndingAccess.Resolve.pSubresourceParameters = &subresource;
  pass.EndingAccess.Resolve.Format = kColorFormat;
  pass.EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;
  pass.EndingAccess.Resolve.PreserveResolveSource = FALSE;

  list4_->BeginRenderPass(1, &pass, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4_->EndRenderPass();
  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_RESOLVE_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(destination.get(), &readback), S_OK);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      EXPECT_TRUE(ColorsMatch(PixelAt(readback, x, y), 0xff2080ffu, 2))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

} // namespace
