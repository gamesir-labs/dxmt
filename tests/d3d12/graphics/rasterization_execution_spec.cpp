#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

constexpr UINT kSize = 12;
constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

ComPtr<ID3D12Resource>
CreateTexture(D3D12TestContext &context, DXGI_FORMAT format, UINT sample_count,
              D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = kSize;
  desc.Height = kSize;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = sample_count;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;

  ComPtr<ID3D12Resource> resource;
  const HRESULT hr = context.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
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

class RasterizationExecutionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    auto white = CompileShader(
        "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
        "ps_5_0");
    ASSERT_EQ(white.result, S_OK) << white.diagnostic_text();
    white_pixel_shader_ = std::move(white.bytecode);

    auto winding = CompileShader(R"(
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        float2 positions[6] = {
          float2(-0.95, -0.8), float2(-0.5, 0.8), float2(-0.05, -0.8),
          float2( 0.05, -0.8), float2( 0.95, -0.8), float2( 0.5, 0.8)
        };
        return float4(positions[vertex_id], 0.5, 1.0);
      })",
                                 "vs_5_0");
    ASSERT_EQ(winding.result, S_OK) << winding.diagnostic_text();
    winding_vertex_shader_ = std::move(winding.bytecode);

    auto fullscreen = CompileShader(R"(
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[vertex_id], 0.5, 1.0);
      })",
                                    "vs_5_0");
    ASSERT_EQ(fullscreen.result, S_OK) << fullscreen.diagnostic_text();
    fullscreen_vertex_shader_ = std::move(fullscreen.bytecode);

    auto outside_depth = CompileShader(R"(
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[vertex_id], 2.0, 1.0);
      })",
                                       "vs_5_0");
    ASSERT_EQ(outside_depth.result, S_OK) << outside_depth.diagnostic_text();
    outside_depth_vertex_shader_ = std::move(outside_depth.bytecode);
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(ID3DBlob *vertex_shader, ID3DBlob *pixel_shader,
                 D3D12_CULL_MODE cull_mode = D3D12_CULL_MODE_NONE,
                 bool front_counter_clockwise = false,
                 bool depth_clip = true, INT depth_bias = 0,
                 bool depth_enabled = false, UINT sample_count = 1,
                 UINT sample_mask = std::numeric_limits<UINT>::max()) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
    desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = sample_mask;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = cull_mode;
    desc.RasterizerState.FrontCounterClockwise = front_counter_clockwise;
    desc.RasterizerState.DepthBias = depth_bias;
    desc.RasterizerState.DepthClipEnable = depth_clip;
    desc.DepthStencilState.DepthEnable = depth_enabled;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
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

  ComPtr<ID3D12Resource> CreateColorTarget(UINT sample_count = 1) {
    return CreateTexture(context_, kColorFormat, sample_count,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
  }

  void Draw(ID3D12PipelineState *pipeline,
            D3D12_CPU_DESCRIPTOR_HANDLE rtv, UINT vertex_count,
            const D3D12_CPU_DESCRIPTOR_HANDLE *dsv = nullptr) {
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, dsv);
    context_.list()->SetGraphicsRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, float(kSize), float(kSize), 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, LONG(kSize), LONG(kSize)};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(vertex_count, 1, 0, 0);
  }

  TextureReadback ReadTarget(ID3D12Resource *target,
                             D3D12_RESOURCE_STATES state =
                                 D3D12_RESOURCE_STATE_RENDER_TARGET) {
    D3D12TestContext::Transition(context_.list(), target, state,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    EXPECT_EQ(context_.ReadbackTexture(target, &readback), S_OK);
    return readback;
  }

  static bool IsCovered(const TextureReadback &readback, UINT x, UINT y) {
    return ColorsMatch(PixelAt(readback, x, y), 0xffffffffu, 1);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3DBlob> white_pixel_shader_;
  ComPtr<ID3DBlob> winding_vertex_shader_;
  ComPtr<ID3DBlob> fullscreen_vertex_shader_;
  ComPtr<ID3DBlob> outside_depth_vertex_shader_;
};

TEST_F(RasterizationExecutionSpec,
       CullAndFrontFaceModesPartitionOppositeWindingTriangles) {
  struct CullCase {
    D3D12_CULL_MODE cull;
    bool front_ccw;
  };
  constexpr std::array cases = {
      CullCase{D3D12_CULL_MODE_NONE, false},
      CullCase{D3D12_CULL_MODE_BACK, false},
      CullCase{D3D12_CULL_MODE_FRONT, false},
      CullCase{D3D12_CULL_MODE_BACK, true},
      CullCase{D3D12_CULL_MODE_FRONT, true},
  };

  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                             UINT(cases.size()), false);
  ASSERT_TRUE(heap);
  std::array<ComPtr<ID3D12Resource>, cases.size()> targets;
  std::array<ComPtr<ID3D12PipelineState>, cases.size()> pipelines;
  constexpr FLOAT kBlack[4] = {};
  for (UINT index = 0; index < cases.size(); ++index) {
    targets[index] = CreateColorTarget();
    pipelines[index] = CreatePipeline(
        winding_vertex_shader_.get(), white_pixel_shader_.get(),
        cases[index].cull, cases[index].front_ccw);
    ASSERT_TRUE(targets[index]);
    ASSERT_TRUE(pipelines[index]);
    const auto rtv = context_.CpuDescriptorHandle(heap.get(), index);
    context_.device()->CreateRenderTargetView(targets[index].get(), nullptr,
                                              rtv);
    context_.list()->ClearRenderTargetView(rtv, kBlack, 0, nullptr);
    Draw(pipelines[index].get(), rtv, 6);
    D3D12TestContext::Transition(context_.list(), targets[index].get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
  }

  std::array<TextureReadback, cases.size()> readbacks;
  for (UINT index = 0; index < readbacks.size(); ++index) {
    ASSERT_EQ(context_.ReadbackTexture(targets[index].get(), &readbacks[index]),
              S_OK);
    if (index + 1 < readbacks.size()) {
      ASSERT_EQ(context_.ResetCommandList(), S_OK);
    }
  }

  std::array<UINT, cases.size()> covered_counts = {};
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::array<bool, cases.size()> covered = {};
      for (UINT index = 0; index < covered.size(); ++index) {
        covered[index] = IsCovered(readbacks[index], x, y);
        covered_counts[index] += covered[index];
      }
      EXPECT_EQ(covered[0], covered[1] || covered[2])
          << "front/back partition at (" << x << ", " << y << ")";
      EXPECT_FALSE(covered[1] && covered[2])
          << "front/back overlap at (" << x << ", " << y << ")";
      EXPECT_EQ(covered[1], covered[4])
          << "front-CCW inversion at (" << x << ", " << y << ")";
      EXPECT_EQ(covered[2], covered[3])
          << "back-CCW inversion at (" << x << ", " << y << ")";
    }
  }
  EXPECT_GT(covered_counts[1], 0u);
  EXPECT_GT(covered_counts[2], 0u);
  EXPECT_EQ(covered_counts[0], covered_counts[1] + covered_counts[2]);
}

TEST_F(RasterizationExecutionSpec, DepthClipEnableRejectsFarPlaneTriangle) {
  auto clipped_target = CreateColorTarget();
  auto unclipped_target = CreateColorTarget();
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2,
                                             false);
  auto clipped_pipeline = CreatePipeline(outside_depth_vertex_shader_.get(),
                                         white_pixel_shader_.get(),
                                         D3D12_CULL_MODE_NONE, false, true);
  auto unclipped_pipeline = CreatePipeline(outside_depth_vertex_shader_.get(),
                                           white_pixel_shader_.get(),
                                           D3D12_CULL_MODE_NONE, false, false);
  ASSERT_TRUE(clipped_target);
  ASSERT_TRUE(unclipped_target);
  ASSERT_TRUE(heap);
  ASSERT_TRUE(clipped_pipeline);
  ASSERT_TRUE(unclipped_pipeline);
  constexpr FLOAT kBlack[4] = {};
  const auto clipped_rtv = context_.CpuDescriptorHandle(heap.get(), 0);
  const auto unclipped_rtv = context_.CpuDescriptorHandle(heap.get(), 1);
  context_.device()->CreateRenderTargetView(clipped_target.get(), nullptr,
                                            clipped_rtv);
  context_.device()->CreateRenderTargetView(unclipped_target.get(), nullptr,
                                            unclipped_rtv);
  context_.list()->ClearRenderTargetView(clipped_rtv, kBlack, 0, nullptr);
  Draw(clipped_pipeline.get(), clipped_rtv, 3);
  context_.list()->ClearRenderTargetView(unclipped_rtv, kBlack, 0, nullptr);
  Draw(unclipped_pipeline.get(), unclipped_rtv, 3);
  D3D12TestContext::Transition(context_.list(), clipped_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(context_.list(), unclipped_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback clipped;
  ASSERT_EQ(context_.ReadbackTexture(clipped_target.get(), &clipped), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  TextureReadback unclipped;
  ASSERT_EQ(context_.ReadbackTexture(unclipped_target.get(), &unclipped), S_OK);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      EXPECT_TRUE(ColorsMatch(PixelAt(clipped, x, y), 0u, 0))
          << "clipped pixel (" << x << ", " << y << ")";
      EXPECT_TRUE(ColorsMatch(PixelAt(unclipped, x, y), 0xffffffffu, 1))
          << "unclipped pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(RasterizationExecutionSpec, NegativeDepthBiasMovesEqualDepthDrawCloser) {
  auto unbiased_target = CreateColorTarget();
  auto biased_target = CreateColorTarget();
  auto unbiased_depth = CreateTexture(
      context_, DXGI_FORMAT_D32_FLOAT, 1,
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
      D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto biased_depth = CreateTexture(
      context_, DXGI_FORMAT_D32_FLOAT, 1,
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
      D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
  auto dsv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
  auto unbiased_pipeline = CreatePipeline(
      fullscreen_vertex_shader_.get(), white_pixel_shader_.get(),
      D3D12_CULL_MODE_NONE, false, true, 0, true);
  auto biased_pipeline = CreatePipeline(
      fullscreen_vertex_shader_.get(), white_pixel_shader_.get(),
      D3D12_CULL_MODE_NONE, false, true, -1024, true);
  ASSERT_TRUE(unbiased_target);
  ASSERT_TRUE(biased_target);
  ASSERT_TRUE(unbiased_depth);
  ASSERT_TRUE(biased_depth);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(dsv_heap);
  ASSERT_TRUE(unbiased_pipeline);
  ASSERT_TRUE(biased_pipeline);

  constexpr FLOAT kBlack[4] = {};
  const auto unbiased_rtv = context_.CpuDescriptorHandle(rtv_heap.get(), 0);
  const auto biased_rtv = context_.CpuDescriptorHandle(rtv_heap.get(), 1);
  const auto unbiased_dsv = context_.CpuDescriptorHandle(dsv_heap.get(), 0);
  const auto biased_dsv = context_.CpuDescriptorHandle(dsv_heap.get(), 1);
  context_.device()->CreateRenderTargetView(unbiased_target.get(), nullptr,
                                            unbiased_rtv);
  context_.device()->CreateRenderTargetView(biased_target.get(), nullptr,
                                            biased_rtv);
  context_.device()->CreateDepthStencilView(unbiased_depth.get(), nullptr,
                                            unbiased_dsv);
  context_.device()->CreateDepthStencilView(biased_depth.get(), nullptr,
                                            biased_dsv);
  context_.list()->ClearRenderTargetView(unbiased_rtv, kBlack, 0, nullptr);
  context_.list()->ClearDepthStencilView(unbiased_dsv, D3D12_CLEAR_FLAG_DEPTH,
                                         0.5f, 0, 0, nullptr);
  Draw(unbiased_pipeline.get(), unbiased_rtv, 3, &unbiased_dsv);
  context_.list()->ClearRenderTargetView(biased_rtv, kBlack, 0, nullptr);
  context_.list()->ClearDepthStencilView(biased_dsv, D3D12_CLEAR_FLAG_DEPTH,
                                         0.5f, 0, 0, nullptr);
  Draw(biased_pipeline.get(), biased_rtv, 3, &biased_dsv);
  D3D12TestContext::Transition(context_.list(), unbiased_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(context_.list(), biased_target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback unbiased;
  ASSERT_EQ(context_.ReadbackTexture(unbiased_target.get(), &unbiased), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  TextureReadback biased;
  ASSERT_EQ(context_.ReadbackTexture(biased_target.get(), &biased), S_OK);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      EXPECT_TRUE(ColorsMatch(PixelAt(unbiased, x, y), 0u, 0));
      EXPECT_TRUE(ColorsMatch(PixelAt(biased, x, y), 0xffffffffu, 1));
    }
  }
}

struct SampleMaskCase {
  UINT mask;
  std::uint32_t expected;
  const char *name;
};

class MultisampleExecutionSpec
    : public RasterizationExecutionSpec,
      public ::testing::WithParamInterface<SampleMaskCase> {};

TEST_P(MultisampleExecutionSpec, SampleMaskSelectsSampleIndexOutputs) {
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
  quality.Format = kColorFormat;
  quality.SampleCount = 4;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                sizeof(quality)),
            S_OK);
  if (!quality.NumQualityLevels)
    GTEST_SKIP() << "4x MSAA is unavailable";

  auto sample_index = CompileShader(R"(
    float4 main(uint sample_index : SV_SampleIndex) : SV_Target {
      return float4((sample_index + 1) * 0.25, 0.0, 0.0, 1.0);
    })",
                                    "ps_5_0");
  ASSERT_EQ(sample_index.result, S_OK) << sample_index.diagnostic_text();
  auto pipeline = CreatePipeline(
      fullscreen_vertex_shader_.get(), sample_index.bytecode.get(),
      D3D12_CULL_MODE_NONE, false, true, 0, false, 4, GetParam().mask);
  auto source = CreateColorTarget(4);
  auto destination = CreateTexture(
      context_, kColorFormat, 1, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_RESOLVE_DEST);
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                             false);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(source.get(), nullptr, rtv);
  constexpr FLOAT kBlack[4] = {};
  context_.list()->ClearRenderTargetView(rtv, kBlack, 0, nullptr);
  Draw(pipeline.get(), rtv, 3);
  D3D12TestContext::Transition(context_.list(), source.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  context_.list()->ResolveSubresource(destination.get(), 0, source.get(), 0,
                                      kColorFormat);
  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_RESOLVE_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(destination.get(), &readback), S_OK);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      EXPECT_TRUE(ColorsMatch(PixelAt(readback, x, y), GetParam().expected, 2))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    Masks, MultisampleExecutionSpec,
    ::testing::Values(
        SampleMaskCase{0x1u, 0x40000010u, "Sample0"},
        SampleMaskCase{0x5u, 0x80000040u, "EvenSamples"},
        SampleMaskCase{0xau, 0x80000060u, "OddSamples"},
        SampleMaskCase{0xfu, 0xff00009fu, "AllSamples"}),
    [](const ::testing::TestParamInfo<SampleMaskCase> &info) {
      return info.param.name;
    });

} // namespace
