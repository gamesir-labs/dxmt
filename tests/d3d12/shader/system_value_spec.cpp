#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

constexpr UINT kSize = 16;
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kRed = 0xff0000ffu;
constexpr std::uint32_t kGreen = 0xff00ff00u;

std::uint32_t PixelAt(const TextureReadback &readback, UINT x, UINT y) {
  std::uint32_t pixel = 0;
  std::memcpy(&pixel,
              readback.data.data() + y * readback.row_pitch +
                  x * sizeof(pixel),
              sizeof(pixel));
  return pixel;
}

struct HalfColorCounts {
  UINT left_red = 0;
  UINT left_green = 0;
  UINT right_red = 0;
  UINT right_green = 0;
};

HalfColorCounts CountHalfColors(const TextureReadback &readback) {
  HalfColorCounts result;
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      const auto pixel = PixelAt(readback, x, y);
      const bool red = ColorsMatch(pixel, kRed, 1);
      const bool green = ColorsMatch(pixel, kGreen, 1);
      if (x < readback.width / 2) {
        result.left_red += red;
        result.left_green += green;
      } else {
        result.right_red += red;
        result.right_green += green;
      }
    }
  }
  return result;
}

class D3D12ShaderSystemValueSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);
  }

  ComPtr<ID3D12PipelineState> CreatePipeline(std::string_view vertex_source,
                                             std::string_view pixel_source) {
    const auto vertex = CompileShader(std::string(vertex_source), "vs_5_0");
    EXPECT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    if (FAILED(vertex.result))
      return {};

    return CreatePipelineFromBytecode(
        {vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize()},
        pixel_source, {});
  }

  ComPtr<ID3D12PipelineState> CreatePipelineFromBytecode(
      D3D12_SHADER_BYTECODE vertex, std::string_view pixel_source,
      D3D12_INPUT_LAYOUT_DESC input_layout) {
    const auto pixel = CompileShader(std::string(pixel_source), "ps_5_0");
    EXPECT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    if (FAILED(pixel.result))
      return {};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.VS = vertex;
    desc.PS = {pixel.bytecode->GetBufferPointer(),
               pixel.bytecode->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = std::numeric_limits<UINT>::max();
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.InputLayout = input_layout;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kFormat;
    desc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, __uuidof(ID3D12PipelineState),
                  reinterpret_cast<void **>(pipeline.put())),
              S_OK);
    return pipeline;
  }

  TextureReadback DrawAndRead(ID3D12PipelineState *pipeline,
                              UINT vertex_count, UINT instance_count = 1,
                              UINT start_vertex = 0,
                              UINT start_instance = 0,
                              const D3D12_VERTEX_BUFFER_VIEW *vertex_buffers =
                                  nullptr,
                              UINT vertex_buffer_count = 0) {
    auto target = context_.CreateTexture2D(
        kSize, kSize, 1, kFormat,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
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
    context_.list()->SetGraphicsRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (vertex_buffer_count)
      context_.list()->IASetVertexBuffers(0, vertex_buffer_count,
                                          vertex_buffers);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, float(kSize), float(kSize), 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, LONG(kSize), LONG(kSize)};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(vertex_count, instance_count, start_vertex,
                                    start_instance);
    D3D12TestContext::Transition(context_.list(), target.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    EXPECT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
    return readback;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
};

TEST_F(D3D12ShaderSystemValueSpec,
       VertexAndInstanceIdsRemainDrawLocalWithNonzeroOffsets) {
  auto pipeline = CreatePipeline(R"(
    struct Output {
      float4 position : SV_Position;
      nointerpolation uint instance_id : TEXCOORD0;
    };
    Output main(uint vertex_id : SV_VertexID,
                uint instance_id : SV_InstanceID) {
      const float2 positions[3] = {
        float2(-0.8, -0.7), float2(0.0, 0.8), float2(0.8, -0.7)
      };
      const uint local_vertex = vertex_id;
      const uint local_instance = instance_id;
      Output output;
      output.position = float4(
          positions[local_vertex].x * 0.45 +
              (local_instance == 0 ? -0.5 : 0.5),
          positions[local_vertex].y, 0.5, 1.0);
      output.instance_id = local_instance;
      return output;
    })",
                                 R"(
    float4 main(nointerpolation uint instance_id : TEXCOORD0) : SV_Target {
      return instance_id == 0 ? float4(1, 0, 0, 1)
                              : float4(0, 1, 0, 1);
    })");
  ASSERT_TRUE(pipeline);
  const auto readback = DrawAndRead(pipeline.get(), 3, 2, 4, 7);
  ASSERT_FALSE(readback.data.empty());
  const auto counts = CountHalfColors(readback);
  EXPECT_GT(counts.left_red, 20u);
  EXPECT_EQ(counts.left_green, 0u);
  EXPECT_EQ(counts.right_red, 0u);
  EXPECT_GT(counts.right_green, 20u);
}

TEST_F(D3D12ShaderSystemValueSpec, PositionReportsPixelCenterCoordinates) {
  auto pipeline = CreatePipeline(R"(
    float4 main(uint vertex_id : SV_VertexID) : SV_Position {
      const float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      return float4(positions[vertex_id], 0.5, 1.0);
    })",
                                 R"(
    float4 main(float4 position : SV_Position) : SV_Target {
      return float4(position.xy / 16.0, 0.0, 1.0);
    })");
  ASSERT_TRUE(pipeline);
  const auto readback = DrawAndRead(pipeline.get(), 3);
  ASSERT_FALSE(readback.data.empty());
  for (const auto [x, y] : {std::pair{1u, 2u}, std::pair{7u, 8u},
                            std::pair{14u, 13u}}) {
    const auto pixel = PixelAt(readback, x, y);
    const int red = pixel & 0xff;
    const int green = (pixel >> 8) & 0xff;
    const int expected_red = int((float(x) + 0.5f) * 255.0f / kSize);
    const int expected_green = int((float(y) + 0.5f) * 255.0f / kSize);
    EXPECT_NEAR(red, expected_red, 2) << "pixel (" << x << ", " << y << ")";
    EXPECT_NEAR(green, expected_green, 2)
        << "pixel (" << x << ", " << y << ")";
  }
}

TEST_F(D3D12ShaderSystemValueSpec,
       FrontFaceDistinguishesOppositeWindingTriangles) {
  auto pipeline = CreatePipeline(R"(
    float4 main(uint vertex_id : SV_VertexID) : SV_Position {
      const float2 positions[6] = {
        float2(-0.9, -0.7), float2(-0.1, -0.7), float2(-0.5, 0.7),
        float2( 0.1, -0.7), float2( 0.5,  0.7), float2( 0.9, -0.7)
      };
      return float4(positions[vertex_id], 0.5, 1.0);
    })",
                                 R"(
    float4 main(bool front_face : SV_IsFrontFace) : SV_Target {
      return front_face ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
    })");
  ASSERT_TRUE(pipeline);
  const auto readback = DrawAndRead(pipeline.get(), 6);
  ASSERT_FALSE(readback.data.empty());
  const auto counts = CountHalfColors(readback);
  const bool left_is_red = counts.left_red > 20 && counts.left_green == 0;
  const bool left_is_green = counts.left_green > 20 && counts.left_red == 0;
  const bool right_is_red = counts.right_red > 20 && counts.right_green == 0;
  const bool right_is_green = counts.right_green > 20 && counts.right_red == 0;
  EXPECT_TRUE((left_is_red && right_is_green) ||
              (left_is_green && right_is_red));
}

TEST_F(D3D12ShaderSystemValueSpec,
       PrimitiveIdIncrementsAcrossTriangleList) {
  auto pipeline = CreatePipeline(R"(
    float4 main(uint vertex_id : SV_VertexID) : SV_Position {
      const float2 positions[6] = {
        float2(-0.9, -0.7), float2(-0.5, 0.7), float2(-0.1, -0.7),
        float2( 0.1, -0.7), float2( 0.5, 0.7), float2( 0.9, -0.7)
      };
      return float4(positions[vertex_id], 0.5, 1.0);
    })",
                                 R"(
    float4 main(uint primitive_id : SV_PrimitiveID) : SV_Target {
      return primitive_id == 0 ? float4(1, 0, 0, 1)
                               : float4(0, 1, 0, 1);
    })");
  ASSERT_TRUE(pipeline);
  const auto readback = DrawAndRead(pipeline.get(), 6);
  ASSERT_FALSE(readback.data.empty());
  const auto counts = CountHalfColors(readback);
  EXPECT_GT(counts.left_red, 20u);
  EXPECT_EQ(counts.left_green, 0u);
  EXPECT_EQ(counts.right_red, 0u);
  EXPECT_GT(counts.right_green, 20u);
}

TEST_F(D3D12ShaderSystemValueSpec, ClipDistanceRejectsNegativePrimitive) {
  // The Wine/vkd3d HLSL frontend used by CompileShader does not yet accept
  // SV_ClipDistance. This vertex shader is the precompiled SM4 DXBC from
  // Wine's d3d10core clip/cull-distance test. It copies four input clip
  // distances and four input cull distances to the corresponding system
  // value outputs without requiring shader resources.
  static constexpr DWORD kClipDistanceVertexShader[] = {
      0x43425844, 0xa24fb3ea, 0x92e2c2b0, 0xb599b1b9, 0xd671f830,
      0x00000001, 0x00000374, 0x00000003, 0x0000002c, 0x0000013c,
      0x000001f0, 0x4e475349, 0x00000108, 0x00000009, 0x00000008,
      0x000000e0, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
      0x00000f0f, 0x000000e9, 0x00000000, 0x00000000, 0x00000003,
      0x00000001, 0x00000101, 0x000000e9, 0x00000001, 0x00000000,
      0x00000003, 0x00000002, 0x00000101, 0x000000e9, 0x00000002,
      0x00000000, 0x00000003, 0x00000003, 0x00000101, 0x000000e9,
      0x00000003, 0x00000000, 0x00000003, 0x00000004, 0x00000101,
      0x000000f7, 0x00000000, 0x00000000, 0x00000003, 0x00000005,
      0x00000101, 0x000000f7, 0x00000001, 0x00000000, 0x00000003,
      0x00000006, 0x00000101, 0x000000f7, 0x00000002, 0x00000000,
      0x00000003, 0x00000007, 0x00000101, 0x000000f7, 0x00000003,
      0x00000000, 0x00000003, 0x00000008, 0x00000101, 0x49534f50,
      0x4e4f4954, 0x494c4300, 0x49445f50, 0x4e415453, 0x43004543,
      0x5f4c4c55, 0x54534944, 0x45434e41, 0xababab00, 0x4e47534f,
      0x000000ac, 0x00000005, 0x00000008, 0x00000080, 0x00000000,
      0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x0000008c,
      0x00000000, 0x00000002, 0x00000003, 0x00000001, 0x00000807,
      0x0000008c, 0x00000001, 0x00000002, 0x00000003, 0x00000001,
      0x00000708, 0x0000009c, 0x00000000, 0x00000003, 0x00000003,
      0x00000002, 0x00000807, 0x0000009c, 0x00000001, 0x00000003,
      0x00000003, 0x00000002, 0x00000708, 0x505f5653, 0x7469736f,
      0x006e6f69, 0x435f5653, 0x4470696c, 0x61747369, 0x0065636e,
      0x435f5653, 0x446c6c75, 0x61747369, 0x0065636e, 0x52444853,
      0x0000017c, 0x00010040, 0x0000005f, 0x0300005f, 0x001010f2,
      0x00000000, 0x0300005f, 0x00101012, 0x00000001, 0x0300005f,
      0x00101012, 0x00000002, 0x0300005f, 0x00101012, 0x00000003,
      0x0300005f, 0x00101012, 0x00000004, 0x0300005f, 0x00101012,
      0x00000005, 0x0300005f, 0x00101012, 0x00000006, 0x0300005f,
      0x00101012, 0x00000007, 0x0300005f, 0x00101012, 0x00000008,
      0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x04000067,
      0x00102072, 0x00000001, 0x00000002, 0x04000067, 0x00102082,
      0x00000001, 0x00000002, 0x04000067, 0x00102072, 0x00000002,
      0x00000003, 0x04000067, 0x00102082, 0x00000002, 0x00000003,
      0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000,
      0x05000036, 0x00102012, 0x00000001, 0x0010100a, 0x00000001,
      0x05000036, 0x00102022, 0x00000001, 0x0010100a, 0x00000002,
      0x05000036, 0x00102042, 0x00000001, 0x0010100a, 0x00000003,
      0x05000036, 0x00102082, 0x00000001, 0x0010100a, 0x00000004,
      0x05000036, 0x00102012, 0x00000002, 0x0010100a, 0x00000005,
      0x05000036, 0x00102022, 0x00000002, 0x0010100a, 0x00000006,
      0x05000036, 0x00102042, 0x00000002, 0x0010100a, 0x00000007,
      0x05000036, 0x00102082, 0x00000002, 0x0010100a, 0x00000008,
      0x0100003e,
  };

  struct Vertex {
    float position[2];
    float clip_distance[4];
    float cull_distance[4];
  };
  static constexpr Vertex kVertices[] = {
      {{-0.9f, -0.7f}, {1, 1, 1, 1}, {1, 1, 1, 1}},
      {{-0.5f, 0.7f}, {1, 1, 1, 1}, {1, 1, 1, 1}},
      {{-0.1f, -0.7f}, {1, 1, 1, 1}, {1, 1, 1, 1}},
      {{0.1f, -0.7f}, {-1, -1, -1, -1}, {1, 1, 1, 1}},
      {{0.5f, 0.7f}, {-1, -1, -1, -1}, {1, 1, 1, 1}},
      {{0.9f, -0.7f}, {-1, -1, -1, -1}, {1, 1, 1, 1}},
  };
  static constexpr D3D12_INPUT_ELEMENT_DESC kInputElements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
       offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
       0},
      {"CLIP_DISTANCE", 0, DXGI_FORMAT_R32_FLOAT, 0,
       offsetof(Vertex, clip_distance) + 0 * sizeof(float),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"CLIP_DISTANCE", 1, DXGI_FORMAT_R32_FLOAT, 0,
       offsetof(Vertex, clip_distance) + 1 * sizeof(float),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"CLIP_DISTANCE", 2, DXGI_FORMAT_R32_FLOAT, 0,
       offsetof(Vertex, clip_distance) + 2 * sizeof(float),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"CLIP_DISTANCE", 3, DXGI_FORMAT_R32_FLOAT, 0,
       offsetof(Vertex, clip_distance) + 3 * sizeof(float),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"CULL_DISTANCE", 0, DXGI_FORMAT_R32_FLOAT, 0,
       offsetof(Vertex, cull_distance) + 0 * sizeof(float),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"CULL_DISTANCE", 1, DXGI_FORMAT_R32_FLOAT, 0,
       offsetof(Vertex, cull_distance) + 1 * sizeof(float),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"CULL_DISTANCE", 2, DXGI_FORMAT_R32_FLOAT, 0,
       offsetof(Vertex, cull_distance) + 2 * sizeof(float),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"CULL_DISTANCE", 3, DXGI_FORMAT_R32_FLOAT, 0,
       offsetof(Vertex, cull_distance) + 3 * sizeof(float),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  auto pipeline = CreatePipelineFromBytecode(
      {kClipDistanceVertexShader, sizeof(kClipDistanceVertexShader)},
      "float4 main() : SV_Target { return float4(1, 0, 0, 1); }",
      {kInputElements,
       UINT(sizeof(kInputElements) / sizeof(kInputElements[0]))});
  ASSERT_TRUE(pipeline);
  auto vertex_buffer = context_.CreateUploadBuffer(
      sizeof(kVertices), kVertices, sizeof(kVertices));
  ASSERT_TRUE(vertex_buffer);
  const D3D12_VERTEX_BUFFER_VIEW view = {
      vertex_buffer->GetGPUVirtualAddress(), sizeof(kVertices), sizeof(Vertex)};
  const auto readback = DrawAndRead(pipeline.get(), 6, 1, 0, 0, &view, 1);
  ASSERT_FALSE(readback.data.empty());
  const auto counts = CountHalfColors(readback);
  EXPECT_GT(counts.left_red, 20u);
  EXPECT_EQ(counts.right_red, 0u);
  EXPECT_EQ(counts.left_green + counts.right_green, 0u);
}

class D3D12PixelDepthSystemValueSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    auto vertex = CompileShader(R"(
      cbuffer VertexDepth : register(b1) { float vertex_depth; };
      float4 main(uint vertex_id : SV_VertexID) : SV_Position {
        const float2 positions[3] = {
          float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
        };
        return float4(positions[vertex_id], vertex_depth, 1.0);
      })",
                                      "vs_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    vertex_ = std::move(vertex.bytecode);

    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.Num32BitValues = 1;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 1;
    parameters[1].Constants.Num32BitValues = 1;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 2;
    root_desc.pParameters = parameters;
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);

    target_ = context_.CreateTexture2D(
        kSize, kSize, 1, kFormat,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    depth_ = context_.CreateTexture2D(
        kSize, kSize, 1, DXGI_FORMAT_D32_FLOAT,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    rtv_heap_ = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    dsv_heap_ = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
    ASSERT_TRUE(target_);
    ASSERT_TRUE(depth_);
    ASSERT_TRUE(rtv_heap_);
    ASSERT_TRUE(dsv_heap_);
    rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    dsv_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target_.get(), nullptr, rtv_);
    context_.device()->CreateDepthStencilView(depth_.get(), nullptr, dsv_);
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(D3D12_SHADER_BYTECODE pixel,
                 D3D12_COMPARISON_FUNC depth_function) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.VS = {vertex_->GetBufferPointer(), vertex_->GetBufferSize()};
    desc.PS = pixel;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = depth_function;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kFormat;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    EXPECT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, __uuidof(ID3D12PipelineState),
                  reinterpret_cast<void **>(pipeline.put())),
              S_OK);
    return pipeline;
  }

  void ExpectDrawPasses(ID3D12PipelineState *pipeline, float vertex_depth,
                        float pixel_depth, float clear_depth) {
    constexpr FLOAT kBlack[4] = {};
    context_.list()->ClearRenderTargetView(rtv_, kBlack, 0, nullptr);
    context_.list()->ClearDepthStencilView(dsv_, D3D12_CLEAR_FLAG_DEPTH,
                                           clear_depth, 0, 0, nullptr);
    context_.list()->OMSetRenderTargets(1, &rtv_, FALSE, &dsv_);
    context_.list()->SetGraphicsRootSignature(root_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetGraphicsRoot32BitConstant(
        0, std::bit_cast<UINT>(pixel_depth), 0);
    context_.list()->SetGraphicsRoot32BitConstant(
        1, std::bit_cast<UINT>(vertex_depth), 0);
    context_.list()->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, float(kSize), float(kSize), 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, LONG(kSize), LONG(kSize)};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);
    D3D12TestContext::Transition(context_.list(), target_.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(target_.get(), &readback), S_OK);
    ASSERT_FALSE(readback.data.empty());
    for (UINT y = 0; y < kSize; ++y) {
      for (UINT x = 0; x < kSize; ++x) {
        EXPECT_TRUE(ColorsMatch(PixelAt(readback, x, y), kGreen, 1))
            << "pixel (" << x << ", " << y << ")";
      }
    }
  }

  D3D12TestContext context_;
  ComPtr<ID3DBlob> vertex_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12Resource> target_;
  ComPtr<ID3D12Resource> depth_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv_ = {};
};

TEST_F(D3D12PixelDepthSystemValueSpec,
       DepthOutputOverridesInterpolatedDepth) {
  const auto pixel = CompileShader(R"(
    cbuffer PixelDepth : register(b0) { float pixel_depth; };
    struct Output {
      float4 color : SV_Target;
      float depth : SV_Depth;
    };
    Output main() {
      Output output;
      output.color = float4(0, 1, 0, 1);
      output.depth = pixel_depth;
      return output;
    })",
                                   "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto pipeline = CreatePipeline(
      {pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize()},
      D3D12_COMPARISON_FUNC_LESS);
  ASSERT_TRUE(pipeline);
  ExpectDrawPasses(pipeline.get(), 0.8f, 0.2f, 0.5f);
}

TEST_F(D3D12PixelDepthSystemValueSpec,
       DepthLessEqualPreservesConservativeQualifier) {
  // Precompiled SM5 DXBC from Wine's conservative-depth conformance test.
  static constexpr DWORD kPixelShader[] = {
      0x43425844, 0x045c8d00, 0xc49e2ebe, 0x76f6022a, 0xf6996ecc,
      0x00000001, 0x00000108, 0x00000003, 0x0000002c, 0x0000003c,
      0x00000098, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
      0x4e47534f, 0x00000054, 0x00000002, 0x00000008, 0x00000038,
      0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f,
      0x00000042, 0x00000000, 0x00000000, 0x00000003, 0xffffffff,
      0x00000e01, 0x545f5653, 0x65677261, 0x56530074, 0x7065445f,
      0x654c6874, 0x71457373, 0x006c6175, 0x58454853, 0x00000068,
      0x00000050, 0x0000001a, 0x0100086a, 0x04000059, 0x00208e46,
      0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000,
      0x02000065, 0x00027001, 0x08000036, 0x001020f2, 0x00000000,
      0x00004002, 0x00000000, 0x3f800000, 0x00000000, 0x3f800000,
      0x05000036, 0x00027001, 0x0020800a, 0x00000000, 0x00000000,
      0x0100003e,
  };
  auto pipeline = CreatePipeline({kPixelShader, sizeof(kPixelShader)},
                                 D3D12_COMPARISON_FUNC_LESS);
  ASSERT_TRUE(pipeline);
  ExpectDrawPasses(pipeline.get(), 0.7f, 0.4f, 0.5f);
}

TEST_F(D3D12PixelDepthSystemValueSpec,
       DepthGreaterEqualPreservesConservativeQualifier) {
  // Precompiled SM5 DXBC from Wine's conservative-depth conformance test.
  static constexpr DWORD kPixelShader[] = {
      0x43425844, 0xd17af83e, 0xa32c01cc, 0x0d8e9665, 0xe6dc17c2,
      0x00000001, 0x0000010c, 0x00000003, 0x0000002c, 0x0000003c,
      0x0000009c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
      0x4e47534f, 0x00000058, 0x00000002, 0x00000008, 0x00000038,
      0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f,
      0x00000042, 0x00000000, 0x00000000, 0x00000003, 0xffffffff,
      0x00000e01, 0x545f5653, 0x65677261, 0x56530074, 0x7065445f,
      0x72476874, 0x65746165, 0x75714572, 0xab006c61, 0x58454853,
      0x00000068, 0x00000050, 0x0000001a, 0x0100086a, 0x04000059,
      0x00208e46, 0x00000000, 0x00000001, 0x03000065, 0x001020f2,
      0x00000000, 0x02000065, 0x00026001, 0x08000036, 0x001020f2,
      0x00000000, 0x00004002, 0x00000000, 0x3f800000, 0x00000000,
      0x3f800000, 0x05000036, 0x00026001, 0x0020800a, 0x00000000,
      0x00000000, 0x0100003e,
  };
  auto pipeline = CreatePipeline({kPixelShader, sizeof(kPixelShader)},
                                 D3D12_COMPARISON_FUNC_GREATER);
  ASSERT_TRUE(pipeline);
  ExpectDrawPasses(pipeline.get(), 0.3f, 0.6f, 0.5f);
}

} // namespace
