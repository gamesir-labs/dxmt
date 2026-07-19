#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class RtvDsvDimensionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.ShaderRegister = 0;
    parameter.Constants.Num32BitValues = 1;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);

    auto vertex = CompileShader(R"(
      cbuffer Constants : register(b0) { float depth; };
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[vertex_id], depth, 1.0);
      })",
                                "vs_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    vertex_shader_ = std::move(vertex.bytecode);

    auto pixel = CompileShader(
        "float4 main() : SV_Target { return float4(0, 1, 0, 1); }", "ps_5_0");
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    pixel_shader_ = std::move(pixel.bytecode);
  }

  bool Supports(DXGI_FORMAT format, D3D12_FORMAT_SUPPORT1 required,
                UINT sample_count = 1) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    if (context_.device()->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)) != S_OK ||
        (support.Support1 & required) != required)
      return false;
    if (sample_count == 1)
      return true;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    quality.Format = format;
    quality.SampleCount = sample_count;
    return context_.device()->CheckFeatureSupport(
               D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
               sizeof(quality)) == S_OK &&
           quality.NumQualityLevels != 0;
  }

  ComPtr<ID3D12Resource>
  CreateTexture(D3D12_RESOURCE_DIMENSION dimension, UINT64 width, UINT height,
                UINT16 depth_or_array, UINT16 mip_levels, DXGI_FORMAT format,
                D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                UINT sample_count = 1) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = dimension;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = depth_or_array;
    desc.MipLevels = mip_levels;
    desc.Format = format;
    desc.SampleDesc.Count = sample_count;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> texture;
    EXPECT_EQ(context_.device()->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                  IID_PPV_ARGS(texture.put())),
              S_OK);
    return texture;
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(DXGI_FORMAT dsv_format, UINT sample_count,
                 const D3D12_DEPTH_STENCIL_DESC &depth_stencil) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.VS = {vertex_shader_->GetBufferPointer(),
               vertex_shader_->GetBufferSize()};
    desc.PS = {pixel_shader_->GetBufferPointer(),
               pixel_shader_->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState = depth_stencil;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DSVFormat = dsv_format;
    desc.SampleDesc.Count = sample_count;
    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(pipeline.put())),
              S_OK);
    return pipeline;
  }

  void Draw(ID3D12PipelineState *pipeline, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
            const D3D12_CPU_DESCRIPTOR_HANDLE *dsv, float depth,
            UINT stencil_ref, const D3D12_RECT &scissor, UINT width,
            UINT height) {
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, dsv);
    context_.list()->SetGraphicsRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetGraphicsRoot32BitConstant(0, std::bit_cast<UINT>(depth),
                                                  0);
    context_.list()->OMSetStencilRef(stencil_ref);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, 1.0f};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);
  }

  HRESULT ReadbackAndReset(ID3D12Resource *texture, TextureReadback *readback,
                           UINT subresource = 0) {
    const HRESULT hr = context_.ReadbackTexture(texture, readback, subresource);
    return FAILED(hr) ? hr : context_.ResetCommandList();
  }

  std::uint32_t Color(const TextureReadback &readback, UINT x, UINT y,
                      UINT z = 0) {
    std::uint32_t value = 0;
    const std::size_t offset =
        std::size_t(z) * readback.row_pitch * readback.height +
        std::size_t(y) * readback.row_pitch + x * sizeof(value);
    EXPECT_LE(offset + sizeof(value), readback.data.size());
    if (offset + sizeof(value) <= readback.data.size())
      std::memcpy(&value, readback.data.data() + offset, sizeof(value));
    return value;
  }

  float Depth(const TextureReadback &readback, UINT x, UINT y) {
    float value = 0.0f;
    const std::size_t offset =
        std::size_t(y) * readback.row_pitch + x * sizeof(value);
    EXPECT_LE(offset + sizeof(value), readback.data.size());
    if (offset + sizeof(value) <= readback.data.size())
      std::memcpy(&value, readback.data.data() + offset, sizeof(value));
    return value;
  }

  void ExpectColorSolid(const TextureReadback &readback, std::uint32_t expected,
                        UINT z = 0) {
    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        const auto actual = Color(readback, x, y, z);
        EXPECT_TRUE(ColorsMatch(actual, expected, 0))
            << "pixel (" << x << ", " << y << ", " << z << ") was 0x"
            << std::hex << actual << ", expected 0x" << expected;
      }
    }
  }

  void ExpectDepthSolid(const TextureReadback &readback, float expected) {
    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        EXPECT_NEAR(Depth(readback, x, y), expected, 1.0e-6f)
            << "pixel (" << x << ", " << y << ')';
      }
    }
  }

  void ExpectRedGreenSplit(const TextureReadback &readback) {
    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        const auto expected =
            x < readback.width / 2 ? 0xff0000ffu : 0xff00ff00u;
        EXPECT_TRUE(ColorsMatch(Color(readback, x, y), expected, 0))
            << "pixel (" << x << ", " << y << ')';
      }
    }
  }

  void ExpectHealthyAfterFence() {
    ASSERT_EQ(context_.SignalAndWait(), S_OK);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3DBlob> vertex_shader_;
  ComPtr<ID3DBlob> pixel_shader_;
};

TEST_F(RtvDsvDimensionSpec, Texture2DMultisampleRtvAndDsvDrawResolve) {
  constexpr UINT kSize = 4;
  constexpr UINT kSamples = 4;
  const auto color_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;
  const auto depth_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL |
      D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, color_required, kSamples) ||
      !Supports(DXGI_FORMAT_D32_FLOAT, depth_required, kSamples))
    GTEST_SKIP() << "4x color/depth render targets are unsupported";

  auto color = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, kSize, kSize,
                             1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                             D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_RENDER_TARGET, kSamples);
  auto resolved =
      CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, kSize, kSize, 1, 1,
                    DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
                    D3D12_RESOURCE_STATE_RESOLVE_DEST);
  auto depth = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, kSize, kSize,
                             1, 1, DXGI_FORMAT_D32_FLOAT,
                             D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                             D3D12_RESOURCE_STATE_DEPTH_WRITE, kSamples);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto dsv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(color);
  ASSERT_TRUE(resolved);
  ASSERT_TRUE(depth);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(dsv_heap);

  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(color.get(), &rtv_desc, rtv);
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
  dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
  dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
  const auto dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(depth.get(), &dsv_desc, dsv);

  constexpr FLOAT poison[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, poison, 0, nullptr);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0,
                                         0, nullptr);
  D3D12_DEPTH_STENCIL_DESC depth_state = {};
  depth_state.DepthEnable = TRUE;
  depth_state.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  depth_state.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  auto pipeline = CreatePipeline(DXGI_FORMAT_D32_FLOAT, kSamples, depth_state);
  ASSERT_TRUE(pipeline);
  constexpr D3D12_RECT left = {0, 0, kSize / 2, kSize};
  constexpr D3D12_RECT right = {kSize / 2, 0, kSize, kSize};
  Draw(pipeline.get(), rtv, &dsv, 0.75f, 0, left, kSize, kSize);
  Draw(pipeline.get(), rtv, &dsv, 0.25f, 0, right, kSize, kSize);
  D3D12TestContext::Transition(context_.list(), color.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  context_.list()->ResolveSubresource(resolved.get(), 0, color.get(), 0,
                                      DXGI_FORMAT_R8G8B8A8_UNORM);
  D3D12TestContext::Transition(context_.list(), resolved.get(),
                               D3D12_RESOURCE_STATE_RESOLVE_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(ReadbackAndReset(resolved.get(), &readback), S_OK);
  ExpectRedGreenSplit(readback);
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, Texture3DRtvClearHonorsWSliceRange) {
  const auto required =
      D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, required))
    GTEST_SKIP() << "R8G8B8A8_UNORM 3D render targets are unsupported";
  auto texture = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE3D, 2, 2, 4, 1,
                               DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  std::array<std::uint32_t, 16> poison;
  poison.fill(0xffff0000u);
  ASSERT_EQ(context_.UploadTextureAndReset(texture.get(), poison.data(),
                                           2 * sizeof(poison[0]),
                                           4 * sizeof(poison[0])),
            S_OK);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_RENDER_TARGET_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
  desc.Texture3D.FirstWSlice = 1;
  desc.Texture3D.WSize = 2;
  context_.device()->CreateRenderTargetView(texture.get(), &desc, rtv);
  constexpr FLOAT selected[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, selected, 0, nullptr);
  constexpr FLOAT rect_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  constexpr D3D12_RECT left = {0, 0, 1, 2};
  context_.list()->ClearRenderTargetView(rtv, rect_color, 1, &left);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(ReadbackAndReset(texture.get(), &readback), S_OK);
  ExpectColorSolid(readback, 0xffff0000u, 0);
  for (UINT z = 1; z <= 2; ++z) {
    for (UINT y = 0; y < readback.height; ++y) {
      EXPECT_TRUE(ColorsMatch(Color(readback, 0, y, z), 0xff0000ffu, 0));
      EXPECT_TRUE(ColorsMatch(Color(readback, 1, y, z), 0xff00ff00u, 0));
    }
  }
  ExpectColorSolid(readback, 0xffff0000u, 3);
  ExpectHealthyAfterFence();
}

DXMT_SERIAL_TEST_F(RtvDsvDimensionSpec,
                   Texture3DClearDoesNotMergeAcrossWSlices) {
  const auto required =
      D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, required))
    GTEST_SKIP() << "R8G8B8A8_UNORM 3D render targets are unsupported";
  auto texture = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE3D, 2, 2, 4, 1,
                               DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  std::array<std::uint32_t, 16> poison;
  poison.fill(0xffff0000u);
  ASSERT_EQ(context_.UploadTextureAndReset(texture.get(), poison.data(),
                                           2 * sizeof(poison[0]),
                                           4 * sizeof(poison[0])),
            S_OK);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);

  D3D12_RENDER_TARGET_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
  desc.Texture3D.WSize = 1;
  desc.Texture3D.FirstWSlice = 1;
  const auto clear_rtv = context_.CpuDescriptorHandle(heap.get(), 0);
  context_.device()->CreateRenderTargetView(texture.get(), &desc, clear_rtv);
  desc.Texture3D.FirstWSlice = 2;
  const auto draw_rtv = context_.CpuDescriptorHandle(heap.get(), 1);
  context_.device()->CreateRenderTargetView(texture.get(), &desc, draw_rtv);

  constexpr FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(clear_rtv, red, 0, nullptr);
  D3D12_DEPTH_STENCIL_DESC depth_state = {};
  auto pipeline = CreatePipeline(DXGI_FORMAT_UNKNOWN, 1, depth_state);
  ASSERT_TRUE(pipeline);
  constexpr D3D12_RECT full = {0, 0, 2, 2};
  Draw(pipeline.get(), draw_rtv, nullptr, 0.0f, 0, full, 2, 2);

  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(ReadbackAndReset(texture.get(), &readback), S_OK);
  ExpectColorSolid(readback, 0xffff0000u, 0);
  ExpectColorSolid(readback, 0xff0000ffu, 1);
  ExpectColorSolid(readback, 0xff00ff00u, 2);
  ExpectColorSolid(readback, 0xffff0000u, 3);
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, InvalidTexture3DRtvIsNoOpAndCanBeOverwritten) {
  const auto required =
      D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, required))
    GTEST_SKIP() << "R8G8B8A8_UNORM 3D render targets are unsupported";
  auto texture = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE3D, 2, 2, 4, 1,
                               DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  std::array<std::uint32_t, 16> poison;
  poison.fill(0xffff0000u);
  ASSERT_EQ(context_.UploadTextureAndReset(texture.get(), poison.data(),
                                           2 * sizeof(poison[0]),
                                           4 * sizeof(poison[0])),
            S_OK);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_RENDER_TARGET_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
  desc.Texture3D.FirstWSlice = 4;
  desc.Texture3D.WSize = 1;
  context_.device()->CreateRenderTargetView(texture.get(), &desc, rtv);
  constexpr FLOAT selected[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, selected, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback invalid_readback;
  ASSERT_EQ(ReadbackAndReset(texture.get(), &invalid_readback), S_OK);
  for (UINT z = 0; z < 4; ++z)
    ExpectColorSolid(invalid_readback, 0xffff0000u, z);

  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  desc.Texture3D.FirstWSlice = 2;
  desc.Texture3D.WSize = 1;
  context_.device()->CreateRenderTargetView(texture.get(), &desc, rtv);
  context_.list()->ClearRenderTargetView(rtv, selected, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback valid_readback;
  ASSERT_EQ(ReadbackAndReset(texture.get(), &valid_readback), S_OK);
  for (UINT z = 0; z < 4; ++z) {
    ExpectColorSolid(valid_readback,
                     z == 2 ? 0xff00ff00u : 0xffff0000u, z);
  }
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, Texture1DArrayRtvCoversRequestedSlices) {
  constexpr UINT kSlices = 4;
  const auto required =
      D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, required))
    GTEST_SKIP() << "1D array color targets are unsupported";

  auto color = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE1D, 4, 1, kSlices,
                             1, DXGI_FORMAT_R8G8B8A8_UNORM,
                             D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
  ASSERT_TRUE(color);
  ASSERT_TRUE(rtv_heap);

  const auto default_rtv = context_.CpuDescriptorHandle(rtv_heap.get(), 0);
  context_.device()->CreateRenderTargetView(color.get(), nullptr, default_rtv);
  constexpr FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(default_rtv, red, 0, nullptr);

  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
  rtv_desc.Texture1DArray.FirstArraySlice = 1;
  rtv_desc.Texture1DArray.ArraySize = 2;
  const auto range_rtv = context_.CpuDescriptorHandle(rtv_heap.get(), 1);
  context_.device()->CreateRenderTargetView(color.get(), &rtv_desc, range_rtv);
  constexpr FLOAT green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(range_rtv, green, 0, nullptr);

  D3D12TestContext::Transition(context_.list(), color.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  for (UINT slice = 0; slice < kSlices; ++slice) {
    TextureReadback color_readback;
    ASSERT_EQ(ReadbackAndReset(color.get(), &color_readback, slice), S_OK);
    ExpectColorSolid(color_readback,
                     slice == 1 || slice == 2 ? 0xff00ff00u : 0xff0000ffu);
  }
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, Texture1DArrayDsvCoversRequestedSlices) {
  constexpr UINT kSlices = 4;
  const auto required =
      D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  if (!Supports(DXGI_FORMAT_D32_FLOAT, required))
    GTEST_SKIP() << "1D array depth targets are unsupported";

  auto depth = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE1D, 4, 1, kSlices,
                             1, DXGI_FORMAT_D32_FLOAT,
                             D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                             D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
  ASSERT_TRUE(depth);
  ASSERT_TRUE(heap);

  const auto default_dsv = context_.CpuDescriptorHandle(heap.get(), 0);
  context_.device()->CreateDepthStencilView(depth.get(), nullptr, default_dsv);
  context_.list()->ClearDepthStencilView(default_dsv, D3D12_CLEAR_FLAG_DEPTH,
                                         0.75f, 0, 0, nullptr);

  D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_D32_FLOAT;
  desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
  desc.Texture1DArray.FirstArraySlice = 1;
  desc.Texture1DArray.ArraySize = 2;
  const auto range_dsv = context_.CpuDescriptorHandle(heap.get(), 1);
  context_.device()->CreateDepthStencilView(depth.get(), &desc, range_dsv);
  context_.list()->ClearDepthStencilView(range_dsv, D3D12_CLEAR_FLAG_DEPTH,
                                         0.25f, 0, 0, nullptr);

  D3D12TestContext::Transition(context_.list(), depth.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  for (UINT slice = 0; slice < kSlices; ++slice) {
    TextureReadback readback;
    ASSERT_EQ(ReadbackAndReset(depth.get(), &readback, slice), S_OK);
    ExpectDepthSolid(readback, slice == 1 || slice == 2 ? 0.25f : 0.75f);
  }
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, Texture2DArrayRtvTargetsMipAndArraySlice) {
  constexpr UINT kMips = 2;
  constexpr UINT kSlices = 3;
  const auto required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, required))
    GTEST_SKIP() << "R8G8B8A8_UNORM render targets are unsupported";
  auto texture = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 8, 8,
                               kSlices, kMips, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_RENDER_TARGET_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
  desc.Texture2DArray.ArraySize = 1;
  constexpr FLOAT poison[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  for (UINT slice = 0; slice < kSlices; ++slice) {
    for (UINT mip = 0; mip < kMips; ++mip) {
      desc.Texture2DArray.MipSlice = mip;
      desc.Texture2DArray.FirstArraySlice = slice;
      context_.device()->CreateRenderTargetView(texture.get(), &desc, rtv);
      context_.list()->ClearRenderTargetView(rtv, poison, 0, nullptr);
    }
  }
  desc.Texture2DArray.MipSlice = 1;
  desc.Texture2DArray.FirstArraySlice = 1;
  context_.device()->CreateRenderTargetView(texture.get(), &desc, rtv);
  constexpr FLOAT selected[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, selected, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  for (UINT slice = 0; slice < kSlices; ++slice) {
    for (UINT mip = 0; mip < kMips; ++mip) {
      TextureReadback readback;
      const UINT subresource = mip + slice * kMips;
      ASSERT_EQ(ReadbackAndReset(texture.get(), &readback, subresource), S_OK);
      const auto expected = slice == 1 && mip == 1 ? 0xff00ff00u : 0xffff0000u;
      ExpectColorSolid(readback, expected);
    }
  }
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, Texture1DDsvTargetsNonzeroMip) {
  const auto required =
      D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  if (!Supports(DXGI_FORMAT_D32_FLOAT, required))
    GTEST_SKIP() << "D32_FLOAT 1D depth textures are unsupported";
  auto texture = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE1D, 8, 1, 1, 2,
                               DXGI_FORMAT_D32_FLOAT,
                               D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                               D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_D32_FLOAT;
  desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
  context_.device()->CreateDepthStencilView(texture.get(), &desc, dsv);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.75f, 0,
                                         0, nullptr);
  desc.Texture1D.MipSlice = 1;
  context_.device()->CreateDepthStencilView(texture.get(), &desc, dsv);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.75f, 0,
                                         0, nullptr);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.25f, 0,
                                         0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback mip_zero;
  ASSERT_EQ(ReadbackAndReset(texture.get(), &mip_zero, 0), S_OK);
  ExpectDepthSolid(mip_zero, 0.75f);
  TextureReadback mip_one;
  ASSERT_EQ(ReadbackAndReset(texture.get(), &mip_one, 1), S_OK);
  ExpectDepthSolid(mip_one, 0.25f);
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, Texture2DArrayDsvTargetsMipAndArraySlice) {
  constexpr UINT kMips = 2;
  constexpr UINT kSlices = 3;
  const auto color_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  const auto depth_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, color_required) ||
      !Supports(DXGI_FORMAT_D32_FLOAT, depth_required))
    GTEST_SKIP() << "required color/depth formats are unsupported";
  auto texture = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 8, 8,
                               kSlices, kMips, DXGI_FORMAT_D32_FLOAT,
                               D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                               D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto color = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 4, 4, 1, 1,
                             DXGI_FORMAT_R8G8B8A8_UNORM,
                             D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto dsv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(color);
  ASSERT_TRUE(dsv_heap);
  ASSERT_TRUE(rtv_heap);
  const auto dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(color.get(), nullptr, rtv);
  D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_D32_FLOAT;
  desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
  desc.Texture2DArray.MipSlice = 1;
  desc.Texture2DArray.ArraySize = 1;
  for (UINT slice = 0; slice < kSlices; ++slice) {
    desc.Texture2DArray.FirstArraySlice = slice;
    context_.device()->CreateDepthStencilView(texture.get(), &desc, dsv);
    context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.75f,
                                           0, 0, nullptr);
  }
  desc.Texture2DArray.FirstArraySlice = 1;
  context_.device()->CreateDepthStencilView(texture.get(), &desc, dsv);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.25f, 0,
                                         0, nullptr);
  constexpr FLOAT poison[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, poison, 0, nullptr);
  D3D12_DEPTH_STENCIL_DESC depth_state = {};
  depth_state.DepthEnable = TRUE;
  depth_state.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  depth_state.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
  auto pipeline = CreatePipeline(DXGI_FORMAT_D32_FLOAT, 1, depth_state);
  ASSERT_TRUE(pipeline);
  constexpr D3D12_RECT full = {0, 0, 4, 4};
  Draw(pipeline.get(), rtv, &dsv, 0.5f, 0, full, 4, 4);
  D3D12TestContext::Transition(context_.list(), color.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback color_readback;
  ASSERT_EQ(ReadbackAndReset(color.get(), &color_readback), S_OK);
  ExpectColorSolid(color_readback, 0xff00ff00u);
  for (UINT slice = 0; slice < kSlices; ++slice) {
    TextureReadback readback;
    ASSERT_EQ(ReadbackAndReset(texture.get(), &readback, 1 + slice * kMips),
              S_OK);
    ExpectDepthSolid(readback, slice == 1 ? 0.25f : 0.75f);
  }
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, ReadOnlyDepthDsvTestsAndPreservesDepth) {
  constexpr UINT kSize = 4;
  const auto color_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  const auto depth_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, color_required) ||
      !Supports(DXGI_FORMAT_D32_FLOAT, depth_required))
    GTEST_SKIP() << "required color/depth formats are unsupported";
  auto color = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, kSize, kSize,
                             1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                             D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto depth = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, kSize, kSize,
                             1, 1, DXGI_FORMAT_D32_FLOAT,
                             D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                             D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto dsv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
  ASSERT_TRUE(color);
  ASSERT_TRUE(depth);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(dsv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  const auto writable = context_.CpuDescriptorHandle(dsv_heap.get(), 0);
  const auto read_only = context_.CpuDescriptorHandle(dsv_heap.get(), 1);
  context_.device()->CreateRenderTargetView(color.get(), nullptr, rtv);
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
  dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
  dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  context_.device()->CreateDepthStencilView(depth.get(), &dsv_desc, writable);
  dsv_desc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
  context_.device()->CreateDepthStencilView(depth.get(), &dsv_desc, read_only);
  constexpr FLOAT poison[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, poison, 0, nullptr);
  context_.list()->ClearDepthStencilView(writable, D3D12_CLEAR_FLAG_DEPTH, 0.5f,
                                         0, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), depth.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_DEPTH_READ);
  D3D12_DEPTH_STENCIL_DESC depth_state = {};
  depth_state.DepthEnable = TRUE;
  depth_state.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  depth_state.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  auto pipeline = CreatePipeline(DXGI_FORMAT_D32_FLOAT, 1, depth_state);
  ASSERT_TRUE(pipeline);
  constexpr D3D12_RECT left = {0, 0, kSize / 2, kSize};
  constexpr D3D12_RECT right = {kSize / 2, 0, kSize, kSize};
  Draw(pipeline.get(), rtv, &read_only, 0.75f, 0, left, kSize, kSize);
  Draw(pipeline.get(), rtv, &read_only, 0.25f, 0, right, kSize, kSize);
  D3D12TestContext::Transition(context_.list(), color.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(context_.list(), depth.get(),
                               D3D12_RESOURCE_STATE_DEPTH_READ,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback color_readback;
  ASSERT_EQ(ReadbackAndReset(color.get(), &color_readback), S_OK);
  ExpectRedGreenSplit(color_readback);
  TextureReadback depth_readback;
  ASSERT_EQ(ReadbackAndReset(depth.get(), &depth_readback), S_OK);
  ExpectDepthSolid(depth_readback, 0.5f);
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, ReadOnlyStencilDsvTestsAndPreservesStencil) {
  constexpr UINT kSize = 4;
  const auto color_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  const auto depth_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, color_required) ||
      !Supports(DXGI_FORMAT_D24_UNORM_S8_UINT, depth_required))
    GTEST_SKIP() << "required color/depth-stencil formats are unsupported";
  auto color = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, kSize, kSize,
                             1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                             D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto depth = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, kSize, kSize,
                             1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT,
                             D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                             D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto dsv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
  ASSERT_TRUE(color);
  ASSERT_TRUE(depth);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(dsv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  const auto writable = context_.CpuDescriptorHandle(dsv_heap.get(), 0);
  const auto read_only = context_.CpuDescriptorHandle(dsv_heap.get(), 1);
  context_.device()->CreateRenderTargetView(color.get(), nullptr, rtv);
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
  dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  context_.device()->CreateDepthStencilView(depth.get(), &dsv_desc, writable);
  dsv_desc.Flags = D3D12_DSV_FLAG_READ_ONLY_STENCIL;
  context_.device()->CreateDepthStencilView(depth.get(), &dsv_desc, read_only);
  constexpr FLOAT poison[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, poison, 0, nullptr);
  context_.list()->ClearDepthStencilView(
      writable, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 7, 0,
      nullptr);
  D3D12TestContext::Transition(context_.list(), depth.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_DEPTH_READ);
  D3D12_DEPTH_STENCIL_DESC stencil_state = {};
  stencil_state.DepthEnable = FALSE;
  stencil_state.StencilEnable = TRUE;
  stencil_state.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
  stencil_state.StencilWriteMask = 0;
  stencil_state.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
  stencil_state.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
  stencil_state.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
  stencil_state.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
  stencil_state.BackFace = stencil_state.FrontFace;
  auto pipeline =
      CreatePipeline(DXGI_FORMAT_D24_UNORM_S8_UINT, 1, stencil_state);
  ASSERT_TRUE(pipeline);
  constexpr D3D12_RECT left = {0, 0, kSize / 2, kSize};
  constexpr D3D12_RECT right = {kSize / 2, 0, kSize, kSize};
  Draw(pipeline.get(), rtv, &read_only, 0.0f, 3, left, kSize, kSize);
  Draw(pipeline.get(), rtv, &read_only, 0.0f, 7, right, kSize, kSize);
  D3D12TestContext::Transition(context_.list(), color.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(context_.list(), depth.get(),
                               D3D12_RESOURCE_STATE_DEPTH_READ,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback color_readback;
  ASSERT_EQ(ReadbackAndReset(color.get(), &color_readback), S_OK);
  ExpectRedGreenSplit(color_readback);
  TextureReadback stencil_readback;
  ASSERT_EQ(ReadbackAndReset(depth.get(), &stencil_readback, 1), S_OK);
  for (UINT y = 0; y < stencil_readback.height; ++y) {
    for (UINT x = 0; x < stencil_readback.width; ++x) {
      EXPECT_EQ(stencil_readback.data[y * stencil_readback.row_pitch + x], 7)
          << "stencil pixel (" << x << ", " << y << ')';
    }
  }
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, TypelessResourcesUseCompatibleTypedViews) {
  const auto color_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  const auto depth_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, color_required) ||
      !Supports(DXGI_FORMAT_D24_UNORM_S8_UINT, depth_required))
    GTEST_SKIP() << "compatible typed color/depth views are unsupported";
  auto color = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2, 2, 1, 1,
                             DXGI_FORMAT_R8G8B8A8_TYPELESS,
                             D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto depth = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2, 2, 1, 1,
                             DXGI_FORMAT_R24G8_TYPELESS,
                             D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                             D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto dsv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(color);
  ASSERT_TRUE(depth);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(dsv_heap);
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(color.get(), &rtv_desc, rtv);
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
  dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  const auto dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(depth.get(), &dsv_desc, dsv);
  constexpr FLOAT poison[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, poison, 0, nullptr);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0,
                                         0, nullptr);
  D3D12_DEPTH_STENCIL_DESC depth_state = {};
  depth_state.DepthEnable = TRUE;
  depth_state.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  depth_state.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  auto pipeline = CreatePipeline(DXGI_FORMAT_D24_UNORM_S8_UINT, 1, depth_state);
  ASSERT_TRUE(pipeline);
  constexpr D3D12_RECT left = {0, 0, 1, 2};
  constexpr D3D12_RECT right = {1, 0, 2, 2};
  Draw(pipeline.get(), rtv, &dsv, 0.75f, 0, left, 2, 2);
  Draw(pipeline.get(), rtv, &dsv, 0.25f, 0, right, 2, 2);
  D3D12TestContext::Transition(context_.list(), color.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback color_readback;
  ASSERT_EQ(ReadbackAndReset(color.get(), &color_readback), S_OK);
  ExpectRedGreenSplit(color_readback);
  ExpectHealthyAfterFence();
}

TEST_F(RtvDsvDimensionSpec, InvalidViewFormatsCanBeOverwrittenByValidViews) {
  const auto color_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
  const auto depth_required =
      D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  if (!Supports(DXGI_FORMAT_R8G8B8A8_UNORM, color_required) ||
      !Supports(DXGI_FORMAT_D32_FLOAT, depth_required))
    GTEST_SKIP() << "required color/depth formats are unsupported";
  auto color = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2, 2, 1, 1,
                             DXGI_FORMAT_R8G8B8A8_UNORM,
                             D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto depth = CreateTexture(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2, 2, 1, 1,
                             DXGI_FORMAT_D32_FLOAT,
                             D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                             D3D12_RESOURCE_STATE_DEPTH_WRITE);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto dsv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(color);
  ASSERT_TRUE(depth);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(dsv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = DXGI_FORMAT_D32_FLOAT;
  rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  context_.device()->CreateRenderTargetView(color.get(), &rtv_desc, rtv);
  rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  context_.device()->CreateRenderTargetView(color.get(), &rtv_desc, rtv);
  const auto dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
  dsv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  context_.device()->CreateDepthStencilView(depth.get(), &dsv_desc, dsv);
  dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
  context_.device()->CreateDepthStencilView(depth.get(), &dsv_desc, dsv);
  constexpr FLOAT color_value[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, color_value, 0, nullptr);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.625f, 0,
                                         0, nullptr);
  D3D12TestContext::Transition(context_.list(), color.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(context_.list(), depth.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback color_readback;
  ASSERT_EQ(ReadbackAndReset(color.get(), &color_readback), S_OK);
  ExpectColorSolid(color_readback, 0xff00ff00u);
  TextureReadback depth_readback;
  ASSERT_EQ(ReadbackAndReset(depth.get(), &depth_readback), S_OK);
  ExpectDepthSolid(depth_readback, 0.625f);
  ExpectHealthyAfterFence();
}

} // namespace
