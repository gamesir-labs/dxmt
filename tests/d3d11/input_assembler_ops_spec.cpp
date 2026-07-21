#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kTargetWidth = 32;
constexpr UINT kTargetHeight = 16;
constexpr uint32_t kClearColor = 0xff000000u;
constexpr uint32_t kRed = 0xff0000ffu;
constexpr uint32_t kGreen = 0xff00ff00u;

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

constexpr std::string_view kVertexColorShader = R"(
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

constexpr std::string_view kInstancedVertexShader = R"(
struct Input {
  float2 position : POSITION;
  float4 color : COLOR0;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

Output main(Input input, uint instance_id : SV_InstanceID) {
  Output output;
  float2 scale = float2(0.35, 0.8);
  float2 offset = float2(-0.75 + 0.5 * instance_id, 0.0);
  output.position = float4(input.position * scale + offset, 0.0, 1.0);
  output.color = input.color;
  return output;
}
)";

constexpr std::string_view kPixelShader = R"(
float4 main(float4 position : SV_Position, float4 color : COLOR0) : SV_Target {
  return color;
}
)";

struct RenderTarget {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> view;
};

class D3D11InputAssemblerOpsSpec : public ::testing::Test {
protected:
  struct Vertex {
    float position[2];
    float color[4];
  };

  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    const auto pixel = CompileShader(kPixelShader, "ps_5_0");
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    ASSERT_EQ(
        context_.device()->CreatePixelShader(pixel.bytecode->GetBufferPointer(),
                                             pixel.bytecode->GetBufferSize(),
                                             nullptr, pixel_shader_.put()),
        S_OK);

    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = D3D11_FILL_SOLID;
    desc.CullMode = D3D11_CULL_NONE;
    desc.DepthClipEnable = TRUE;
    ASSERT_EQ(
        context_.device()->CreateRasterizerState(&desc, rasterizer_.put()),
        S_OK);
  }

  ComPtr<ID3D11Buffer> CreateBuffer(const void *data, UINT byte_width,
                                    UINT bind_flags) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = byte_width;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = data;
    ComPtr<ID3D11Buffer> buffer;
    EXPECT_EQ(context_.device()->CreateBuffer(&desc, &initial, buffer.put()),
              S_OK);
    return buffer;
  }

  RenderTarget CreateRenderTarget() {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kTargetWidth;
    desc.Height = kTargetHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;

    RenderTarget target;
    EXPECT_EQ(context_.device()->CreateTexture2D(&desc, nullptr,
                                                 target.texture.put()),
              S_OK);
    if (target.texture) {
      EXPECT_EQ(context_.device()->CreateRenderTargetView(
                    target.texture.get(), nullptr, target.view.put()),
                S_OK);
    }
    return target;
  }

  void CreateVertexPipeline(std::string_view source,
                            const D3D11_INPUT_ELEMENT_DESC *elements,
                            UINT element_count,
                            ComPtr<ID3D11VertexShader> *shader,
                            ComPtr<ID3D11InputLayout> *layout) {
    const auto vertex = CompileShader(source, "vs_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    ASSERT_EQ(context_.device()->CreateVertexShader(
                  vertex.bytecode->GetBufferPointer(),
                  vertex.bytecode->GetBufferSize(), nullptr, shader->put()),
              S_OK);
    ASSERT_EQ(context_.device()->CreateInputLayout(
                  elements, element_count, vertex.bytecode->GetBufferPointer(),
                  vertex.bytecode->GetBufferSize(), layout->put()),
              S_OK);
  }

  void BindTarget(const RenderTarget &target) {
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context_.context()->ClearRenderTargetView(target.view.get(), clear);
    ID3D11RenderTargetView *views[] = {target.view.get()};
    context_.context()->OMSetRenderTargets(1, views, nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(kTargetWidth);
    viewport.Height = static_cast<float>(kTargetHeight);
    viewport.MaxDepth = 1.0f;
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->RSSetState(rasterizer_.get());
    context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  }

  uint32_t ReadPixel(const RenderTarget &target, UINT x, UINT y) {
    uint32_t pixel = 0;
    EXPECT_EQ(ReadTexturePixel(context_.device(), context_.context(),
                               target.texture.get(), x, y, &pixel),
              S_OK);
    return pixel;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
};

TEST_F(D3D11InputAssemblerOpsSpec,
       VertexStreamsHonorOffsetsStridesAndStartVertex) {
  const D3D11_INPUT_ELEMENT_DESC elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11InputLayout> input_layout;
  CreateVertexPipeline(kVertexColorShader, elements, ARRAYSIZE(elements),
                       &vertex_shader, &input_layout);
  ASSERT_TRUE(vertex_shader && input_layout);

  struct PositionRecord {
    float position[2];
    uint32_t padding[2];
  };
  struct ColorRecord {
    float color[4];
    uint32_t padding[4];
  };
  constexpr std::array<PositionRecord, 5> positions = {{
      {{2.0f, 2.0f}, {0x3f800000u, 0x3f800000u}},
      {{2.0f, 2.0f}, {0x3f800000u, 0x3f800000u}},
      {{-1.0f, -1.0f}, {0x3f800000u, 0x3f800000u}},
      {{0.0f, 1.0f}, {0x3f800000u, 0x3f800000u}},
      {{1.0f, -1.0f}, {0x3f800000u, 0x3f800000u}},
  }};
  constexpr std::array<ColorRecord, 5> colors = {{
      {{0.0f, 0.0f, 1.0f, 1.0f}, {}},
      {{0.0f, 1.0f, 0.0f, 1.0f}, {}},
      {{1.0f, 0.0f, 0.0f, 1.0f}, {}},
      {{1.0f, 0.0f, 0.0f, 1.0f}, {}},
      {{1.0f, 0.0f, 0.0f, 1.0f}, {}},
  }};
  auto position_buffer = CreateBuffer(positions.data(), sizeof(positions),
                                      D3D11_BIND_VERTEX_BUFFER);
  auto color_buffer =
      CreateBuffer(colors.data(), sizeof(colors), D3D11_BIND_VERTEX_BUFFER);
  ASSERT_TRUE(position_buffer && color_buffer);

  auto target = CreateRenderTarget();
  ASSERT_TRUE(target.texture && target.view);
  BindTarget(target);
  ID3D11Buffer *buffers[] = {position_buffer.get(), color_buffer.get()};
  constexpr UINT strides[] = {sizeof(PositionRecord), sizeof(ColorRecord)};
  constexpr UINT offsets[] = {sizeof(PositionRecord), sizeof(ColorRecord)};
  context_.context()->IASetInputLayout(input_layout.get());
  context_.context()->IASetVertexBuffers(0, ARRAYSIZE(buffers), buffers,
                                         strides, offsets);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->Draw(3, 1);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  const uint32_t center =
      ReadPixel(target, kTargetWidth / 2, kTargetHeight / 2);
  EXPECT_TRUE(ColorMatches(center, kRed, 2))
      << "center pixel was 0x" << std::hex << center;

  ID3D11Buffer *bound = nullptr;
  context_.context()->IAGetVertexBuffers(1, 1, &bound, nullptr, nullptr);
  ASSERT_EQ(bound, color_buffer.get());
  bound->Release();

  ID3D11Buffer *null_buffer[] = {nullptr};
  constexpr UINT null_stride = 37;
  constexpr UINT null_offset = 13;
  context_.context()->IASetVertexBuffers(1, 1, null_buffer, &null_stride,
                                         &null_offset);
  bound = nullptr;
  context_.context()->IAGetVertexBuffers(1, 1, &bound, nullptr, nullptr);
  EXPECT_EQ(bound, nullptr);
  if (bound)
    bound->Release();
}

TEST_F(D3D11InputAssemblerOpsSpec, InstanceStepRateUsesNonZeroStartInstance) {
  const D3D11_INPUT_ELEMENT_DESC elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
       D3D11_INPUT_PER_INSTANCE_DATA, 2},
  };
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11InputLayout> input_layout;
  CreateVertexPipeline(kInstancedVertexShader, elements, ARRAYSIZE(elements),
                       &vertex_shader, &input_layout);
  ASSERT_TRUE(vertex_shader && input_layout);

  constexpr std::array<std::array<float, 2>, 3> positions = {{
      {{-0.5f, -0.5f}},
      {{0.0f, 0.5f}},
      {{0.5f, -0.5f}},
  }};
  constexpr std::array<std::array<float, 4>, 3> instance_colors = {{
      {{0.0f, 0.0f, 1.0f, 1.0f}},
      {{1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.0f, 1.0f, 0.0f, 1.0f}},
  }};
  auto position_buffer = CreateBuffer(positions.data(), sizeof(positions),
                                      D3D11_BIND_VERTEX_BUFFER);
  auto instance_buffer =
      CreateBuffer(instance_colors.data(), sizeof(instance_colors),
                   D3D11_BIND_VERTEX_BUFFER);
  ASSERT_TRUE(position_buffer && instance_buffer);

  auto target = CreateRenderTarget();
  ASSERT_TRUE(target.texture && target.view);
  BindTarget(target);
  ID3D11Buffer *buffers[] = {position_buffer.get(), instance_buffer.get()};
  constexpr UINT strides[] = {sizeof(positions[0]), sizeof(instance_colors[0])};
  constexpr UINT offsets[] = {0, 0};
  context_.context()->IASetInputLayout(input_layout.get());
  context_.context()->IASetVertexBuffers(0, ARRAYSIZE(buffers), buffers,
                                         strides, offsets);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->DrawInstanced(3, 4, 0, 1);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  const std::array expected = {kRed, kRed, kGreen, kGreen};
  for (UINT instance = 0; instance < expected.size(); ++instance) {
    const uint32_t pixel =
        ReadPixel(target, 4 + instance * 8, kTargetHeight / 2);
    EXPECT_TRUE(ColorMatches(pixel, expected[instance], 2))
        << "instance " << instance << " pixel was 0x" << std::hex << pixel;
  }
}

TEST_F(D3D11InputAssemblerOpsSpec,
       Uint32IndicesCombineBufferOffsetStartIndexAndNegativeBaseVertex) {
  const D3D11_INPUT_ELEMENT_DESC elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11InputLayout> input_layout;
  CreateVertexPipeline(kVertexColorShader, elements, ARRAYSIZE(elements),
                       &vertex_shader, &input_layout);
  ASSERT_TRUE(vertex_shader && input_layout);

  constexpr std::array<Vertex, 6> vertices = {{
      {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{2.0f, 2.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
      {{2.0f, 2.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
      {{2.0f, 2.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
  }};
  constexpr std::array<uint32_t, 5> indices = {6, 6, 3, 4, 5};
  auto vertex_buffer =
      CreateBuffer(vertices.data(), sizeof(vertices), D3D11_BIND_VERTEX_BUFFER);
  auto index_buffer =
      CreateBuffer(indices.data(), sizeof(indices), D3D11_BIND_INDEX_BUFFER);
  ASSERT_TRUE(vertex_buffer && index_buffer);

  auto target = CreateRenderTarget();
  ASSERT_TRUE(target.texture && target.view);
  BindTarget(target);
  ID3D11Buffer *buffers[] = {vertex_buffer.get()};
  constexpr UINT stride = sizeof(Vertex);
  constexpr UINT vertex_offset = 0;
  context_.context()->IASetInputLayout(input_layout.get());
  context_.context()->IASetVertexBuffers(0, 1, buffers, &stride,
                                         &vertex_offset);
  context_.context()->IASetIndexBuffer(index_buffer.get(), DXGI_FORMAT_R32_UINT,
                                       sizeof(uint32_t));
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->DrawIndexed(3, 1, -3);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  const uint32_t center =
      ReadPixel(target, kTargetWidth / 2, kTargetHeight / 2);
  EXPECT_TRUE(ColorMatches(center, kRed, 2))
      << "center pixel was 0x" << std::hex << center;
}

struct StripCutCase {
  DXGI_FORMAT format;
  const char *name;
};

class D3D11StripCutSpec : public D3D11InputAssemblerOpsSpec,
                          public ::testing::WithParamInterface<StripCutCase> {};

TEST_P(D3D11StripCutSpec, RestartsTriangleStripAtFixedMaximumIndex) {
  const auto &test = GetParam();
  const D3D11_INPUT_ELEMENT_DESC elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11InputLayout> input_layout;
  CreateVertexPipeline(kVertexColorShader, elements, ARRAYSIZE(elements),
                       &vertex_shader, &input_layout);
  ASSERT_TRUE(vertex_shader && input_layout);

  constexpr std::array<Vertex, 6> vertices = {{
      {{-0.9f, -0.8f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-0.5f, 0.8f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-0.1f, -0.8f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.1f, -0.8f}, {0.0f, 1.0f, 0.0f, 1.0f}},
      {{0.5f, 0.8f}, {0.0f, 1.0f, 0.0f, 1.0f}},
      {{0.9f, -0.8f}, {0.0f, 1.0f, 0.0f, 1.0f}},
  }};
  auto vertex_buffer =
      CreateBuffer(vertices.data(), sizeof(vertices), D3D11_BIND_VERTEX_BUFFER);
  ASSERT_TRUE(vertex_buffer);

  ComPtr<ID3D11Buffer> index_buffer;
  if (test.format == DXGI_FORMAT_R16_UINT) {
    constexpr std::array<uint16_t, 7> indices = {0, 1, 2, 0xffffu, 3, 4, 5};
    index_buffer =
        CreateBuffer(indices.data(), sizeof(indices), D3D11_BIND_INDEX_BUFFER);
  } else {
    constexpr std::array<uint32_t, 7> indices = {0, 1, 2, 0xffffffffu, 3, 4, 5};
    index_buffer =
        CreateBuffer(indices.data(), sizeof(indices), D3D11_BIND_INDEX_BUFFER);
  }
  ASSERT_TRUE(index_buffer);

  auto target = CreateRenderTarget();
  ASSERT_TRUE(target.texture && target.view);
  BindTarget(target);
  ID3D11Buffer *buffers[] = {vertex_buffer.get()};
  constexpr UINT stride = sizeof(Vertex);
  constexpr UINT offset = 0;
  context_.context()->IASetInputLayout(input_layout.get());
  context_.context()->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
  context_.context()->IASetIndexBuffer(index_buffer.get(), test.format, 0);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->DrawIndexed(7, 0, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  const uint32_t left = ReadPixel(target, 8, kTargetHeight / 2);
  const uint32_t center =
      ReadPixel(target, kTargetWidth / 2, kTargetHeight / 2);
  const uint32_t right = ReadPixel(target, 24, kTargetHeight / 2);
  EXPECT_TRUE(ColorMatches(left, kRed, 2))
      << test.name << " left pixel was 0x" << std::hex << left;
  EXPECT_EQ(center, kClearColor)
      << test.name << " center pixel was 0x" << std::hex << center;
  EXPECT_TRUE(ColorMatches(right, kGreen, 2))
      << test.name << " right pixel was 0x" << std::hex << right;
}

constexpr std::array kStripCutCases = {
    StripCutCase{DXGI_FORMAT_R16_UINT, "Uint16"},
    StripCutCase{DXGI_FORMAT_R32_UINT, "Uint32"},
};

INSTANTIATE_TEST_SUITE_P(
    IndexFormats, D3D11StripCutSpec, ::testing::ValuesIn(kStripCutCases),
    [](const ::testing::TestParamInfo<StripCutCase> &info) {
      return info.param.name;
    });

} // namespace
