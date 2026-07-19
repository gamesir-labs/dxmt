#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Public D3D12 input-assembler coverage for the complete indexed-address
// calculation: IBV address + StartIndexLocation + stored index + base vertex.

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct IndexedAddressCase {
  DXGI_FORMAT format;
  UINT view_prefix_indices;
  UINT start_index;
  UINT valid_vertex_start;
  UINT stored_index;
};

std::vector<IndexedAddressCase> BuildIndexedAddressCases() {
  std::vector<IndexedAddressCase> cases;
  const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R32_UINT};
  const UINT view_prefixes[] = {1, 3, 7, 15};
  const UINT start_indices[] = {1, 3, 7, 15};
  const UINT valid_vertex_starts[] = {0, 1, 3, 7, 15, 31, 63, 95};
  const UINT stored_indices[] = {0, 1, 3, 7, 15, 31, 47, 63};

  for (const auto format : formats)
    cases.push_back({format, 1, 1, 0, 0});
  for (const UINT view_prefix : view_prefixes)
    cases.push_back({DXGI_FORMAT_R16_UINT, view_prefix, 1, 0, 1});
  for (const UINT start_index : start_indices)
    cases.push_back({DXGI_FORMAT_R32_UINT, 3, start_index, 1, 3});
  for (const UINT valid_vertex_start : valid_vertex_starts)
    cases.push_back({DXGI_FORMAT_R16_UINT, 7, 3, valid_vertex_start, 7});
  for (const UINT stored_index : stored_indices)
    cases.push_back({DXGI_FORMAT_R32_UINT, 15, 7, 3, stored_index});
  for (UINT index = 0; index < 8; ++index) {
    cases.push_back({formats[index & 1], view_prefixes[index & 3],
                     start_indices[(index + 1) & 3],
                     valid_vertex_starts[index], stored_indices[7 - index]});
  }
  return cases;
}

class GraphicsIndexedAddressMatrixSpec
    : public ::testing::TestWithParam<IndexedAddressCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    const auto vertex = CompileShader(R"(
      struct Input { float2 position : POSITION; };
      float4 main(Input input) : SV_Position {
        return float4(input.position, 0.0, 1.0);
      }
    )",
                                      "vs_5_0");
    const auto pixel = CompileShader(
        "float4 main() : SV_Target { return 1.0.xxxx; }", "ps_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_);

    D3D12_INPUT_ELEMENT_DESC input = {};
    input.SemanticName = "POSITION";
    input.Format = DXGI_FORMAT_R32G32_FLOAT;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_.get();
    desc.VS = {vertex.bytecode->GetBufferPointer(),
               vertex.bytecode->GetBufferSize()};
    desc.PS = {pixel.bytecode->GetBufferPointer(),
               pixel.bytecode->GetBufferSize()};
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
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_P(GraphicsIndexedAddressMatrixSpec,
       ResolvesViewStartStoredIndexAndBaseVertex) {
  const auto &test = GetParam();
  constexpr UINT kVertexCount = 256;
  constexpr UINT kTargetSize = 8;
  constexpr UINT kUnusedIndex = 128;

  std::array<std::array<float, 2>, kVertexCount> vertices = {};
  for (auto &position : vertices)
    position = {2.0f, 2.0f};
  vertices[test.valid_vertex_start + 0] = {-1.0f, -1.0f};
  vertices[test.valid_vertex_start + 1] = {-1.0f, 3.0f};
  vertices[test.valid_vertex_start + 2] = {3.0f, -1.0f};

  const UINT index_size = test.format == DXGI_FORMAT_R16_UINT
                              ? sizeof(std::uint16_t)
                              : sizeof(std::uint32_t);
  const UINT index_count = test.view_prefix_indices + test.start_index + 3;
  std::vector<std::uint8_t> index_bytes(index_count * index_size);
  const auto store_index = [&](UINT position, UINT value) {
    if (test.format == DXGI_FORMAT_R16_UINT) {
      const auto index = static_cast<std::uint16_t>(value);
      std::memcpy(index_bytes.data() + position * index_size, &index,
                  sizeof(index));
    } else {
      const auto index = static_cast<std::uint32_t>(value);
      std::memcpy(index_bytes.data() + position * index_size, &index,
                  sizeof(index));
    }
  };
  for (UINT position = 0; position < index_count; ++position)
    store_index(position, kUnusedIndex);
  for (UINT component = 0; component < 3; ++component) {
    store_index(test.view_prefix_indices + test.start_index + component,
                test.stored_index + component);
  }

  auto vertex_buffer = context_.CreateUploadBuffer(
      sizeof(vertices), vertices.data(), sizeof(vertices));
  auto index_buffer = context_.CreateUploadBuffer(
      index_bytes.size(), index_bytes.data(), index_bytes.size());
  auto target = context_.CreateTexture2D(
      kTargetSize, kTargetSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(vertex_buffer);
  ASSERT_TRUE(index_buffer);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);

  const D3D12_VERTEX_BUFFER_VIEW vertex_view = {
      vertex_buffer->GetGPUVirtualAddress(), sizeof(vertices),
      sizeof(vertices[0])};
  const D3D12_INDEX_BUFFER_VIEW index_view = {
      index_buffer->GetGPUVirtualAddress() +
          test.view_prefix_indices * index_size,
      static_cast<UINT>(index_bytes.size() -
                        test.view_prefix_indices * index_size),
      test.format};
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  constexpr FLOAT clear[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->IASetVertexBuffers(0, 1, &vertex_view);
  context_.list()->IASetIndexBuffer(&index_view);
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, float(kTargetSize), float(kTargetSize),
                                   0, 1};
  const D3D12_RECT scissor = {0, 0, LONG(kTargetSize), LONG(kTargetSize)};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);

  const INT base_vertex = static_cast<INT>(test.valid_vertex_start) -
                          static_cast<INT>(test.stored_index);
  context_.list()->DrawIndexedInstanced(3, 1, test.start_index, base_vertex, 0);
  D3D12TestContext::Transition(context_.list(), target.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  ASSERT_EQ(readback.width, kTargetSize);
  ASSERT_EQ(readback.height, kTargetSize);
  for (UINT y = 0; y < kTargetSize; ++y) {
    for (UINT x = 0; x < kTargetSize; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_TRUE(ColorsMatch(actual, 0xffffffffu, 0))
          << "pixel=" << x << "," << y
          << " format=" << static_cast<UINT>(test.format)
          << " view_prefix=" << test.view_prefix_indices
          << " start=" << test.start_index << " stored=" << test.stored_index
          << " base=" << base_vertex << " vertex=" << test.valid_vertex_start;
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string
IndexedAddressName(const ::testing::TestParamInfo<IndexedAddressCase> &info) {
  const auto &test = info.param;
  return std::string(test.format == DXGI_FORMAT_R16_UINT ? "U16" : "U32") +
         "P" + std::to_string(test.view_prefix_indices) + "S" +
         std::to_string(test.start_index) + "V" +
         std::to_string(test.valid_vertex_start) + "R" +
         std::to_string(test.stored_index);
}

INSTANTIATE_TEST_SUITE_P(AddressMatrix, GraphicsIndexedAddressMatrixSpec,
                         ::testing::ValuesIn(BuildIndexedAddressCases()),
                         IndexedAddressName);

} // namespace
