#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

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

constexpr std::string_view kConstantBufferPixelShader = R"(
cbuffer ColorConstants : register(b0) {
  float4 color;
};
float4 main() : SV_Target {
  return color;
}
)";

constexpr std::string_view kTextureSamplePixelShader = R"(
Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer SampleUv : register(b0) {
  float2 uv;
  float2 pad;
};

float4 main() : SV_Target {
  return source_texture.SampleLevel(source_sampler, uv, 0.0);
}
)";

constexpr std::string_view kTypedBufferPixelShader = R"(
Buffer<uint> source_buffer : register(t0);

float4 main() : SV_Target {
  // Encode buffer[0] as R8G8B8A8_UNORM channels for RT readback.
  uint value = source_buffer[0];
  float r = float((value >> 0) & 0xffu) / 255.0;
  float g = float((value >> 8) & 0xffu) / 255.0;
  float b = float((value >> 16) & 0xffu) / 255.0;
  float a = float((value >> 24) & 0xffu) / 255.0;
  return float4(r, g, b, a);
}
)";

constexpr std::string_view kStructuredBufferPixelShader = R"(
struct Element {
  uint value;
};
StructuredBuffer<Element> source_buffer : register(t0);

float4 main() : SV_Target {
  uint value = source_buffer[0].value;
  float r = float((value >> 0) & 0xffu) / 255.0;
  float g = float((value >> 8) & 0xffu) / 255.0;
  float b = float((value >> 16) & 0xffu) / 255.0;
  float a = float((value >> 24) & 0xffu) / 255.0;
  return float4(r, g, b, a);
}
)";

constexpr std::string_view kUavTextureComputeShader = R"(
RWTexture2D<uint> output : register(u0);

[numthreads(4, 4, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
  // Distinct per-texel pattern: base 0xA5000000 + y*width + x.
  output[thread_id.xy] = 0xa5000000u + thread_id.y * 4u + thread_id.x;
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

uint32_t PackUnormRgba(float r, float g, float b, float a) {
  const auto channel = [](float value) -> uint32_t {
    const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    return static_cast<uint32_t>(std::lround(clamped * 255.0f)) & 0xffu;
  };
  return channel(r) | (channel(g) << 8) | (channel(b) << 16) |
         (channel(a) << 24);
}

HRESULT ReadTextureRgba8(ID3D11Device *device, ID3D11DeviceContext *context,
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

HRESULT CreateSolidRenderTarget(ID3D11Device *device, UINT width, UINT height,
                                ComPtr<ID3D11Texture2D> *texture,
                                ComPtr<ID3D11RenderTargetView> *view) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, texture->put());
  if (FAILED(hr))
    return hr;
  return device->CreateRenderTargetView(texture->get(), nullptr, view->put());
}

enum class ConstantBufferUpdatePath {
  UpdateSubresource,
  MapDiscard,
};

const char *UpdatePathName(ConstantBufferUpdatePath path) {
  switch (path) {
  case ConstantBufferUpdatePath::UpdateSubresource:
    return "UpdateSubresource";
  case ConstantBufferUpdatePath::MapDiscard:
    return "MapDiscard";
  }
  return "Unknown";
}

struct ConstantBufferCase {
  UINT byte_width;
  ConstantBufferUpdatePath path;
  float color[4];
};

class D3D11ViewMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

// ---------------------------------------------------------------------------
// 1. Constant buffer (b0) size × update-path matrix
// ---------------------------------------------------------------------------

TEST_F(D3D11ViewMatrixSpec, ConstantBufferReadsFloat4FromSlotB0) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kConstantBufferPixelShader, "ps_5_0");
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

  // Distinct colors per (size, path) so a wrong slot/path cannot pass by chance.
  const std::array<ConstantBufferCase, 6> cases = {{
      {16, ConstantBufferUpdatePath::UpdateSubresource,
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {256, ConstantBufferUpdatePath::UpdateSubresource,
       {0.0f, 1.0f, 0.0f, 1.0f}},
      {512, ConstantBufferUpdatePath::UpdateSubresource,
       {0.0f, 0.0f, 1.0f, 1.0f}},
      {16, ConstantBufferUpdatePath::MapDiscard, {1.0f, 1.0f, 0.0f, 1.0f}},
      {256, ConstantBufferUpdatePath::MapDiscard, {1.0f, 0.0f, 1.0f, 1.0f}},
      {512, ConstantBufferUpdatePath::MapDiscard, {0.0f, 1.0f, 1.0f, 1.0f}},
  }};

  for (const auto &test_case : cases) {
    SCOPED_TRACE(std::string("byte_width=") +
                 std::to_string(test_case.byte_width) +
                 " path=" + UpdatePathName(test_case.path));

    std::vector<uint8_t> payload(test_case.byte_width, 0xcd);
    std::memcpy(payload.data(), test_case.color, sizeof(test_case.color));

    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = test_case.byte_width;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (test_case.path == ConstantBufferUpdatePath::MapDiscard) {
      buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
      buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    } else {
      buffer_desc.Usage = D3D11_USAGE_DEFAULT;
      buffer_desc.CPUAccessFlags = 0;
    }

    ComPtr<ID3D11Buffer> constant_buffer;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
        &buffer_desc, nullptr, constant_buffer.put())));

    if (test_case.path == ConstantBufferUpdatePath::MapDiscard) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
          constant_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
      std::memcpy(mapped.pData, payload.data(), payload.size());
      context_.context()->Unmap(constant_buffer.get(), 0);
    } else {
      context_.context()->UpdateSubresource(constant_buffer.get(), 0, nullptr,
                                            payload.data(), 0, 0);
    }

    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    ASSERT_TRUE(HResultSucceeded(CreateSolidRenderTarget(
        context_.device(), 8, 8, &target, &target_view)));

    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = 8.0f;
    viewport.Height = 8.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11RenderTargetView *targets[] = {target_view.get()};
    ID3D11Buffer *cbs[] = {constant_buffer.get()};
    context_.context()->OMSetRenderTargets(1, targets, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
    context_.context()->PSSetConstantBuffers(0, 1, cbs);
    context_.context()->Draw(3, 0);
    context_.context()->PSSetConstantBuffers(0, 0, nullptr);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

    const uint32_t expected =
        PackUnormRgba(test_case.color[0], test_case.color[1],
                      test_case.color[2], test_case.color[3]);
    for (UINT y = 0; y < 8; ++y) {
      for (UINT x = 0; x < 8; ++x) {
        uint32_t actual = 0;
        ASSERT_TRUE(HResultSucceeded(ReadTextureRgba8(
            context_.device(), context_.context(), target.get(), x, y,
            &actual)));
        EXPECT_TRUE(ColorMatches(actual, expected, 2))
            << "pixel (" << x << ", " << y << ") was 0x" << std::hex << actual
            << " expected 0x" << expected;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 2. Texture2D SRV point-sample matrix (known colors at fixed UVs)
// ---------------------------------------------------------------------------

TEST_F(D3D11ViewMatrixSpec, Texture2DSrvPointSamplesKnownColors) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kTextureSamplePixelShader, "ps_5_0");
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

  // 2×2 source texture with distinct corner colors (B8G8R8A8_UNORM layout).
  // texel (0,0)=R, (1,0)=G, (0,1)=B, (1,1)=W
  constexpr uint32_t kTexels[4] = {
      0xff0000ffu, // R
      0xff00ff00u, // G
      0xffff0000u, // B
      0xffffffffu, // W
  };
  D3D11_TEXTURE2D_DESC source_desc = {};
  source_desc.Width = 2;
  source_desc.Height = 2;
  source_desc.MipLevels = 1;
  source_desc.ArraySize = 1;
  source_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  source_desc.SampleDesc.Count = 1;
  source_desc.Usage = D3D11_USAGE_DEFAULT;
  source_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = kTexels;
  initial.SysMemPitch = 2 * sizeof(uint32_t);
  ComPtr<ID3D11Texture2D> source;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &source_desc, &initial, source.put())));
  ComPtr<ID3D11ShaderResourceView> source_srv;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateShaderResourceView(
      source.get(), nullptr, source_srv.put())));

  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateSamplerState(&sampler_desc, sampler.put())));

  // UV matrix: center of each 2×2 texel + a few edge-adjacent centers.
  struct SampleCase {
    float uv[2];
    uint32_t expected;
    const char *name;
  };
  const std::array<SampleCase, 6> samples = {{
      {{0.25f, 0.25f}, kTexels[0], "texel00"},
      {{0.75f, 0.25f}, kTexels[1], "texel10"},
      {{0.25f, 0.75f}, kTexels[2], "texel01"},
      {{0.75f, 0.75f}, kTexels[3], "texel11"},
      {{0.0f, 0.0f}, kTexels[0], "corner00_clamp"},
      {{1.0f, 1.0f}, kTexels[3], "corner11_clamp"},
  }};

  // UV constant buffer (16 bytes minimum).
  D3D11_BUFFER_DESC uv_cb_desc = {};
  uv_cb_desc.ByteWidth = 16;
  uv_cb_desc.Usage = D3D11_USAGE_DYNAMIC;
  uv_cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  uv_cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ComPtr<ID3D11Buffer> uv_cb;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&uv_cb_desc, nullptr, uv_cb.put())));

  for (const auto &sample : samples) {
    SCOPED_TRACE(sample.name);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        uv_cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
    float *uv_data = static_cast<float *>(mapped.pData);
    uv_data[0] = sample.uv[0];
    uv_data[1] = sample.uv[1];
    uv_data[2] = 0.0f;
    uv_data[3] = 0.0f;
    context_.context()->Unmap(uv_cb.get(), 0);

    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    ASSERT_TRUE(HResultSucceeded(CreateSolidRenderTarget(
        context_.device(), 4, 4, &target, &target_view)));
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = 4.0f;
    viewport.Height = 4.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11RenderTargetView *targets[] = {target_view.get()};
    ID3D11ShaderResourceView *srvs[] = {source_srv.get()};
    ID3D11SamplerState *samplers[] = {sampler.get()};
    ID3D11Buffer *cbs[] = {uv_cb.get()};
    context_.context()->OMSetRenderTargets(1, targets, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
    context_.context()->PSSetShaderResources(0, 1, srvs);
    context_.context()->PSSetSamplers(0, 1, samplers);
    context_.context()->PSSetConstantBuffers(0, 1, cbs);
    context_.context()->Draw(3, 0);
    ID3D11ShaderResourceView *null_srv[] = {nullptr};
    context_.context()->PSSetShaderResources(0, 1, null_srv);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

    uint32_t actual = 0;
    ASSERT_TRUE(HResultSucceeded(ReadTextureRgba8(
        context_.device(), context_.context(), target.get(), 2, 2, &actual)));
    EXPECT_TRUE(ColorMatches(actual, sample.expected, 2))
        << "sample " << sample.name << " was 0x" << std::hex << actual
        << " expected 0x" << sample.expected;
  }
}

// ---------------------------------------------------------------------------
// 3. Buffer SRV — typed R32_UINT and structured
// ---------------------------------------------------------------------------

TEST_F(D3D11ViewMatrixSpec, BufferSrvTypedR32UintReadsKnownValue) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kTypedBufferPixelShader, "ps_5_0");
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

  // Matrix of known payload values encoded as RGBA via the pixel shader.
  constexpr std::array<uint32_t, 4> kValues = {
      0xff102030u,
      0x80aabbccu,
      0x00000001u,
      0xffffffffu,
  };

  for (const uint32_t value : kValues) {
    std::ostringstream value_trace;
    value_trace << "value=0x" << std::hex << value;
    SCOPED_TRACE(value_trace.str());

    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = sizeof(uint32_t) * 4;
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    // Extra elements after [0] must not be sampled by the shader.
    const uint32_t elements[4] = {value, 0xdeadbeefu, 0xcafebabeu, 0u};
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = elements;
    ComPtr<ID3D11Buffer> buffer;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
        &buffer_desc, &initial, buffer.put())));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R32_UINT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = 4;
    ComPtr<ID3D11ShaderResourceView> srv;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateShaderResourceView(
        buffer.get(), &srv_desc, srv.put())));

    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    ASSERT_TRUE(HResultSucceeded(CreateSolidRenderTarget(
        context_.device(), 4, 4, &target, &target_view)));
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = 4.0f;
    viewport.Height = 4.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11RenderTargetView *targets[] = {target_view.get()};
    ID3D11ShaderResourceView *srvs[] = {srv.get()};
    context_.context()->OMSetRenderTargets(1, targets, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
    context_.context()->PSSetShaderResources(0, 1, srvs);
    context_.context()->Draw(3, 0);
    ID3D11ShaderResourceView *null_srv[] = {nullptr};
    context_.context()->PSSetShaderResources(0, 1, null_srv);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

    uint32_t actual = 0;
    ASSERT_TRUE(HResultSucceeded(ReadTextureRgba8(
        context_.device(), context_.context(), target.get(), 1, 1, &actual)));
    EXPECT_TRUE(ColorMatches(actual, value, 1))
        << "was 0x" << std::hex << actual << " expected 0x" << value;
  }
}

TEST_F(D3D11ViewMatrixSpec, BufferSrvStructuredReadsKnownValue) {
  const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
  const auto pixel = CompileShader(kStructuredBufferPixelShader, "ps_5_0");
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

  constexpr uint32_t kValue = 0xff336699u;
  constexpr UINT kElementCount = 8;
  std::array<uint32_t, kElementCount> elements = {};
  elements[0] = kValue;
  elements[1] = 0x11111111u;

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = sizeof(elements);
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  buffer_desc.StructureByteStride = sizeof(uint32_t);
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = elements.data();
  ComPtr<ID3D11Buffer> buffer;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&buffer_desc, &initial, buffer.put())));

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = DXGI_FORMAT_UNKNOWN;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  srv_desc.Buffer.FirstElement = 0;
  srv_desc.Buffer.NumElements = kElementCount;
  ComPtr<ID3D11ShaderResourceView> srv;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateShaderResourceView(
      buffer.get(), &srv_desc, srv.put())));

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(
      CreateSolidRenderTarget(context_.device(), 4, 4, &target, &target_view)));
  const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = 4.0f;
  viewport.Height = 4.0f;
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *targets[] = {target_view.get()};
  ID3D11ShaderResourceView *srvs[] = {srv.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->PSSetShaderResources(0, 1, srvs);
  context_.context()->Draw(3, 0);
  ID3D11ShaderResourceView *null_srv[] = {nullptr};
  context_.context()->PSSetShaderResources(0, 1, null_srv);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t actual = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTextureRgba8(
      context_.device(), context_.context(), target.get(), 1, 1, &actual)));
  EXPECT_TRUE(ColorMatches(actual, kValue, 1))
      << "was 0x" << std::hex << actual << " expected 0x" << kValue;
}

// ---------------------------------------------------------------------------
// 4. UAV texture: ClearUnorderedAccessViewUint + CS write + staging readback
// ---------------------------------------------------------------------------

TEST_F(D3D11ViewMatrixSpec, UavTextureClearWritesKnownUintValue) {
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

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_UINT;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
  ComPtr<ID3D11UnorderedAccessView> uav;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateUnorderedAccessView(
      texture.get(), &uav_desc, uav.put())));

  // Matrix of clear values.
  constexpr std::array<uint32_t, 4> kClearValues = {
      0u,
      1u,
      0x12345678u,
      0xffffffffu,
  };

  D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));

  for (const uint32_t clear_value : kClearValues) {
    std::ostringstream clear_trace;
    clear_trace << "clear=0x" << std::hex << clear_value;
    SCOPED_TRACE(clear_trace.str());

    const UINT clear_values[4] = {clear_value, clear_value, clear_value,
                                  clear_value};
    context_.context()->ClearUnorderedAccessViewUint(uav.get(), clear_values);
    context_.context()->CopyResource(staging.get(), texture.get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
    for (UINT y = 0; y < texture_desc.Height; ++y) {
      const auto *row = reinterpret_cast<const uint32_t *>(
          static_cast<const uint8_t *>(mapped.pData) + y * mapped.RowPitch);
      for (UINT x = 0; x < texture_desc.Width; ++x)
        EXPECT_EQ(row[x], clear_value) << "texel (" << x << ", " << y << ')';
    }
    context_.context()->Unmap(staging.get(), 0);
  }
}

TEST_F(D3D11ViewMatrixSpec, UavTextureComputeWriteAndStagingReadback) {
  const auto compute = CompileShader(kUavTextureComputeShader, "cs_5_0");
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

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_UINT;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
  ComPtr<ID3D11UnorderedAccessView> uav;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateUnorderedAccessView(
      texture.get(), &uav_desc, uav.put())));

  // Seed with a known clear so a no-op dispatch would fail assertions.
  const UINT seed[4] = {0xcdcdcdcdu, 0xcdcdcdcdu, 0xcdcdcdcdu, 0xcdcdcdcdu};
  context_.context()->ClearUnorderedAccessViewUint(uav.get(), seed);

  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  context_.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
  context_.context()->Dispatch(1, 1, 1);
  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

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
    for (UINT x = 0; x < texture_desc.Width; ++x) {
      const uint32_t expected = 0xa5000000u + y * texture_desc.Width + x;
      EXPECT_EQ(row[x], expected) << "texel (" << x << ", " << y << ')';
    }
  }
  context_.context()->Unmap(staging.get(), 0);
}

} // namespace
