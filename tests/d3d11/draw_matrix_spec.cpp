#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string>
#include <vector>

// Public D3D11 DrawInstanced / DrawIndexedInstanced parameter matrices.
// Exercises only ID3D11* / DXGI / D3D11CreateDevice / d3dcompiler surfaces.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::string_view kFullscreenVertexShader = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  // Fullscreen oversized triangle for ids 0,1,2; other ids stay offscreen.
  float2 p = float2(-2.0, -2.0);
  if (vertex_id == 0)
    p = float2(-1.0, -1.0);
  else if (vertex_id == 1)
    p = float2(-1.0, 3.0);
  else if (vertex_id == 2)
    p = float2(3.0, -1.0);
  return float4(p, 0.0, 1.0);
}
)";

constexpr std::string_view kSolidPixelShader = R"(
float4 main() : SV_Target {
  return float4(0.25, 0.5, 0.75, 1.0);
}
)";

constexpr std::string_view kVertexColorVertexShader = R"(
struct Input {
  float2 position : POSITION;
  float4 color : COLOR0;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};
Output main(Input input) {
  Output output;
  output.position = float4(input.position, 0.0, 1.0);
  output.color = input.color;
  return output;
}
)";

constexpr std::string_view kVertexColorPixelShader = R"(
struct Input {
  float4 position : SV_Position;
  float4 color : COLOR0;
};
float4 main(Input input) : SV_Target {
  return input.color;
}
)";

// R8G8B8A8 little-endian: 0xAABBGGRR
constexpr uint32_t kSolidColor = 0xffbf8040;
constexpr uint32_t kClearColor = 0x00000000;
constexpr uint32_t kRed = 0xff0000ff;
constexpr uint32_t kGreen = 0xff00ff00;
constexpr uint32_t kBlue = 0xffff0000;

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

bool ColorMatches(uint32_t actual, uint32_t expected, unsigned tolerance) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    const int actual_channel = (actual >> shift) & 0xff;
    const int expected_channel = (expected >> shift) & 0xff;
    if (std::abs(actual_channel - expected_channel) >
        static_cast<int>(tolerance))
      return false;
  }
  return true;
}

HRESULT ReadTexturePixel(ID3D11Device *device, ID3D11DeviceContext *context,
                         ID3D11Texture2D *texture, UINT x, UINT y,
                         uint32_t *pixel) {
  if (!device || !context || !texture || !pixel)
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  if (x >= desc.Width || y >= desc.Height)
    return E_INVALIDARG;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, staging.put());
  if (FAILED(hr))
    return hr;
  context->CopyResource(staging.get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;
  std::memcpy(pixel,
              static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch +
                  x * sizeof(*pixel),
              sizeof(*pixel));
  context->Unmap(staging.get(), 0);
  return S_OK;
}

HRESULT CreateRenderTarget(ID3D11Device *device, UINT width, UINT height,
                           ID3D11Texture2D **texture,
                           ID3D11RenderTargetView **rtv) {
  if (!device || !texture || !rtv)
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, texture);
  if (FAILED(hr))
    return hr;
  return device->CreateRenderTargetView(*texture, nullptr, rtv);
}

// ---------------------------------------------------------------------------
// DrawInstanced: vertex_count × instance_count matrix
// ---------------------------------------------------------------------------

struct DrawInstancedCase {
  UINT vertex_count;
  UINT instance_count;
  UINT start_vertex;
  UINT start_instance;
};

std::vector<DrawInstancedCase> BuildDrawInstancedCases() {
  std::vector<DrawInstancedCase> cases;
  // Fullscreen triangle uses SV_VertexID 0..2. Include 0/1/3/many.
  const UINT vertices[] = {0, 1, 2, 3, 4, 33, 512};
  const UINT instances[] = {0, 1, 2, 3, 4, 15, 16, 64};
  for (const UINT vertex_count : vertices)
    cases.push_back({vertex_count, 1, 0, 0});
  for (const UINT instance_count : instances) {
    if (instance_count == 1)
      continue; // {3,1} already added in the vertex sweep
    cases.push_back({3, instance_count, 0, 0});
  }
  // Cross product of a smaller meaningful subset (0/1/3/many).
  for (const UINT vertex_count : {0u, 1u, 3u, 33u}) {
    for (const UINT instance_count : {0u, 1u, 3u, 16u}) {
      // Skip cells already covered by the sweeps above.
      if (instance_count == 1)
        continue;
      if (vertex_count == 3)
        continue;
      cases.push_back({vertex_count, instance_count, 0, 0});
    }
  }
  // start_instance is exercised; start_vertex stays 0 because the VS uses
  // SV_VertexID 0..2 for the fullscreen triangle (relative vertex indices).
  for (const UINT vertex_count : {3u, 33u, 512u})
    cases.push_back({vertex_count, 1, 0, 1});
  return cases;
}

class DrawInstancedMatrixSpec
    : public ::testing::TestWithParam<DrawInstancedCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kSolidPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();

    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
};

TEST_P(DrawInstancedMatrixSpec,
       DrawInstancedCoversWhenFullscreenTrianglePresent) {
  const auto &test = GetParam();
  constexpr UINT kSize = 16;

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kSize, kSize, target.put(), target_view.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(kSize);
  viewport.Height = static_cast<float>(kSize);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *targets[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->DrawInstanced(test.vertex_count, test.instance_count,
                                    test.start_vertex, test.start_instance);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t center = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), kSize / 2,
      kSize / 2, &center)));

  // Coverage only when the draw includes SV_VertexID 0..2 and >=1 instance.
  const bool covers = test.instance_count >= 1 && test.vertex_count >= 3 &&
                      test.start_vertex == 0;
  if (covers) {
    EXPECT_TRUE(ColorMatches(center, kSolidColor, 2))
        << "center was 0x" << std::hex << center
        << " vertex_count=" << std::dec << test.vertex_count
        << " instance_count=" << test.instance_count
        << " start_vertex=" << test.start_vertex;
  } else {
    EXPECT_EQ(center, kClearColor)
        << "center was 0x" << std::hex << center
        << " vertex_count=" << std::dec << test.vertex_count
        << " instance_count=" << test.instance_count
        << " start_vertex=" << test.start_vertex
        << " (expected clear when draw has no coverage)";
  }
}

std::string
DrawInstancedName(const ::testing::TestParamInfo<DrawInstancedCase> &info) {
  return "V" + std::to_string(info.param.vertex_count) + "I" +
         std::to_string(info.param.instance_count) + "SV" +
         std::to_string(info.param.start_vertex) + "SI" +
         std::to_string(info.param.start_instance) + "N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(CountMatrix, DrawInstancedMatrixSpec,
                         ::testing::ValuesIn(BuildDrawInstancedCases()),
                         DrawInstancedName);

// ---------------------------------------------------------------------------
// DrawIndexedInstanced: start_index × base_vertex matrix
// ---------------------------------------------------------------------------

struct IndexedAddressCase {
  UINT start_index;
  INT base_vertex;
  uint32_t expected_center;
};

std::vector<IndexedAddressCase> BuildIndexedAddressCases() {
  // Vertex layout (POSITION + COLOR):
  //   verts 0..2 : red triangle covering the RT center
  //   verts 3..5 : green triangle covering the RT center
  //   verts 6..8 : blue triangle covering the RT center
  //   verts 9+   : offscreen (no coverage)
  // Index buffer is three copies of {0,1,2} so start_index selects the pack
  // and base_vertex selects the color triangle (0=red, 3=green, 6=blue).
  return {
      // start_index × base_vertex that hit red
      {0, 0, kRed},
      {3, 0, kRed},
      {6, 0, kRed},
      // green
      {0, 3, kGreen},
      {3, 3, kGreen},
      {6, 3, kGreen},
      // blue
      {0, 6, kBlue},
      {3, 6, kBlue},
      {6, 6, kBlue},
      // miss: relative indices land on offscreen fillers
      {0, 9, kClearColor},
      {3, 9, kClearColor},
      {0, 12, kClearColor},
  };
}

class DrawIndexedInstancedMatrixSpec
    : public ::testing::TestWithParam<IndexedAddressCase> {
protected:
  struct Vertex {
    float position[2];
    float color[4];
  };

  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vertex = CompileShader(kVertexColorVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kVertexColorPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();

    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader_.put())));

    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateInputLayout(
        elements, ARRAYSIZE(elements), vertex.bytecode->GetBufferPointer(),
        vertex.bytecode->GetBufferSize(), input_layout_.put())));
  }

  static Vertex MakeVertex(float x, float y, float r, float g, float b) {
    return {{x, y}, {r, g, b, 1.0f}};
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11InputLayout> input_layout_;
};

TEST_P(DrawIndexedInstancedMatrixSpec,
       StartIndexAndBaseVertexSelectTriangleColor) {
  const auto &test = GetParam();
  constexpr UINT kSize = 16;

  // Three on-screen triangles (red/green/blue) + offscreen fillers.
  const std::array<Vertex, 15> vertices = {
      MakeVertex(-1.0f, -1.0f, 1.0f, 0.0f, 0.0f),
      MakeVertex(0.0f, 1.0f, 1.0f, 0.0f, 0.0f),
      MakeVertex(1.0f, -1.0f, 1.0f, 0.0f, 0.0f),
      MakeVertex(-1.0f, -1.0f, 0.0f, 1.0f, 0.0f),
      MakeVertex(0.0f, 1.0f, 0.0f, 1.0f, 0.0f),
      MakeVertex(1.0f, -1.0f, 0.0f, 1.0f, 0.0f),
      MakeVertex(-1.0f, -1.0f, 0.0f, 0.0f, 1.0f),
      MakeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f),
      MakeVertex(1.0f, -1.0f, 0.0f, 0.0f, 1.0f),
      MakeVertex(2.0f, 2.0f, 1.0f, 1.0f, 1.0f),
      MakeVertex(2.0f, 2.0f, 1.0f, 1.0f, 1.0f),
      MakeVertex(2.0f, 2.0f, 1.0f, 1.0f, 1.0f),
      MakeVertex(2.0f, 2.0f, 1.0f, 1.0f, 1.0f),
      MakeVertex(2.0f, 2.0f, 1.0f, 1.0f, 1.0f),
      MakeVertex(2.0f, 2.0f, 1.0f, 1.0f, 1.0f),
  };
  // Three identical relative packs {0,1,2} so start_index ∈ {0,3,6}.
  constexpr std::array<uint16_t, 9> indices = {0, 1, 2, 0, 1, 2, 0, 1, 2};

  D3D11_BUFFER_DESC vertex_desc = {};
  vertex_desc.ByteWidth = sizeof(vertices);
  vertex_desc.Usage = D3D11_USAGE_DEFAULT;
  vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA vertex_initial = {};
  vertex_initial.pSysMem = vertices.data();
  ComPtr<ID3D11Buffer> vertex_buffer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &vertex_desc, &vertex_initial, vertex_buffer.put())));

  D3D11_BUFFER_DESC index_desc = {};
  index_desc.ByteWidth = sizeof(indices);
  index_desc.Usage = D3D11_USAGE_DEFAULT;
  index_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  D3D11_SUBRESOURCE_DATA index_initial = {};
  index_initial.pSysMem = indices.data();
  ComPtr<ID3D11Buffer> index_buffer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &index_desc, &index_initial, index_buffer.put())));

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kSize, kSize, target.put(), target_view.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(kSize);
  viewport.Height = static_cast<float>(kSize);
  viewport.MaxDepth = 1.0f;
  UINT stride = sizeof(Vertex);
  UINT offset = 0;
  ID3D11Buffer *vertex_buffers[] = {vertex_buffer.get()};
  ID3D11RenderTargetView *targets[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetInputLayout(input_layout_.get());
  context_.context()->IASetVertexBuffers(0, 1, vertex_buffers, &stride,
                                         &offset);
  context_.context()->IASetIndexBuffer(index_buffer.get(), DXGI_FORMAT_R16_UINT,
                                       0);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->DrawIndexedInstanced(3, 1, test.start_index,
                                           test.base_vertex, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t center = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), kSize / 2,
      kSize / 2, &center)));
  if (test.expected_center == kClearColor) {
    EXPECT_EQ(center, kClearColor)
        << "center was 0x" << std::hex << center
        << " start_index=" << std::dec << test.start_index
        << " base_vertex=" << test.base_vertex;
  } else {
    EXPECT_TRUE(ColorMatches(center, test.expected_center, 2))
        << "center was 0x" << std::hex << center << " expected 0x"
        << test.expected_center << " start_index=" << std::dec
        << test.start_index << " base_vertex=" << test.base_vertex;
  }
}

std::string IndexedAddressName(
    const ::testing::TestParamInfo<IndexedAddressCase> &info) {
  const char *color = "Clear";
  if (info.param.expected_center == kRed)
    color = "Red";
  else if (info.param.expected_center == kGreen)
    color = "Green";
  else if (info.param.expected_center == kBlue)
    color = "Blue";
  return "S" + std::to_string(info.param.start_index) + "B" +
         std::to_string(info.param.base_vertex) + color + "N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(AddressMatrix, DrawIndexedInstancedMatrixSpec,
                         ::testing::ValuesIn(BuildIndexedAddressCases()),
                         IndexedAddressName);

// pipeline_spec pattern as TEST_P: padded index buffer where start_index skips
// junk entries and base_vertex remaps the relative indices onto a green
// covering triangle (see D3D11PipelineSpec.DrawsIndexedWithStartIndexAndBaseVertex).
struct PadPrefixCase {
  UINT start_index;
  INT base_vertex;
  uint32_t expected_center;
};

std::vector<PadPrefixCase> BuildPadPrefixCases() {
  // IB layout built in the test: [0,0,...,0, 0,1,2] with pad_count junk zeros
  // then the relative triangle pack. start_index is the draw parameter.
  //
  // Success: start_index == pad_count, base_vertex == 1 → verts 1,2,3 green.
  // Miss: start_index into the zero pad with base 0 → three copies of offscreen
  // vertex 0 → clear center.
  return {
      {0, 1, kGreen},  // no pad
      {1, 1, kGreen},  // classic pipeline_spec-style start_index=1
      {2, 1, kGreen},
      {3, 1, kGreen},
      {7, 1, kGreen},
      {0, 0, kClearColor}, // degenerate/offscreen at vertex 0
      {1, 0, kClearColor}, // still inside zero pad when pad_count>=7 below
      {3, 0, kClearColor},
  };
}

class PaddedIndexedMatrixSpec
    : public ::testing::TestWithParam<PadPrefixCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
  }

  D3D11TestContext context_;
};

TEST_P(PaddedIndexedMatrixSpec, DrawsIndexedWithStartIndexAndBaseVertex) {
  const auto &test = GetParam();
  constexpr UINT kSize = 16;
  // Enough zero pad that miss cases with start_index <= 3 stay inside zeros.
  constexpr UINT kPadCount = 7;

  const auto vertex = CompileShader(kVertexColorVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kVertexColorPixelShader, "ps_5_0");
  ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
  ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();

  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
      vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
      nullptr, vertex_shader.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
      pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
      nullptr, pixel_shader.put())));

  const D3D11_INPUT_ELEMENT_DESC elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  ComPtr<ID3D11InputLayout> input_layout;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateInputLayout(
      elements, ARRAYSIZE(elements), vertex.bytecode->GetBufferPointer(),
      vertex.bytecode->GetBufferSize(), input_layout.put())));

  struct Vertex {
    float position[2];
    float color[4];
  };
  // Matches pipeline_spec DrawsIndexedWithStartIndexAndBaseVertex layout.
  constexpr std::array<Vertex, 4> vertices = {
      {{{2.0f, 2.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
       {{-1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
       {{0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
       {{1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}}}};

  // For success cases start_index is the pad length before {0,1,2}.
  // For miss cases start_index stays inside the leading zeros.
  const UINT pack_offset =
      test.expected_center == kClearColor ? kPadCount : test.start_index;
  std::vector<uint16_t> indices(pack_offset + 3, 0);
  indices[pack_offset + 0] = 0;
  indices[pack_offset + 1] = 1;
  indices[pack_offset + 2] = 2;

  D3D11_BUFFER_DESC vertex_desc = {};
  vertex_desc.ByteWidth = sizeof(vertices);
  vertex_desc.Usage = D3D11_USAGE_DEFAULT;
  vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA vertex_initial = {};
  vertex_initial.pSysMem = vertices.data();
  ComPtr<ID3D11Buffer> vertex_buffer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &vertex_desc, &vertex_initial, vertex_buffer.put())));

  D3D11_BUFFER_DESC index_desc = {};
  index_desc.ByteWidth =
      static_cast<UINT>(indices.size() * sizeof(indices[0]));
  index_desc.Usage = D3D11_USAGE_DEFAULT;
  index_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  D3D11_SUBRESOURCE_DATA index_initial = {};
  index_initial.pSysMem = indices.data();
  ComPtr<ID3D11Buffer> index_buffer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &index_desc, &index_initial, index_buffer.put())));

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kSize, kSize, target.put(), target_view.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(kSize);
  viewport.Height = static_cast<float>(kSize);
  viewport.MaxDepth = 1.0f;
  UINT stride = sizeof(Vertex);
  UINT offset = 0;
  ID3D11Buffer *vertex_buffers[] = {vertex_buffer.get()};
  ID3D11RenderTargetView *targets[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetInputLayout(input_layout.get());
  context_.context()->IASetVertexBuffers(0, 1, vertex_buffers, &stride,
                                         &offset);
  context_.context()->IASetIndexBuffer(index_buffer.get(), DXGI_FORMAT_R16_UINT,
                                       0);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->DrawIndexedInstanced(3, 1, test.start_index,
                                           test.base_vertex, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t center = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), kSize / 2,
      kSize / 2, &center)));
  if (test.expected_center == kClearColor) {
    EXPECT_EQ(center, kClearColor)
        << "center was 0x" << std::hex << center
        << " start_index=" << std::dec << test.start_index
        << " base_vertex=" << test.base_vertex;
  } else {
    EXPECT_TRUE(ColorMatches(center, test.expected_center, 2))
        << "center was 0x" << std::hex << center << " expected 0x"
        << test.expected_center << " start_index=" << std::dec
        << test.start_index << " base_vertex=" << test.base_vertex;
  }
}

std::string
PaddedIndexedName(const ::testing::TestParamInfo<PadPrefixCase> &info) {
  return "S" + std::to_string(info.param.start_index) + "B" +
         std::to_string(info.param.base_vertex) + "N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(PadMatrix, PaddedIndexedMatrixSpec,
                         ::testing::ValuesIn(BuildPadPrefixCases()),
                         PaddedIndexedName);

// ---------------------------------------------------------------------------
// Scissor interaction with a fullscreen draw
// ---------------------------------------------------------------------------

struct ScissorCase {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
};

std::vector<ScissorCase> BuildScissorCases() {
  return {
      {0, 0, 16, 16},
      {0, 0, 8, 16},
      {8, 0, 16, 16},
      {0, 0, 16, 8},
      {0, 8, 16, 16},
      {4, 4, 12, 12},
      {0, 0, 1, 16},
      {15, 0, 16, 16},
  };
}

class DrawScissorMatrixSpec : public ::testing::TestWithParam<ScissorCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kSolidPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();

    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader_.put())));

    D3D11_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    rasterizer_desc.ScissorEnable = TRUE;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
        &rasterizer_desc, rasterizer_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
};

TEST_P(DrawScissorMatrixSpec, RestrictsFullscreenDrawToScissorRect) {
  const auto &test = GetParam();
  constexpr UINT kSize = 16;

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kSize, kSize, target.put(), target_view.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(kSize);
  viewport.Height = static_cast<float>(kSize);
  viewport.MaxDepth = 1.0f;
  D3D11_RECT scissor = {test.left, test.top, test.right, test.bottom};
  ID3D11RenderTargetView *targets[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->RSSetScissorRects(1, &scissor);
  context_.context()->RSSetState(rasterizer_.get());
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->DrawInstanced(3, 1, 0, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  const int cx = static_cast<int>((test.left + test.right) / 2);
  const int cy = static_cast<int>((test.top + test.bottom) / 2);

  if (test.right > test.left && test.bottom > test.top && cx >= 0 && cy >= 0 &&
      cx < static_cast<int>(kSize) && cy < static_cast<int>(kSize)) {
    uint32_t inside = 0;
    ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
        context_.device(), context_.context(), target.get(),
        static_cast<UINT>(cx), static_cast<UINT>(cy), &inside)));
    EXPECT_TRUE(ColorMatches(inside, kSolidColor, 2))
        << "inside (" << cx << "," << cy << ") was 0x" << std::hex << inside;
  }

  if (test.left > 0) {
    uint32_t outside = 0xffffffffu;
    const int ox = static_cast<int>(test.left) - 1;
    const int oy = cy < 0 ? 0 : (cy >= static_cast<int>(kSize)
                                     ? static_cast<int>(kSize) - 1
                                     : cy);
    ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
        context_.device(), context_.context(), target.get(),
        static_cast<UINT>(ox), static_cast<UINT>(oy), &outside)));
    EXPECT_EQ(outside, kClearColor)
        << "left-outside (" << ox << "," << oy << ") was 0x" << std::hex
        << outside;
  }
  if (test.top > 0) {
    uint32_t outside = 0xffffffffu;
    const int ox = cx < 0 ? 0 : (cx >= static_cast<int>(kSize)
                                     ? static_cast<int>(kSize) - 1
                                     : cx);
    const int oy = static_cast<int>(test.top) - 1;
    ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
        context_.device(), context_.context(), target.get(),
        static_cast<UINT>(ox), static_cast<UINT>(oy), &outside)));
    EXPECT_EQ(outside, kClearColor)
        << "top-outside (" << ox << "," << oy << ") was 0x" << std::hex
        << outside;
  }
  if (test.right < static_cast<LONG>(kSize)) {
    uint32_t outside = 0xffffffffu;
    const int ox = static_cast<int>(test.right);
    const int oy = cy < 0 ? 0 : (cy >= static_cast<int>(kSize)
                                     ? static_cast<int>(kSize) - 1
                                     : cy);
    if (ox >= 0 && ox < static_cast<int>(kSize)) {
      ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
          context_.device(), context_.context(), target.get(),
          static_cast<UINT>(ox), static_cast<UINT>(oy), &outside)));
      EXPECT_EQ(outside, kClearColor)
          << "right-outside (" << ox << "," << oy << ") was 0x" << std::hex
          << outside;
    }
  }
}

std::string ScissorName(const ::testing::TestParamInfo<ScissorCase> &info) {
  return "L" + std::to_string(info.param.left) + "T" +
         std::to_string(info.param.top) + "R" +
         std::to_string(info.param.right) + "B" +
         std::to_string(info.param.bottom);
}

INSTANTIATE_TEST_SUITE_P(ScissorMatrix, DrawScissorMatrixSpec,
                         ::testing::ValuesIn(BuildScissorCases()), ScissorName);

} // namespace
