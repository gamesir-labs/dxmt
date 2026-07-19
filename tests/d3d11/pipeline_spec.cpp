#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::string_view kFullscreenVertexShader = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
  return float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)";

constexpr std::string_view kSolidPixelShader = R"(
float4 main() : SV_Target {
  return float4(0.25, 0.5, 0.75, 1.0);
}
)";

constexpr std::string_view kTextureComputeShader = R"(
RWTexture2D<uint> output : register(u0);

[numthreads(4, 4, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
  output[thread_id.xy] = thread_id.y * 4 + thread_id.x + 7;
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

constexpr std::string_view kInstancedVertexShader = R"(
struct Input {
  float2 position : POSITION;
  float2 offset : OFFSET;
  float4 color : COLOR0;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};
Output main(Input input) {
  Output output;
  output.position = float4(input.position + input.offset, 0.0, 1.0);
  output.color = input.color;
  return output;
}
)";

constexpr std::string_view kTranslucentRedPixelShader = R"(
float4 main() : SV_Target {
  return float4(1.0, 0.0, 0.0, 0.25);
}
)";

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

class D3D11PipelineSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
  }

  D3D11TestContext context_;
};

TEST_F(D3D11PipelineSpec, DrawsFullscreenTriangleIntoRenderTarget) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kSolidPixelShader, "ps_5_0");
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

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 16;
  target_desc.Height = 16;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(target_desc.Width);
  viewport.Height = static_cast<float>(target_desc.Height);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *target_views[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, target_views, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), target.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  constexpr uint32_t expected = 0xffbf8040;
  for (UINT y = 0; y < target_desc.Height; ++y) {
    for (UINT x = 0; x < target_desc.Width; ++x) {
      uint32_t actual = 0;
      std::memcpy(&actual,
                  static_cast<const uint8_t *>(mapped.pData) +
                      y * mapped.RowPitch + x * sizeof(actual),
                  sizeof(actual));
      ASSERT_TRUE(ColorMatches(actual, expected, 2))
          << "pixel (" << x << ", " << y << ") was 0x" << std::hex << actual;
    }
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11PipelineSpec, DrawsWithInputLayoutAndVertexBuffer) {
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
  constexpr std::array<Vertex, 3> vertices = {
      {{{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
       {{0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
       {{1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}}};
  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = sizeof(vertices);
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = vertices.data();
  ComPtr<ID3D11Buffer> vertex_buffer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &buffer_desc, &initial, vertex_buffer.put())));

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 16;
  target_desc.Height = 16;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D11_VIEWPORT viewport = {};
  viewport.Width = 16.0f;
  viewport.Height = 16.0f;
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
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t center = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), 8, 8, &center)));
  EXPECT_TRUE(ColorMatches(center, 0xff0000ff, 2))
      << "center pixel was 0x" << std::hex << center;
}

TEST_F(D3D11PipelineSpec, DrawsIndexedWithStartIndexAndBaseVertex) {
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
  constexpr std::array<Vertex, 4> vertices = {
      {{{2.0f, 2.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
       {{-1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
       {{0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
       {{1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}}}};
  constexpr std::array<uint16_t, 4> indices = {0xffff, 0, 1, 2};

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

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 16;
  target_desc.Height = 16;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(target_desc.Width);
  viewport.Height = static_cast<float>(target_desc.Height);
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
  context_.context()->DrawIndexed(3, 1, 1);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t center = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), 8, 8, &center)));
  EXPECT_TRUE(ColorMatches(center, 0xff00ff00, 2))
      << "center pixel was 0x" << std::hex << center;
}

TEST_F(D3D11PipelineSpec, AppliesScissorRectangleDuringRasterization) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kSolidPixelShader, "ps_5_0");
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

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 16;
  target_desc.Height = 16;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D11_RASTERIZER_DESC rasterizer_desc = {};
  rasterizer_desc.FillMode = D3D11_FILL_SOLID;
  rasterizer_desc.CullMode = D3D11_CULL_NONE;
  rasterizer_desc.DepthClipEnable = TRUE;
  rasterizer_desc.ScissorEnable = TRUE;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
      &rasterizer_desc, rasterizer.put())));

  constexpr float clear_color[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear_color);
  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(target_desc.Width);
  viewport.Height = static_cast<float>(target_desc.Height);
  viewport.MaxDepth = 1.0f;
  D3D11_RECT scissor = {0, 0, 8, 16};
  ID3D11RenderTargetView *targets[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->RSSetScissorRects(1, &scissor);
  context_.context()->RSSetState(rasterizer.get());
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t inside = 0;
  uint32_t outside = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), 4, 8, &inside)));
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), 12, 8, &outside)));
  EXPECT_TRUE(ColorMatches(inside, 0xffbf8040, 2))
      << "inside pixel was 0x" << std::hex << inside;
  EXPECT_EQ(outside, 0u) << "outside pixel was 0x" << std::hex << outside;
}

TEST_F(D3D11PipelineSpec, AdvancesPerInstanceInputData) {
  const auto vertex = CompileShader(kInstancedVertexShader, "vs_5_0");
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
      {"OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0,
       D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 8,
       D3D11_INPUT_PER_INSTANCE_DATA, 1},
  };
  ComPtr<ID3D11InputLayout> input_layout;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateInputLayout(
      elements, ARRAYSIZE(elements), vertex.bytecode->GetBufferPointer(),
      vertex.bytecode->GetBufferSize(), input_layout.put())));

  constexpr std::array<std::array<float, 2>, 3> vertices = {
      std::array{-0.5f, -1.0f}, std::array{0.0f, 1.0f},
      std::array{0.5f, -1.0f}};
  struct Instance {
    float offset[2];
    float color[4];
  };
  constexpr std::array<Instance, 2> instances = {
      {{{-0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
       {{0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}}}};

  D3D11_BUFFER_DESC vertex_desc = {};
  vertex_desc.ByteWidth = sizeof(vertices);
  vertex_desc.Usage = D3D11_USAGE_DEFAULT;
  vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA vertex_initial = {};
  vertex_initial.pSysMem = vertices.data();
  ComPtr<ID3D11Buffer> vertex_buffer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &vertex_desc, &vertex_initial, vertex_buffer.put())));

  D3D11_BUFFER_DESC instance_desc = {};
  instance_desc.ByteWidth = sizeof(instances);
  instance_desc.Usage = D3D11_USAGE_DEFAULT;
  instance_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA instance_initial = {};
  instance_initial.pSysMem = instances.data();
  ComPtr<ID3D11Buffer> instance_buffer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &instance_desc, &instance_initial, instance_buffer.put())));

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 16;
  target_desc.Height = 16;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(target_desc.Width);
  viewport.Height = static_cast<float>(target_desc.Height);
  viewport.MaxDepth = 1.0f;
  ID3D11Buffer *vertex_buffers[] = {vertex_buffer.get(), instance_buffer.get()};
  const UINT strides[] = {sizeof(vertices[0]), sizeof(Instance)};
  constexpr UINT offsets[] = {0, 0};
  ID3D11RenderTargetView *targets[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetInputLayout(input_layout.get());
  context_.context()->IASetVertexBuffers(0, ARRAYSIZE(vertex_buffers),
                                         vertex_buffers, strides, offsets);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->DrawInstanced(3, 2, 0, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t left = 0;
  uint32_t right = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), 4, 8, &left)));
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), 12, 8, &right)));
  EXPECT_TRUE(ColorMatches(left, 0xff0000ff, 2))
      << "left pixel was 0x" << std::hex << left;
  EXPECT_TRUE(ColorMatches(right, 0xff00ff00, 2))
      << "right pixel was 0x" << std::hex << right;
}

TEST_F(D3D11PipelineSpec, BlendsSourceAndDestinationColors) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kTranslucentRedPixelShader, "ps_5_0");
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

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 16;
  target_desc.Height = 16;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.RenderTarget[0].BlendEnable = TRUE;
  blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D11BlendState> blend;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBlendState(&blend_desc, blend.put())));

  constexpr float clear_color[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  context_.context()->ClearRenderTargetView(target_view.get(), clear_color);
  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(target_desc.Width);
  viewport.Height = static_cast<float>(target_desc.Height);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *targets[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->OMSetBlendState(blend.get(), nullptr, 0xffffffff);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t center = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), 8, 8, &center)));
  EXPECT_TRUE(ColorMatches(center, 0x40bf0040, 2))
      << "center pixel was 0x" << std::hex << center;
}

TEST_F(D3D11PipelineSpec, ReportsSamplesForAnOcclusionQuery) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kSolidPixelShader, "ps_5_0");
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

  D3D11_TEXTURE2D_DESC target_desc = {};
  target_desc.Width = 8;
  target_desc.Height = 8;
  target_desc.MipLevels = 1;
  target_desc.ArraySize = 1;
  target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.SampleDesc.Count = 1;
  target_desc.Usage = D3D11_USAGE_DEFAULT;
  target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&target_desc, nullptr, target.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      target.get(), nullptr, target_view.put())));

  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_OCCLUSION;
  ComPtr<ID3D11Query> query;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateQuery(&query_desc, query.put())));
  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(target_desc.Width);
  viewport.Height = static_cast<float>(target_desc.Height);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *target_views[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, target_views, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->Begin(query.get());
  context_.context()->Draw(3, 0);
  context_.context()->End(query.get());
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
  context_.context()->Flush();

  UINT64 samples = 0;
  HRESULT data_hr = S_FALSE;
  for (UINT attempt = 0; attempt < 100 && data_hr == S_FALSE; ++attempt) {
    data_hr =
        context_.context()->GetData(query.get(), &samples, sizeof(samples), 0);
    if (data_hr == S_FALSE)
      Sleep(1);
  }
  EXPECT_EQ(data_hr, S_OK);
  EXPECT_GT(samples, 0u);
}

TEST_F(D3D11PipelineSpec, ReadsCompletedTimestampQuery) {
  D3D11_QUERY_DESC query_desc = {};
  query_desc.Query = D3D11_QUERY_TIMESTAMP;
  ComPtr<ID3D11Query> query;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateQuery(&query_desc, query.put())));
  context_.context()->End(query.get());
  context_.context()->Flush();

  UINT64 timestamp = 0;
  HRESULT data_hr = S_FALSE;
  for (UINT attempt = 0; attempt < 100 && data_hr == S_FALSE; ++attempt) {
    data_hr = context_.context()->GetData(query.get(), &timestamp,
                                          sizeof(timestamp), 0);
    if (data_hr == S_FALSE)
      Sleep(1);
  }
  EXPECT_EQ(data_hr, S_OK);
  EXPECT_GT(timestamp, 0ull);
}

TEST_F(D3D11PipelineSpec, DispatchesComputeShaderIntoStorageTexture) {
  const auto compute = CompileShader(kTextureComputeShader, "cs_5_0");
  ASSERT_TRUE(HResultSucceeded(compute.result)) << compute.diagnostic_text();
  ComPtr<ID3D11ComputeShader> compute_shader;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateComputeShader(
      compute.bytecode->GetBufferPointer(), compute.bytecode->GetBufferSize(),
      nullptr, compute_shader.put())));

  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 4;
  texture_desc.Height = 4;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R32_UINT;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));

  D3D11_UNORDERED_ACCESS_VIEW_DESC view_desc = {};
  view_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
  view_desc.Format = texture_desc.Format;
  ComPtr<ID3D11UnorderedAccessView> view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateUnorderedAccessView(
      texture.get(), &view_desc, view.put())));
  ID3D11UnorderedAccessView *views[] = {view.get()};
  context_.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, views, nullptr);
  context_.context()->Dispatch(1, 1, 1);
  context_.context()->CSSetUnorderedAccessViews(0, 0, nullptr, nullptr);

  D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  for (UINT y = 0; y < texture_desc.Height; ++y) {
    const auto *row = reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch);
    for (UINT x = 0; x < texture_desc.Width; ++x)
      EXPECT_EQ(row[x], y * texture_desc.Width + x + 7)
          << "texel (" << x << ", " << y << ')';
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11PipelineSpec, ExecutesDeferredRenderTargetClear) {
  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 8;
  texture_desc.Height = 8;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      texture.get(), nullptr, view.put())));

  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDeferredContext(0, deferred.put())));
  constexpr float clear_color[4] = {0.125f, 0.25f, 0.5f, 1.0f};
  deferred->ClearRenderTargetView(view.get(), clear_color);
  ComPtr<ID3D11CommandList> command_list;
  ASSERT_TRUE(
      HResultSucceeded(deferred->FinishCommandList(FALSE, command_list.put())));
  context_.context()->ExecuteCommandList(command_list.get(), FALSE);

  D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  constexpr uint32_t expected = 0xff804020;
  for (UINT y = 0; y < texture_desc.Height; ++y) {
    const auto *row = reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch);
    for (UINT x = 0; x < texture_desc.Width; ++x)
      EXPECT_TRUE(ColorMatches(row[x], expected, 1));
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11PipelineSpec, CreatesStateObjectsWithRequestedDescriptions) {
  D3D11_RASTERIZER_DESC rasterizer_desc = {};
  rasterizer_desc.FillMode = D3D11_FILL_WIREFRAME;
  rasterizer_desc.CullMode = D3D11_CULL_FRONT;
  rasterizer_desc.FrontCounterClockwise = TRUE;
  rasterizer_desc.ScissorEnable = TRUE;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
      &rasterizer_desc, rasterizer.put())));
  D3D11_RASTERIZER_DESC actual_rasterizer = {};
  rasterizer->GetDesc(&actual_rasterizer);
  EXPECT_EQ(actual_rasterizer.FillMode, rasterizer_desc.FillMode);
  EXPECT_EQ(actual_rasterizer.CullMode, rasterizer_desc.CullMode);
  EXPECT_TRUE(actual_rasterizer.FrontCounterClockwise);
  EXPECT_TRUE(actual_rasterizer.ScissorEnable);

  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.AlphaToCoverageEnable = TRUE;
  blend_desc.IndependentBlendEnable = TRUE;
  blend_desc.RenderTarget[0].BlendEnable = TRUE;
  blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D11BlendState> blend;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBlendState(&blend_desc, blend.put())));
  D3D11_BLEND_DESC actual_blend = {};
  blend->GetDesc(&actual_blend);
  EXPECT_TRUE(actual_blend.AlphaToCoverageEnable);
  EXPECT_TRUE(actual_blend.IndependentBlendEnable);
  EXPECT_EQ(actual_blend.RenderTarget[0].SrcBlend, D3D11_BLEND_SRC_ALPHA);

  D3D11_DEPTH_STENCIL_DESC depth_desc = {};
  depth_desc.DepthEnable = TRUE;
  depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_desc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
  depth_desc.StencilEnable = TRUE;
  depth_desc.StencilReadMask = 0x3f;
  depth_desc.StencilWriteMask = 0x7f;
  depth_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
  depth_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
  depth_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
  depth_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
  depth_desc.BackFace = depth_desc.FrontFace;
  ComPtr<ID3D11DepthStencilState> depth;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDepthStencilState(&depth_desc, depth.put())));
  D3D11_DEPTH_STENCIL_DESC actual_depth = {};
  depth->GetDesc(&actual_depth);
  EXPECT_EQ(actual_depth.DepthFunc, D3D11_COMPARISON_GREATER_EQUAL);
  EXPECT_EQ(actual_depth.StencilReadMask, 0x3f);
  EXPECT_EQ(actual_depth.StencilWriteMask, 0x7f);

  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  sampler_desc.BorderColor[0] = 0.25f;
  ComPtr<ID3D11SamplerState> sampler;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateSamplerState(&sampler_desc, sampler.put())));
  D3D11_SAMPLER_DESC actual_sampler = {};
  sampler->GetDesc(&actual_sampler);
  EXPECT_EQ(actual_sampler.AddressU, D3D11_TEXTURE_ADDRESS_BORDER);
  EXPECT_EQ(actual_sampler.AddressV, D3D11_TEXTURE_ADDRESS_MIRROR);
  EXPECT_FLOAT_EQ(actual_sampler.BorderColor[0], 0.25f);
}

TEST_F(D3D11PipelineSpec, ClearStateResetsBindingsAndDynamicState) {
  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = 64;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  ComPtr<ID3D11Buffer> vertex_buffer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &buffer_desc, nullptr, vertex_buffer.put())));

  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 4;
  texture_desc.Height = 4;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags =
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> resource_view;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateShaderResourceView(
      texture.get(), nullptr, resource_view.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      texture.get(), nullptr, target_view.put())));

  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateSamplerState(
      &sampler_desc, sampler.put())));

  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D11BlendState> blend;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBlendState(&blend_desc, blend.put())));
  D3D11_RASTERIZER_DESC rasterizer_desc = {};
  rasterizer_desc.FillMode = D3D11_FILL_SOLID;
  rasterizer_desc.CullMode = D3D11_CULL_BACK;
  rasterizer_desc.DepthClipEnable = TRUE;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
      &rasterizer_desc, rasterizer.put())));

  constexpr UINT stride = 16;
  constexpr UINT offset = 4;
  ID3D11Buffer *vertex_buffers[] = {vertex_buffer.get()};
  ID3D11ShaderResourceView *resource_views[] = {resource_view.get()};
  ID3D11SamplerState *samplers[] = {sampler.get()};
  ID3D11RenderTargetView *target_views[] = {target_view.get()};
  const FLOAT blend_factor[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  const D3D11_VIEWPORT viewport = {1.0f, 2.0f, 3.0f, 4.0f, 0.0f, 1.0f};
  context_.context()->IASetVertexBuffers(0, 1, vertex_buffers, &stride,
                                         &offset);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->PSSetShaderResources(0, 1, resource_views);
  context_.context()->PSSetSamplers(0, 1, samplers);
  context_.context()->OMSetRenderTargets(1, target_views, nullptr);
  context_.context()->OMSetBlendState(blend.get(), blend_factor, 0x13579bdf);
  context_.context()->RSSetState(rasterizer.get());
  context_.context()->RSSetViewports(1, &viewport);

  ComPtr<ID3D11Buffer> queried_vertex;
  UINT queried_stride = 0;
  UINT queried_offset = 0;
  context_.context()->IAGetVertexBuffers(0, 1, queried_vertex.put(),
                                         &queried_stride, &queried_offset);
  EXPECT_EQ(queried_vertex.get(), vertex_buffer.get());
  EXPECT_EQ(queried_stride, stride);
  EXPECT_EQ(queried_offset, offset);
  ComPtr<ID3D11RenderTargetView> queried_target;
  context_.context()->OMGetRenderTargets(1, queried_target.put(), nullptr);
  EXPECT_EQ(queried_target.get(), target_view.get());

  context_.context()->ClearState();

  queried_vertex.reset();
  queried_stride = ~UINT{0};
  queried_offset = ~UINT{0};
  context_.context()->IAGetVertexBuffers(0, 1, queried_vertex.put(),
                                         &queried_stride, &queried_offset);
  EXPECT_FALSE(queried_vertex);
  EXPECT_EQ(queried_stride, 0u);
  EXPECT_EQ(queried_offset, 0u);
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  context_.context()->IAGetPrimitiveTopology(&topology);
  EXPECT_EQ(topology, D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);

  ComPtr<ID3D11ShaderResourceView> queried_resource;
  ComPtr<ID3D11SamplerState> queried_sampler;
  context_.context()->PSGetShaderResources(0, 1, queried_resource.put());
  context_.context()->PSGetSamplers(0, 1, queried_sampler.put());
  EXPECT_FALSE(queried_resource);
  EXPECT_FALSE(queried_sampler);
  queried_target.reset();
  context_.context()->OMGetRenderTargets(1, queried_target.put(), nullptr);
  EXPECT_FALSE(queried_target);

  ComPtr<ID3D11BlendState> queried_blend;
  FLOAT queried_factor[4] = {};
  UINT queried_mask = 0;
  context_.context()->OMGetBlendState(queried_blend.put(), queried_factor,
                                      &queried_mask);
  EXPECT_FALSE(queried_blend);
  for (FLOAT component : queried_factor)
    EXPECT_FLOAT_EQ(component, 1.0f);
  EXPECT_EQ(queried_mask, ~UINT{0});
  ComPtr<ID3D11RasterizerState> queried_rasterizer;
  context_.context()->RSGetState(queried_rasterizer.put());
  EXPECT_FALSE(queried_rasterizer);
  UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  context_.context()->RSGetViewports(&viewport_count, nullptr);
  EXPECT_EQ(viewport_count, 0u);
}

TEST_F(D3D11PipelineSpec, CreatesSelectedTexture2DArrayMipAndSliceViews) {
  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 16;
  texture_desc.Height = 8;
  texture_desc.MipLevels = 4;
  texture_desc.ArraySize = 3;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags =
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = texture_desc.Format;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
  srv_desc.Texture2DArray.MostDetailedMip = 1;
  srv_desc.Texture2DArray.MipLevels = 2;
  srv_desc.Texture2DArray.FirstArraySlice = 1;
  srv_desc.Texture2DArray.ArraySize = 2;
  ComPtr<ID3D11ShaderResourceView> srv;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateShaderResourceView(
      texture.get(), &srv_desc, srv.put())));

  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = texture_desc.Format;
  rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
  rtv_desc.Texture2DArray.MipSlice = 2;
  rtv_desc.Texture2DArray.FirstArraySlice = 2;
  rtv_desc.Texture2DArray.ArraySize = 1;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
      texture.get(), &rtv_desc, rtv.put())));

  D3D11_SHADER_RESOURCE_VIEW_DESC actual_srv = {};
  D3D11_RENDER_TARGET_VIEW_DESC actual_rtv = {};
  srv->GetDesc(&actual_srv);
  rtv->GetDesc(&actual_rtv);
  EXPECT_EQ(actual_srv.ViewDimension, D3D11_SRV_DIMENSION_TEXTURE2DARRAY);
  EXPECT_EQ(actual_srv.Texture2DArray.MostDetailedMip, 1u);
  EXPECT_EQ(actual_srv.Texture2DArray.MipLevels, 2u);
  EXPECT_EQ(actual_srv.Texture2DArray.FirstArraySlice, 1u);
  EXPECT_EQ(actual_srv.Texture2DArray.ArraySize, 2u);
  EXPECT_EQ(actual_rtv.ViewDimension, D3D11_RTV_DIMENSION_TEXTURE2DARRAY);
  EXPECT_EQ(actual_rtv.Texture2DArray.MipSlice, 2u);
  EXPECT_EQ(actual_rtv.Texture2DArray.FirstArraySlice, 2u);
  EXPECT_EQ(actual_rtv.Texture2DArray.ArraySize, 1u);
}

TEST_F(D3D11PipelineSpec, RejectsOutOfRangeTextureArrayViewsAndClearsOutputs) {
  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 8;
  texture_desc.Height = 8;
  texture_desc.MipLevels = 3;
  texture_desc.ArraySize = 2;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags =
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = texture_desc.Format;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
  srv_desc.Texture2DArray.MostDetailedMip = 2;
  srv_desc.Texture2DArray.MipLevels = 2;
  srv_desc.Texture2DArray.ArraySize = 1;
  ID3D11ShaderResourceView *srv =
      reinterpret_cast<ID3D11ShaderResourceView *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(context_.device()->CreateShaderResourceView(
      texture.get(), &srv_desc, &srv)));
  EXPECT_EQ(srv, nullptr);

  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = texture_desc.Format;
  rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
  rtv_desc.Texture2DArray.MipSlice = 3;
  rtv_desc.Texture2DArray.ArraySize = 1;
  ID3D11RenderTargetView *rtv =
      reinterpret_cast<ID3D11RenderTargetView *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(context_.device()->CreateRenderTargetView(
      texture.get(), &rtv_desc, &rtv)));
  EXPECT_EQ(rtv, nullptr);
}

TEST_F(D3D11PipelineSpec, CreatesCompatibleViewsOfTypelessDepthTexture) {
  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 8;
  texture_desc.Height = 8;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags =
      D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, nullptr, texture.put())));

  D3D11_DEPTH_STENCIL_VIEW_DESC depth_desc = {};
  depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  ComPtr<ID3D11DepthStencilView> depth_view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateDepthStencilView(
      texture.get(), &depth_desc, depth_view.put())));

  D3D11_SHADER_RESOURCE_VIEW_DESC resource_desc = {};
  resource_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  resource_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  resource_desc.Texture2D.MipLevels = 1;
  ComPtr<ID3D11ShaderResourceView> resource_view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateShaderResourceView(
      texture.get(), &resource_desc, resource_view.put())));

  D3D11_DEPTH_STENCIL_VIEW_DESC actual_depth = {};
  D3D11_SHADER_RESOURCE_VIEW_DESC actual_resource = {};
  depth_view->GetDesc(&actual_depth);
  resource_view->GetDesc(&actual_resource);
  EXPECT_EQ(actual_depth.Format, depth_desc.Format);
  EXPECT_EQ(actual_resource.Format, resource_desc.Format);
}

} // namespace
