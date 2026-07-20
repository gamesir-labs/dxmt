#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

using Float4 = std::array<float, 4>;

constexpr std::string_view kFullscreenVertexShader = R"(
struct VertexOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

VertexOutput main(uint vertex_id : SV_VertexID) {
  VertexOutput output;
  output.uv = float2((vertex_id << 1) & 2, vertex_id & 2);
  output.position = float4(output.uv * float2(2.0, -2.0) +
                           float2(-1.0, 1.0), 0.0, 1.0);
  return output;
}
)";

constexpr std::string_view kSampleVariantsPixelShader = R"(
Texture2D<float> input_texture : register(t0);
SamplerState point_sampler : register(s0);

struct PixelInput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target {
  float implicit_value = input_texture.Sample(point_sampler, input.uv);
  float biased_value = input_texture.SampleBias(point_sampler, input.uv, 1.0);
  float gradient_value = input_texture.SampleGrad(
      point_sampler, input.uv, float2(0.25, 0.0), float2(0.0, 0.25));
  float explicit_value = input_texture.SampleLevel(point_sampler, input.uv, 2.0);
  return float4(implicit_value, biased_value, gradient_value, explicit_value);
}
)";

constexpr std::string_view kGatherPixelShader = R"(
Texture2D<float4> input_texture : register(t0);
SamplerState point_sampler : register(s0);

float4 main(float4 position : SV_Position) : SV_Target {
  return input_texture.Gather(point_sampler, position.xy / float2(4.0, 4.0));
}
)";

constexpr std::string_view kComparisonPixelShader = R"(
Texture2D<float> input_texture : register(t0);
SamplerComparisonState comparison_sampler : register(s0);

float4 main(float4 position : SV_Position) : SV_Target {
  if (position.x < 1.0) {
    return input_texture.GatherCmp(comparison_sampler,
                                   float2(0.375, 0.375), 0.5);
  }
  return float4(
      input_texture.SampleCmp(comparison_sampler, float2(0.375, 0.375), 0.5),
      input_texture.SampleCmpLevelZero(comparison_sampler,
                                       float2(0.125, 0.125), 0.5),
      input_texture.SampleCmp(comparison_sampler, float2(0.625, 0.375), 0.5),
      input_texture.SampleCmpLevelZero(comparison_sampler,
                                       float2(0.625, 0.625), 0.5));
}
)";

// Compiled with Microsoft D3DCompiler 43 from the redistributable
// Microsoft.DXSDK.D3DX 9.29.952.8 package, then stripped of reflection data.
// The local Wine HLSL frontend does not define CalculateLevelOfDetail, so the
// checked-in SM5 DXBC keeps this runtime behavior executable on every worker.
constexpr DWORD kCalculateLodPixelShader[] = {
#if 0
  Texture2D<float> input_texture : register(t0);
  SamplerState point_sampler : register(s0);

  struct PixelInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
  };

  float4 main(PixelInput input) : SV_Target {
    return float4(
        input_texture.CalculateLevelOfDetail(point_sampler, input.uv),
        input_texture.CalculateLevelOfDetailUnclamped(point_sampler, input.uv),
        0.0, 1.0);
  }
#endif
    0x43425844, 0xdf398b08, 0x0505f031, 0x27c5c13a, 0x28f1444f, 0x00000001,
    0x0000016c, 0x00000003, 0x0000002c, 0x00000084, 0x000000b8, 0x4e475349,
    0x00000050, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000001,
    0x00000003, 0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000,
    0x00000003, 0x00000001, 0x00000303, 0x505f5653, 0x7469736f, 0x006e6f69,
    0x43584554, 0x44524f4f, 0xababab00, 0x4e47534f, 0x0000002c, 0x00000001,
    0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
    0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x000000ac,
    0x00000050, 0x0000002b, 0x0100086a, 0x0300005a, 0x00106000, 0x00000000,
    0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x03001062, 0x00101032,
    0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x0900006c, 0x00102012,
    0x00000000, 0x00101046, 0x00000001, 0x0010700a, 0x00000000, 0x00106000,
    0x00000000, 0x0900006c, 0x00102022, 0x00000000, 0x00101046, 0x00000001,
    0x0010701a, 0x00000000, 0x00106000, 0x00000000, 0x08000036, 0x001020c2,
    0x00000000, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x3f800000,
    0x0100003e,
};

HRESULT CreateFloatTarget(ID3D11Device *device, UINT width, UINT height,
                          ID3D11Texture2D **texture,
                          ID3D11RenderTargetView **rtv) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, texture);
  if (FAILED(hr))
    return hr;
  return device->CreateRenderTargetView(*texture, nullptr, rtv);
}

HRESULT ReadFloatTarget(ID3D11Device *device, ID3D11DeviceContext *context,
                        ID3D11Texture2D *source, std::vector<Float4> *pixels) {
  D3D11_TEXTURE2D_DESC desc = {};
  source->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, staging.put());
  if (FAILED(hr))
    return hr;
  context->CopyResource(staging.get(), source);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;
  pixels->resize(desc.Width * desc.Height);
  for (UINT y = 0; y < desc.Height; ++y) {
    std::memcpy(pixels->data() + y * desc.Width,
                static_cast<const BYTE *>(mapped.pData) + y * mapped.RowPitch,
                desc.Width * sizeof(Float4));
  }
  context->Unmap(staging.get(), 0);
  return S_OK;
}

HRESULT CreateSampler(ID3D11Device *device, bool comparison,
                      ID3D11SamplerState **sampler) {
  D3D11_SAMPLER_DESC desc = {};
  desc.Filter = comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT
                           : D3D11_FILTER_MIN_MAG_MIP_POINT;
  desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  desc.MaxAnisotropy = 1;
  desc.ComparisonFunc =
      comparison ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER;
  desc.MaxLOD = D3D11_FLOAT32_MAX;
  return device->CreateSamplerState(&desc, sampler);
}

HRESULT CreateMipTextureSrv(ID3D11Device *device, ID3D11DeviceContext *context,
                            ID3D11ShaderResourceView **srv) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 8;
  desc.Height = 8;
  desc.MipLevels = 4;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, texture.put());
  if (FAILED(hr))
    return hr;

  constexpr std::array<float, 4> kMipValues = {0.125f, 0.375f, 0.625f, 0.875f};
  for (UINT mip = 0; mip < desc.MipLevels; ++mip) {
    const UINT width = desc.Width >> mip;
    const UINT height = desc.Height >> mip;
    const std::vector<float> values(width * height, kMipValues[mip]);
    context->UpdateSubresource(texture.get(), mip, nullptr, values.data(),
                               width * sizeof(float), 0);
  }
  return device->CreateShaderResourceView(texture.get(), nullptr, srv);
}

HRESULT CreateGatherTextureSrv(ID3D11Device *device,
                               ID3D11ShaderResourceView **srv) {
  constexpr std::array<Float4, 16> kTexels = {{
      {0.0f, 0.0f, 0.0f, 0.0f},
      {1.0f, 1.0f, 0.0f, 0.0f},
      {2.0f, 2.0f, 0.0f, 0.0f},
      {3.0f, 3.0f, 0.0f, 0.0f},
      {4.0f, 0.1f, 0.0f, 0.0f},
      {5.0f, 1.1f, 0.0f, 0.0f},
      {6.0f, 2.1f, 0.0f, 0.0f},
      {7.0f, 3.1f, 0.0f, 0.0f},
      {8.0f, 0.2f, 0.0f, 0.0f},
      {9.0f, 1.2f, 0.0f, 0.0f},
      {0.5f, 2.2f, 0.0f, 0.0f},
      {1.5f, 3.2f, 0.0f, 0.0f},
      {2.5f, 0.3f, 0.0f, 0.0f},
      {3.5f, 1.3f, 0.0f, 0.0f},
      {4.5f, 2.3f, 0.0f, 0.0f},
      {5.5f, 3.3f, 0.0f, 0.0f},
  }};
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 4;
  desc.Height = 4;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = kTexels.data();
  initial.SysMemPitch = desc.Width * sizeof(Float4);
  ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = device->CreateTexture2D(&desc, &initial, texture.put());
  if (FAILED(hr))
    return hr;
  return device->CreateShaderResourceView(texture.get(), nullptr, srv);
}

HRESULT CreateComparisonTextureSrv(ID3D11Device *device,
                                   ID3D11ShaderResourceView **srv) {
  constexpr std::array<float, 16> kTexels = {
      0.00f, 0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f,
      0.80f, 0.90f, 0.05f, 0.15f, 0.25f, 0.35f, 0.45f, 0.55f,
  };
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 4;
  desc.Height = 4;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = kTexels.data();
  initial.SysMemPitch = desc.Width * sizeof(float);
  ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = device->CreateTexture2D(&desc, &initial, texture.put());
  if (FAILED(hr))
    return hr;

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MipLevels = 1;
  return device->CreateShaderResourceView(texture.get(), &srv_desc, srv);
}

void ExpectFloat4(const Float4 &actual, const Float4 &expected, UINT x,
                  UINT y) {
  for (std::size_t component = 0; component < actual.size(); ++component) {
    EXPECT_FLOAT_EQ(actual[component], expected[component])
        << "pixel (" << x << ", " << y << ") component " << component;
  }
}

class D3D11TextureSamplingOpsSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto compilation = CompileShader(kFullscreenVertexShader, "vs_5_0");
    ASSERT_EQ(compilation.result, S_OK) << compilation.diagnostic_text();
    ASSERT_EQ(context_.device()->CreateVertexShader(
                  compilation.bytecode->GetBufferPointer(),
                  compilation.bytecode->GetBufferSize(), nullptr,
                  vertex_shader_.put()),
              S_OK);
  }

  ComPtr<ID3D11PixelShader> CompilePixelShader(std::string_view source) {
    const auto compilation = CompileShader(source, "ps_5_0");
    EXPECT_EQ(compilation.result, S_OK) << compilation.diagnostic_text();
    ComPtr<ID3D11PixelShader> shader;
    if (SUCCEEDED(compilation.result)) {
      EXPECT_EQ(context_.device()->CreatePixelShader(
                    compilation.bytecode->GetBufferPointer(),
                    compilation.bytecode->GetBufferSize(), nullptr,
                    shader.put()),
                S_OK);
    }
    return shader;
  }

  void Draw(ID3D11RenderTargetView *rtv, UINT width, UINT height,
            ID3D11PixelShader *pixel_shader, ID3D11ShaderResourceView *srv,
            ID3D11SamplerState *sampler) {
    const float clear[4] = {-100.0f, -100.0f, -100.0f, -100.0f};
    context_.context()->ClearRenderTargetView(rtv, clear);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader, nullptr, 0);
    context_.context()->PSSetShaderResources(0, 1, &srv);
    context_.context()->PSSetSamplers(0, 1, &sampler);
    context_.context()->OMSetRenderTargets(1, &rtv, nullptr);
    context_.context()->Draw(3, 0);

    ID3D11ShaderResourceView *null_srv = nullptr;
    ID3D11RenderTargetView *null_rtv = nullptr;
    context_.context()->PSSetShaderResources(0, 1, &null_srv);
    context_.context()->OMSetRenderTargets(1, &null_rtv, nullptr);
    context_.context()->PSSetShader(nullptr, nullptr, 0);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
};

TEST_F(D3D11TextureSamplingOpsSpec,
       SampleLevelBiasAndGradientSelectExpectedMips) {
  auto pixel_shader = CompilePixelShader(kSampleVariantsPixelShader);
  ASSERT_TRUE(pixel_shader);
  ComPtr<ID3D11ShaderResourceView> srv;
  ASSERT_EQ(
      CreateMipTextureSrv(context_.device(), context_.context(), srv.put()),
      S_OK);
  ComPtr<ID3D11SamplerState> sampler;
  ASSERT_EQ(CreateSampler(context_.device(), false, sampler.put()), S_OK);
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_EQ(CreateFloatTarget(context_.device(), 8, 8, target.put(), rtv.put()),
            S_OK);

  Draw(rtv.get(), 8, 8, pixel_shader.get(), srv.get(), sampler.get());
  std::vector<Float4> pixels;
  ASSERT_EQ(ReadFloatTarget(context_.device(), context_.context(), target.get(),
                            &pixels),
            S_OK);
  for (UINT y = 0; y < 8; ++y) {
    for (UINT x = 0; x < 8; ++x) {
      const auto &pixel = pixels[y * 8 + x];
      EXPECT_FLOAT_EQ(pixel[0], 0.125f) << "pixel (" << x << ", " << y << ')';
      EXPECT_FLOAT_EQ(pixel[1], 0.375f) << "pixel (" << x << ", " << y << ')';
      EXPECT_FLOAT_EQ(pixel[2], 0.375f) << "pixel (" << x << ", " << y << ')';
      EXPECT_FLOAT_EQ(pixel[3], 0.625f) << "pixel (" << x << ", " << y << ')';
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11TextureSamplingOpsSpec,
       CalculateLodReportsClampedAndUnclampedValues) {
  ComPtr<ID3D11PixelShader> pixel_shader;
  ASSERT_EQ(context_.device()->CreatePixelShader(
                kCalculateLodPixelShader, sizeof(kCalculateLodPixelShader),
                nullptr, pixel_shader.put()),
            S_OK);
  ComPtr<ID3D11ShaderResourceView> srv;
  ASSERT_EQ(
      CreateMipTextureSrv(context_.device(), context_.context(), srv.put()),
      S_OK);
  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MinLOD = 1.0f;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler;
  ASSERT_EQ(context_.device()->CreateSamplerState(&sampler_desc, sampler.put()),
            S_OK);
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_EQ(CreateFloatTarget(context_.device(), 8, 8, target.put(), rtv.put()),
            S_OK);

  Draw(rtv.get(), 8, 8, pixel_shader.get(), srv.get(), sampler.get());
  std::vector<Float4> pixels;
  ASSERT_EQ(ReadFloatTarget(context_.device(), context_.context(), target.get(),
                            &pixels),
            S_OK);
  for (UINT y = 0; y < 8; ++y) {
    for (UINT x = 0; x < 8; ++x)
      ExpectFloat4(pixels[y * 8 + x], {1.0f, 0.0f, 0.0f, 1.0f}, x, y);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11TextureSamplingOpsSpec, GatherReturnsOrderedClampedNeighborhoods) {
  auto pixel_shader = CompilePixelShader(kGatherPixelShader);
  ASSERT_TRUE(pixel_shader);
  ComPtr<ID3D11ShaderResourceView> srv;
  ASSERT_EQ(CreateGatherTextureSrv(context_.device(), srv.put()), S_OK);
  ComPtr<ID3D11SamplerState> sampler;
  ASSERT_EQ(CreateSampler(context_.device(), false, sampler.put()), S_OK);
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_EQ(CreateFloatTarget(context_.device(), 4, 4, target.put(), rtv.put()),
            S_OK);

  Draw(rtv.get(), 4, 4, pixel_shader.get(), srv.get(), sampler.get());
  std::vector<Float4> pixels;
  ASSERT_EQ(ReadFloatTarget(context_.device(), context_.context(), target.get(),
                            &pixels),
            S_OK);
  // Wine's public D3D11 gather conformance vector exercises component order
  // as well as clamp behavior along all four texture edges.
  constexpr std::array<Float4, 16> kExpected = {{
      {4.0f, 5.0f, 1.0f, 0.0f},
      {5.0f, 6.0f, 2.0f, 1.0f},
      {6.0f, 7.0f, 3.0f, 2.0f},
      {7.0f, 7.0f, 3.0f, 3.0f},
      {8.0f, 9.0f, 5.0f, 4.0f},
      {9.0f, 0.5f, 6.0f, 5.0f},
      {0.5f, 1.5f, 7.0f, 6.0f},
      {1.5f, 1.5f, 7.0f, 7.0f},
      {2.5f, 3.5f, 9.0f, 8.0f},
      {3.5f, 4.5f, 0.5f, 9.0f},
      {4.5f, 5.5f, 1.5f, 0.5f},
      {5.5f, 5.5f, 1.5f, 1.5f},
      {2.5f, 3.5f, 3.5f, 2.5f},
      {3.5f, 4.5f, 4.5f, 3.5f},
      {4.5f, 5.5f, 5.5f, 4.5f},
      {5.5f, 5.5f, 5.5f, 5.5f},
  }};
  ASSERT_EQ(pixels.size(), kExpected.size());
  for (UINT y = 0; y < 4; ++y) {
    for (UINT x = 0; x < 4; ++x)
      ExpectFloat4(pixels[y * 4 + x], kExpected[y * 4 + x], x, y);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11TextureSamplingOpsSpec,
       SampleCmpLevelZeroAndGatherCmpReturnBooleanResults) {
  auto pixel_shader = CompilePixelShader(kComparisonPixelShader);
  ASSERT_TRUE(pixel_shader);
  ComPtr<ID3D11ShaderResourceView> srv;
  ASSERT_EQ(CreateComparisonTextureSrv(context_.device(), srv.put()), S_OK);
  ComPtr<ID3D11SamplerState> sampler;
  ASSERT_EQ(CreateSampler(context_.device(), true, sampler.put()), S_OK);
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_EQ(CreateFloatTarget(context_.device(), 2, 1, target.put(), rtv.put()),
            S_OK);

  Draw(rtv.get(), 2, 1, pixel_shader.get(), srv.get(), sampler.get());
  std::vector<Float4> pixels;
  ASSERT_EQ(ReadFloatTarget(context_.device(), context_.context(), target.get(),
                            &pixels),
            S_OK);
  ASSERT_EQ(pixels.size(), 2u);
  ExpectFloat4(pixels[0], {1.0f, 0.0f, 1.0f, 1.0f}, 0, 0);
  ExpectFloat4(pixels[1], {1.0f, 0.0f, 1.0f, 0.0f}, 1, 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
