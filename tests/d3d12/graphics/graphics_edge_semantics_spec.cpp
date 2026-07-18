#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

constexpr UINT kSize = 32;
constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

std::uint32_t PixelAt(const TextureReadback &readback, UINT x, UINT y) {
  std::uint32_t pixel = 0;
  std::memcpy(&pixel,
              readback.data.data() + y * readback.row_pitch +
                  x * sizeof(pixel),
              sizeof(pixel));
  return pixel;
}

UINT CoveredPixelCount(const TextureReadback &readback) {
  UINT count = 0;
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x)
      count += (PixelAt(readback, x, y) >> 24) != 0;
  }
  return count;
}

class GraphicsEdgeSemanticsSpec : public ::testing::Test {
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
  }

  ComPtr<ID3D12Resource>
  CreateTexture(DXGI_FORMAT format, UINT sample_count,
                D3D12_RESOURCE_FLAGS flags,
                D3D12_RESOURCE_STATES initial_state) {
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
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, initial_state, nullptr,
        IID_PPV_ARGS(resource.put()));
    return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>{};
  }

  ComPtr<ID3D12Resource> CreateColorTarget(UINT sample_count = 1) {
    return CreateTexture(kColorFormat, sample_count,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(ID3DBlob *vertex_shader, ID3DBlob *pixel_shader,
                 D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type =
                     D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                 UINT sample_count = 1, bool depth_enabled = false,
                 float slope_scaled_depth_bias = 0.0f,
                 float depth_bias_clamp = 0.0f,
                 bool alpha_to_coverage = false) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
    desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    desc.BlendState.AlphaToCoverageEnable = alpha_to_coverage;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = std::numeric_limits<UINT>::max();
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.RasterizerState.MultisampleEnable = sample_count > 1;
    desc.RasterizerState.SlopeScaledDepthBias = slope_scaled_depth_bias;
    desc.RasterizerState.DepthBiasClamp = depth_bias_clamp;
    desc.DepthStencilState.DepthEnable = depth_enabled;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.PrimitiveTopologyType = topology_type;
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
                    D3D_PRIMITIVE_TOPOLOGY topology) {
    context_.list()->SetGraphicsRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->IASetPrimitiveTopology(topology);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, float(kSize), float(kSize), 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, LONG(kSize), LONG(kSize)};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
  }

  TextureReadback DrawAndRead(ID3D12PipelineState *pipeline,
                              D3D12_PRIMITIVE_TOPOLOGY topology,
                              UINT vertex_count) {
    auto target = CreateColorTarget();
    auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                               1, false);
    EXPECT_TRUE(target);
    EXPECT_TRUE(heap);
    if (!target || !heap)
      return {};
    const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
    constexpr FLOAT kBlack[4] = {};
    context_.list()->ClearRenderTargetView(rtv, kBlack, 0, nullptr);
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    SetDrawState(pipeline, topology);
    context_.list()->DrawInstanced(vertex_count, 1, 0, 0);
    D3D12TestContext::Transition(context_.list(), target.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    EXPECT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
    return readback;
  }

  bool Supports4xMsaa() {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    quality.Format = kColorFormat;
    quality.SampleCount = 4;
    return SUCCEEDED(context_.device()->CheckFeatureSupport(
               D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
               sizeof(quality))) &&
           quality.NumQualityLevels != 0;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3DBlob> white_pixel_shader_;
};

TEST_F(GraphicsEdgeSemanticsSpec,
       PerspectiveAffineAndFlatInterpolationHaveDistinctSemantics) {
  auto vertex = CompileShader(R"(
    struct Output {
      float4 position : SV_Position;
      float perspective_value : TEXCOORD0;
      noperspective float affine_value : TEXCOORD1;
      nointerpolation float flat_value : TEXCOORD2;
    };
    Output main(uint id : SV_VertexID) {
      const float4 positions[3] = {
        float4(-0.8, -0.8, 0.25, 1.0),
        float4( 0.0,  0.8, 0.25, 1.0),
        float4( 0.4, -0.4, 0.125, 0.5)
      };
      const float values[3] = {0.0, 0.0, 1.0};
      Output output;
      output.position = positions[id];
      output.perspective_value = values[id];
      output.affine_value = values[id];
      output.flat_value = 0.75;
      return output;
    })",
                              "vs_5_0");
  auto pixel = CompileShader(R"(
    float4 main(float perspective_value : TEXCOORD0,
                noperspective float affine_value : TEXCOORD1,
                nointerpolation float flat_value : TEXCOORD2) : SV_Target {
      return float4(perspective_value, affine_value, flat_value, 1.0);
    })",
                             "ps_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto pipeline = CreatePipeline(vertex.bytecode.get(), pixel.bytecode.get());
  ASSERT_TRUE(pipeline);
  const auto readback = DrawAndRead(
      pipeline.get(), D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 3);
  ASSERT_FALSE(readback.data.empty());

  UINT covered = 0;
  UINT interpolation_differences = 0;
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      const std::uint32_t value = PixelAt(readback, x, y);
      if (!(value >> 24))
        continue;
      ++covered;
      const int perspective = value & 0xff;
      const int affine = (value >> 8) & 0xff;
      const int flat = (value >> 16) & 0xff;
      EXPECT_NEAR(flat, 191, 2) << "pixel (" << x << ", " << y << ")";
      interpolation_differences += std::abs(perspective - affine) >= 8;
    }
  }
  EXPECT_GT(covered, 100u);
  EXPECT_GT(interpolation_differences, 20u);
}

struct ClipCase {
  std::array<float, 3> depths;
  bool expects_fragments;
  const char *name;
};

class GraphicsClipEdgeSpec
    : public GraphicsEdgeSemanticsSpec,
      public ::testing::WithParamInterface<ClipCase> {};

TEST_P(GraphicsClipEdgeSpec, NearAndFarPlaneClippingProducesExpectedCoverage) {
  const auto &test_case = GetParam();
  std::ostringstream source;
  source << R"(
    float4 main(uint id : SV_VertexID) : SV_Position {
      const float2 positions[3] = {
        float2(-0.9, -0.8), float2(0.0, 0.9), float2(0.9, -0.8)
      };
      const float depths[3] = {)"
         << test_case.depths[0] << ", " << test_case.depths[1] << ", "
         << test_case.depths[2] << R"(};
      return float4(positions[id], depths[id], 1.0);
    })";
  auto vertex = CompileShader(source.str(), "vs_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  auto pipeline =
      CreatePipeline(vertex.bytecode.get(), white_pixel_shader_.get());
  ASSERT_TRUE(pipeline);
  const auto readback = DrawAndRead(
      pipeline.get(), D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 3);
  const UINT covered = CoveredPixelCount(readback);
  if (test_case.expects_fragments) {
    EXPECT_GT(covered, 0u);
    EXPECT_LT(covered, kSize * kSize);
  } else {
    EXPECT_EQ(covered, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Planes, GraphicsClipEdgeSpec,
    ::testing::Values(
        ClipCase{{-0.25f, -0.25f, -0.25f}, false, "BehindNearPlane"},
        ClipCase{{1.25f, 1.25f, 1.25f}, false, "BeyondFarPlane"},
        ClipCase{{-0.5f, 0.5f, 0.5f}, true, "PartialNearClip"},
        ClipCase{{1.5f, 0.5f, 0.5f}, true, "PartialFarClip"}),
    [](const ::testing::TestParamInfo<ClipCase> &info) {
      return info.param.name;
    });

struct PrimitiveCase {
  D3D12_PRIMITIVE_TOPOLOGY_TYPE pipeline_topology;
  D3D12_PRIMITIVE_TOPOLOGY draw_topology;
  const char *positions;
  UINT vertex_count;
  bool expects_fragments;
  const char *name;
};

class PrimitiveRasterEdgeSpec
    : public GraphicsEdgeSemanticsSpec,
      public ::testing::WithParamInterface<PrimitiveCase> {};

TEST_P(PrimitiveRasterEdgeSpec, PrimitiveRasterizationHasStableCoverage) {
  const auto &test_case = GetParam();
  const std::string source = std::string(R"(
    float4 main(uint id : SV_VertexID) : SV_Position {
      const float2 positions[3] = {)") +
                             test_case.positions + R"(};
      return float4(positions[id], 0.5, 1.0);
    })";
  auto vertex = CompileShader(source, "vs_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  auto pipeline = CreatePipeline(vertex.bytecode.get(),
                                 white_pixel_shader_.get(),
                                 test_case.pipeline_topology);
  ASSERT_TRUE(pipeline);
  const auto readback = DrawAndRead(pipeline.get(), test_case.draw_topology,
                                    test_case.vertex_count);
  const UINT covered = CoveredPixelCount(readback);
  EXPECT_EQ(covered != 0, test_case.expects_fragments);
}

INSTANTIATE_TEST_SUITE_P(
    Topologies, PrimitiveRasterEdgeSpec,
    ::testing::Values(
        PrimitiveCase{D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                      D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
                      "float2(-0.8, 0.0), float2(0.0, 0.0), "
                      "float2(0.8, 0.0)",
                      3, false, "DegenerateTriangle"},
        PrimitiveCase{D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
                      D3D_PRIMITIVE_TOPOLOGY_LINELIST,
                      "float2(-0.8, 0.0), float2(0.8, 0.0), float2(0, 0)",
                      2, true, "Line"},
        PrimitiveCase{D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT,
                      D3D_PRIMITIVE_TOPOLOGY_POINTLIST,
                      "float2(0.0, 0.0), float2(0, 0), float2(0, 0)",
                      1, true, "Point"}),
    [](const ::testing::TestParamInfo<PrimitiveCase> &info) {
      return info.param.name;
    });

TEST_F(GraphicsEdgeSemanticsSpec, NegativeSlopeScaledDepthBiasIncreasesCoverage) {
  auto vertex = CompileShader(R"(
    float4 main(uint id : SV_VertexID) : SV_Position {
      const float4 positions[3] = {
        float4(-1.0, -1.0, 0.30, 1.0),
        float4(-1.0,  3.0, 0.80, 1.0),
        float4( 3.0, -1.0, 0.30, 1.0)
      };
      return positions[id];
    })",
                              "vs_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  auto unbiased =
      CreatePipeline(vertex.bytecode.get(), white_pixel_shader_.get(),
                     D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 1, true);
  auto biased = CreatePipeline(vertex.bytecode.get(), white_pixel_shader_.get(),
                               D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 1, true,
                               -8.0f, -0.20f);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
  auto dsv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
  std::array<ComPtr<ID3D12Resource>, 2> colors = {CreateColorTarget(),
                                                 CreateColorTarget()};
  std::array<ComPtr<ID3D12Resource>, 2> depths = {
      CreateTexture(DXGI_FORMAT_D32_FLOAT, 1,
                    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                    D3D12_RESOURCE_STATE_DEPTH_WRITE),
      CreateTexture(DXGI_FORMAT_D32_FLOAT, 1,
                    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                    D3D12_RESOURCE_STATE_DEPTH_WRITE)};
  ASSERT_TRUE(unbiased);
  ASSERT_TRUE(biased);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(dsv_heap);
  ASSERT_TRUE(colors[0]);
  ASSERT_TRUE(colors[1]);
  ASSERT_TRUE(depths[0]);
  ASSERT_TRUE(depths[1]);

  ID3D12PipelineState *pipelines[] = {unbiased.get(), biased.get()};
  constexpr FLOAT kBlack[4] = {};
  for (UINT i = 0; i < 2; ++i) {
    const auto rtv = context_.CpuDescriptorHandle(rtv_heap.get(), i);
    const auto dsv = context_.CpuDescriptorHandle(dsv_heap.get(), i);
    context_.device()->CreateRenderTargetView(colors[i].get(), nullptr, rtv);
    context_.device()->CreateDepthStencilView(depths[i].get(), nullptr, dsv);
    context_.list()->ClearRenderTargetView(rtv, kBlack, 0, nullptr);
    context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.5f,
                                           0, 0, nullptr);
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    SetDrawState(pipelines[i], D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.list()->DrawInstanced(3, 1, 0, 0);
    D3D12TestContext::Transition(context_.list(), colors[i].get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
  }

  std::array<TextureReadback, 2> readbacks;
  ASSERT_EQ(context_.ReadbackTexture(colors[0].get(), &readbacks[0]), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  ASSERT_EQ(context_.ReadbackTexture(colors[1].get(), &readbacks[1]), S_OK);
  const UINT unbiased_count = CoveredPixelCount(readbacks[0]);
  const UINT biased_count = CoveredPixelCount(readbacks[1]);
  EXPECT_GT(unbiased_count, 0u);
  EXPECT_LT(unbiased_count, kSize * kSize);
  EXPECT_GT(biased_count, unbiased_count);
}

TEST_F(GraphicsEdgeSemanticsSpec,
       CentroidInterpolationStaysInsidePartiallyCoveredPrimitive) {
  if (!Supports4xMsaa())
    GTEST_SKIP() << "4x MSAA is unavailable";
  auto vertex = CompileShader(R"(
    struct Output {
      float4 position : SV_Position;
      centroid float2 barycentric : TEXCOORD0;
    };
    Output main(uint id : SV_VertexID) {
      const float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 1.0), float2(1.0, -1.0)
      };
      const float2 barycentrics[3] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0)
      };
      Output output;
      output.position = float4(positions[id], 0.5, 1.0);
      output.barycentric = barycentrics[id];
      return output;
    })",
                              "vs_5_0");
  auto pixel = CompileShader(R"(
    float4 main(centroid float2 barycentric : TEXCOORD0) : SV_Target {
      bool inside = all(barycentric >= -0.001) &&
                    barycentric.x + barycentric.y <= 1.001;
      return inside ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
    })",
                             "ps_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto pipeline = CreatePipeline(vertex.bytecode.get(), pixel.bytecode.get(),
                                 D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 4);
  auto source = CreateColorTarget(4);
  auto destination = CreateTexture(kColorFormat, 1, D3D12_RESOURCE_FLAG_NONE,
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
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  SetDrawState(pipeline.get(), D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->DrawInstanced(3, 1, 0, 0);
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
  UINT green_pixels = 0;
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      const std::uint32_t value = PixelAt(readback, x, y);
      EXPECT_EQ(value & 0xffu, 0u) << "red failure at (" << x << ", " << y
                                  << ")";
      green_pixels += ((value >> 8) & 0xffu) != 0;
    }
  }
  EXPECT_GT(green_pixels, 0u);
}

DXMT_SERIAL_TEST_F(GraphicsEdgeSemanticsSpec,
                   SampleInterpolationUsesPerSampleLocations) {
  if (!Supports4xMsaa())
    GTEST_SKIP() << "4x MSAA is unavailable";
  auto vertex = CompileShader(R"(
    struct Output {
      float4 position : SV_Position;
      noperspective float ndc_x : TEXCOORD0;
    };
    Output main(uint id : SV_VertexID) {
      const float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      Output output;
      output.position = float4(positions[id], 0.5, 1.0);
      output.ndc_x = positions[id].x;
      return output;
    })",
                              "vs_5_0");
  auto pixel = CompileShader(R"(
    float4 main(sample noperspective float ndc_x : TEXCOORD0) : SV_Target {
      const float threshold = 1.0 / 32.0;
      return float4(ndc_x >= threshold, 0.0, 0.0, 1.0);
    })",
                             "ps_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto pipeline = CreatePipeline(vertex.bytecode.get(), pixel.bytecode.get(),
                                 D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 4);
  auto source = CreateColorTarget(4);
  auto destination = CreateTexture(kColorFormat, 1, D3D12_RESOURCE_FLAG_NONE,
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
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  SetDrawState(pipeline.get(), D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->DrawInstanced(3, 1, 0, 0);
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

  UINT mixed_pixels = 0;
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      const std::uint32_t value = PixelAt(readback, x, y);
      EXPECT_GE((value >> 24) & 0xffu, 250u)
          << "incomplete coverage at (" << x << ", " << y << ")";
      const UINT red = value & 0xffu;
      mixed_pixels += red > 16u && red < 239u;
    }
  }
  EXPECT_GT(mixed_pixels, kSize / 2);
}

// Keep the MSAA fixed-function path out of the concurrent Metal worker wave.
DXMT_SERIAL_TEST_F(GraphicsEdgeSemanticsSpec,
                   AlphaToCoverageRejectsZeroAndPreservesOpaqueAlpha) {
  if (!Supports4xMsaa())
    GTEST_SKIP() << "4x MSAA is unavailable";
  auto vertex = CompileShader(R"(
    float4 main(uint id : SV_VertexID) : SV_Position {
      const float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      return float4(positions[id], 0.5, 1.0);
    })",
                              "vs_5_0");
  auto pixel = CompileShader(R"(
    float4 main(float4 position : SV_Position) : SV_Target {
      float alpha = position.x < 16.0 ? 0.0 : 1.0;
      return float4(0.0, 1.0, 0.0, alpha);
    })",
                             "ps_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto pipeline = CreatePipeline(
      vertex.bytecode.get(), pixel.bytecode.get(),
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 4, false, 0.0f, 0.0f, true);
  auto source = CreateColorTarget(4);
  auto destination = CreateTexture(kColorFormat, 1, D3D12_RESOURCE_FLAG_NONE,
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
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  SetDrawState(pipeline.get(), D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->DrawInstanced(3, 1, 0, 0);
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
      const std::uint32_t expected = x < kSize / 2 ? 0u : 0xff00ff00u;
      EXPECT_TRUE(ColorsMatch(PixelAt(readback, x, y), expected, 1))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(GraphicsEdgeSemanticsSpec,
       CoverageAndSampleIndexDrivePerSamplePixelShading) {
  if (!Supports4xMsaa())
    GTEST_SKIP() << "4x MSAA is unavailable";
  auto vertex = CompileShader(R"(
    float4 main(uint id : SV_VertexID) : SV_Position {
      const float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      return float4(positions[id], 0.5, 1.0);
    })",
                              "vs_5_0");
  auto pixel = CompileShader(R"(
    struct Output {
      float4 color : SV_Target;
      uint coverage : SV_Coverage;
    };
    Output main(uint sample_index : SV_SampleIndex) {
      Output output;
      output.color = float4(0.0, (sample_index + 1) * 0.25, 0.0, 1.0);
      output.coverage = 0x5;
      return output;
    })",
                             "ps_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto pipeline = CreatePipeline(vertex.bytecode.get(), pixel.bytecode.get(),
                                 D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 4);
  auto source = CreateColorTarget(4);
  auto destination = CreateTexture(kColorFormat, 1, D3D12_RESOURCE_FLAG_NONE,
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
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  SetDrawState(pipeline.get(), D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.list()->DrawInstanced(3, 1, 0, 0);
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
      EXPECT_TRUE(ColorsMatch(PixelAt(readback, x, y), 0x80004000u, 3))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

} // namespace
