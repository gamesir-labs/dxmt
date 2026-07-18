#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct IndexedDrawCase {
  DXGI_FORMAT format;
  UINT view_offset;
  UINT start_index;
  INT base_vertex;
  UINT valid_vertex_start;
  const char *name;
};

class GraphicsInputAssemblerSpec
    : public ::testing::TestWithParam<IndexedDrawCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto vertex_shader = CompileShader(R"(
      struct Input { float2 position : POSITION; };
      float4 main(Input input) : SV_Position {
        return float4(input.position, 0.0, 1.0);
      })",
                                             "vs_5_0");
    ASSERT_EQ(vertex_shader.result, S_OK) << vertex_shader.diagnostic_text();
    const auto pixel_shader = CompileShader(
        "float4 main() : SV_Target { return 1.0.xxxx; }", "ps_5_0");
    ASSERT_EQ(pixel_shader.result, S_OK) << pixel_shader.diagnostic_text();

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    D3D12_INPUT_ELEMENT_DESC input = {};
    input.SemanticName = "POSITION";
    input.Format = DXGI_FORMAT_R32G32_FLOAT;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature_.get();
    desc.VS = {vertex_shader.bytecode->GetBufferPointer(),
               vertex_shader.bytecode->GetBufferSize()};
    desc.PS = {pixel_shader.bytecode->GetBufferPointer(),
               pixel_shader.bytecode->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.InputLayout = {&input, 1};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(pipeline_.put())),
              S_OK);

    target_ =
        context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv_heap_ =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(target_);
    ASSERT_TRUE(rtv_heap_);
    rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target_.get(), nullptr, rtv_);
  }

  void CreateGeometry(const IndexedDrawCase &test_case) {
    std::array<std::array<float, 2>, 8> vertices = {};
    for (auto &vertex : vertices)
      vertex = {2.0f, 2.0f};
    const std::array valid = {std::array{-1.0f, -1.0f}, std::array{-1.0f, 3.0f},
                              std::array{3.0f, -1.0f}};
    ASSERT_LE(test_case.valid_vertex_start + valid.size(), vertices.size());
    std::copy(valid.begin(), valid.end(),
              vertices.begin() + test_case.valid_vertex_start);
    vertex_buffer_ = context_.CreateUploadBuffer(
        sizeof(vertices), vertices.data(), sizeof(vertices));
    ASSERT_TRUE(vertex_buffer_);
    vertex_view_ = {vertex_buffer_->GetGPUVirtualAddress(), sizeof(vertices),
                    sizeof(vertices[0])};

    const UINT index_size = test_case.format == DXGI_FORMAT_R16_UINT
                                ? sizeof(std::uint16_t)
                                : sizeof(std::uint32_t);
    ASSERT_EQ(test_case.view_offset % index_size, 0u);
    std::vector<std::uint32_t> indices(
        test_case.view_offset / index_size + test_case.start_index, 7u);
    const INT first_index =
        static_cast<INT>(test_case.valid_vertex_start) - test_case.base_vertex;
    ASSERT_GE(first_index, 0);
    indices.push_back(static_cast<UINT>(first_index));
    indices.push_back(static_cast<UINT>(first_index + 1));
    indices.push_back(static_cast<UINT>(first_index + 2));

    std::vector<std::uint8_t> bytes(indices.size() * index_size);
    for (std::size_t i = 0; i < indices.size(); ++i) {
      if (index_size == sizeof(std::uint16_t)) {
        const auto index = static_cast<std::uint16_t>(indices[i]);
        std::memcpy(bytes.data() + i * index_size, &index, index_size);
      } else {
        std::memcpy(bytes.data() + i * index_size, &indices[i], index_size);
      }
    }
    index_buffer_ =
        context_.CreateUploadBuffer(bytes.size(), bytes.data(), bytes.size());
    ASSERT_TRUE(index_buffer_);
    index_view_ = {index_buffer_->GetGPUVirtualAddress() +
                       test_case.view_offset,
                   static_cast<UINT>(bytes.size() - test_case.view_offset),
                   test_case.format};
  }

public:
  static const char *
  Name(const ::testing::TestParamInfo<IndexedDrawCase> &info) {
    return info.param.name;
  }

protected:
  static constexpr UINT kSize = 8;
  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12Resource> target_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  ComPtr<ID3D12Resource> vertex_buffer_;
  ComPtr<ID3D12Resource> index_buffer_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
  D3D12_VERTEX_BUFFER_VIEW vertex_view_ = {};
  D3D12_INDEX_BUFFER_VIEW index_view_ = {};
};

TEST_P(GraphicsInputAssemblerSpec, DrawIndexedBaseVertexMatrix) {
  const auto &test_case = GetParam();
  CreateGeometry(test_case);
  const FLOAT clear[4] = {};
  context_.list()->ClearRenderTargetView(rtv_, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv_, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_signature_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->IASetVertexBuffers(0, 1, &vertex_view_);
  context_.list()->IASetIndexBuffer(&index_view_);
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {
      0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
      0.0f, 1.0f};
  const D3D12_RECT scissor = {0, 0, kSize, kSize};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawIndexedInstanced(3, 1, test_case.start_index,
                                        test_case.base_vertex, 0);
  D3D12TestContext::Transition(context_.list(), target_.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target_.get(), &readback), S_OK);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, 0xffffffff, 0))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex << pixel;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    Indexing, GraphicsInputAssemblerSpec,
    ::testing::Values(
        IndexedDrawCase{DXGI_FORMAT_R16_UINT, 0, 0, 0, 0, "Uint16"},
        IndexedDrawCase{DXGI_FORMAT_R32_UINT, 0, 0, 0, 0, "Uint32"},
        IndexedDrawCase{DXGI_FORMAT_R16_UINT, 8, 0, 0, 0, "Uint16ViewOffset"},
        IndexedDrawCase{DXGI_FORMAT_R32_UINT, 8, 0, 0, 0, "Uint32ViewOffset"},
        IndexedDrawCase{DXGI_FORMAT_R16_UINT, 0, 2, 0, 0, "Uint16StartIndex"},
        IndexedDrawCase{DXGI_FORMAT_R32_UINT, 0, 2, 0, 0, "Uint32StartIndex"},
        IndexedDrawCase{DXGI_FORMAT_R16_UINT, 0, 0, 2, 2,
                        "Uint16PositiveBaseVertex"},
        IndexedDrawCase{DXGI_FORMAT_R32_UINT, 0, 0, 2, 2,
                        "Uint32PositiveBaseVertex"},
        IndexedDrawCase{DXGI_FORMAT_R16_UINT, 0, 0, -2, 0,
                        "Uint16NegativeBaseVertex"},
        IndexedDrawCase{DXGI_FORMAT_R32_UINT, 0, 0, -2, 0,
                        "Uint32NegativeBaseVertex"},
        IndexedDrawCase{DXGI_FORMAT_R16_UINT, 8, 2, 2, 2,
                        "Uint16OffsetStartPositiveBase"},
        IndexedDrawCase{DXGI_FORMAT_R32_UINT, 8, 2, 2, 2,
                        "Uint32OffsetStartPositiveBase"},
        IndexedDrawCase{DXGI_FORMAT_R16_UINT, 8, 2, -2, 0,
                        "Uint16OffsetStartNegativeBase"},
        IndexedDrawCase{DXGI_FORMAT_R32_UINT, 8, 2, -2, 0,
                        "Uint32OffsetStartNegativeBase"}),
    GraphicsInputAssemblerSpec::Name);

class GraphicsInstancingSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_F(GraphicsInstancingSpec,
       MultipleVertexBuffersAndInstanceStepRateCoverTarget) {
  const auto vertex = CompileShader(R"(
    struct Input {
      float2 position : POSITION;
      float2 offset : OFFSET;
    };
    float4 main(Input input) : SV_Position {
      return float4(input.position + input.offset, 0.0, 1.0);
    })",
                                    "vs_5_0");
  const auto pixel =
      CompileShader("float4 main() : SV_Target { return 1.0.xxxx; }", "ps_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const D3D12_INPUT_ELEMENT_DESC inputs[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0,
       D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root.get();
  desc.VS = {vertex.bytecode->GetBufferPointer(),
             vertex.bytecode->GetBufferSize()};
  desc.PS = {pixel.bytecode->GetBufferPointer(),
             pixel.bytecode->GetBufferSize()};
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.InputLayout = {inputs, 2};
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                &desc, IID_PPV_ARGS(pipeline.put())),
            S_OK);

  constexpr std::array<std::array<float, 2>, 6> vertices = {{
      {-1.0f, -1.0f},
      {-1.0f, 1.0f},
      {0.0f, -1.0f},
      {0.0f, -1.0f},
      {-1.0f, 1.0f},
      {0.0f, 1.0f},
  }};
  constexpr std::array<std::array<float, 2>, 2> offsets = {
      {{0.0f, 0.0f}, {1.0f, 0.0f}}};
  auto vertex_buffer = context_.CreateUploadBuffer(
      sizeof(vertices), vertices.data(), sizeof(vertices));
  auto instance_buffer = context_.CreateUploadBuffer(
      sizeof(offsets), offsets.data(), sizeof(offsets));
  ASSERT_TRUE(vertex_buffer);
  ASSERT_TRUE(instance_buffer);
  const D3D12_VERTEX_BUFFER_VIEW views[] = {
      {vertex_buffer->GetGPUVirtualAddress(), sizeof(vertices),
       sizeof(vertices[0])},
      {instance_buffer->GetGPUVirtualAddress(), sizeof(offsets),
       sizeof(offsets[0])},
  };
  constexpr UINT kSize = 8;
  auto target =
      context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  constexpr FLOAT clear[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->IASetVertexBuffers(0, 2, views);
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, float(kSize), float(kSize), 0, 1};
  const D3D12_RECT scissor = {0, 0, kSize, kSize};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(vertices.size(), offsets.size(), 0, 0);
  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t value = 0;
      std::memcpy(&value,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(value),
                  sizeof(value));
      EXPECT_TRUE(ColorsMatch(value, 0xffffffffu, 0))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(GraphicsInstancingSpec,
       VertexBufferSlotGapAndStepRateTwoAdvanceIndependently) {
  const auto vertex = CompileShader(R"(
    struct Input {
      float2 position : POSITION;
      float2 offset : OFFSET;
    };
    struct Output {
      float4 position : SV_Position;
      nointerpolation float4 color : COLOR;
    };
    Output main(Input input, uint instance_id : SV_InstanceID) {
      Output output;
      output.position = float4(input.position + input.offset, 0.0, 1.0);
      output.color = float4((instance_id >> 1) & 1,
                            instance_id & 1, 0.0, 1.0);
      return output;
    })",
                                    "vs_5_0");
  const auto pixel = CompileShader(R"(
    float4 main(nointerpolation float4 color : COLOR) : SV_Target {
      return color;
    })",
                                   "ps_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const D3D12_INPUT_ELEMENT_DESC inputs[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 2, 0,
       D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 2},
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root.get();
  desc.VS = {vertex.bytecode->GetBufferPointer(),
             vertex.bytecode->GetBufferSize()};
  desc.PS = {pixel.bytecode->GetBufferPointer(),
             pixel.bytecode->GetBufferSize()};
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.InputLayout = {inputs, 2};
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pipeline;
  ASSERT_EQ(context_.device()->CreateGraphicsPipelineState(
                &desc, IID_PPV_ARGS(pipeline.put())),
            S_OK);

  constexpr std::array<std::array<float, 2>, 6> vertices = {{
      {-1.0f, -1.0f},
      {-1.0f, 1.0f},
      {0.0f, -1.0f},
      {0.0f, -1.0f},
      {-1.0f, 1.0f},
      {0.0f, 1.0f},
  }};
  constexpr std::array<std::array<float, 2>, 2> offsets = {
      {{0.0f, 0.0f}, {1.0f, 0.0f}}};
  auto vertex_buffer = context_.CreateUploadBuffer(
      sizeof(vertices), vertices.data(), sizeof(vertices));
  auto instance_buffer = context_.CreateUploadBuffer(
      sizeof(offsets), offsets.data(), sizeof(offsets));
  ASSERT_TRUE(vertex_buffer);
  ASSERT_TRUE(instance_buffer);
  const D3D12_VERTEX_BUFFER_VIEW vertex_view = {
      vertex_buffer->GetGPUVirtualAddress(), sizeof(vertices),
      sizeof(vertices[0])};
  const D3D12_VERTEX_BUFFER_VIEW instance_view = {
      instance_buffer->GetGPUVirtualAddress(), sizeof(offsets),
      sizeof(offsets[0])};

  constexpr UINT kSize = 8;
  auto target =
      context_.CreateTexture2D(kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  constexpr FLOAT clear[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->IASetVertexBuffers(0, 1, &vertex_view);
  context_.list()->IASetVertexBuffers(2, 1, &instance_view);
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, float(kSize), float(kSize), 0, 1};
  const D3D12_RECT scissor = {0, 0, kSize, kSize};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(vertices.size(), 4, 0, 0);
  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t value = 0;
      std::memcpy(&value,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(value),
                  sizeof(value));
      const std::uint32_t expected = x < kSize / 2 ? 0xff00ff00u : 0xff00ffffu;
      EXPECT_TRUE(ColorsMatch(value, expected, 0))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex << value;
    }
  }
}

} // namespace
