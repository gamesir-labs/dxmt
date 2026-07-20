#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

enum class TextureDimension {
  Texture1D,
  Texture1DArray,
  Texture2D,
  Texture2DArray,
  Texture3D,
  TextureCube,
  TextureCubeArray,
  Texture2DMS,
  Texture2DMSArray,
};

struct TextureDimensionCase {
  TextureDimension dimension;
  const char *name;
  std::string_view shader;
  std::array<UINT, 8> expected;
};

constexpr std::string_view kTexture1DShader = R"(
Texture1D<uint> input_texture : register(t0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint width, levels;
  input_texture.GetDimensions(1, width, levels);
  output[0] = input_texture.Load(int2(2, 1));
  output[1] = width;
  output[2] = levels;
  output[3] = 0x1d;
  output[4] = 2;
  output[5] = 1;
  output[6] = 0x1d1d;
  output[7] = 0xfeed0001;
}
)";

constexpr std::string_view kTexture1DArrayShader = R"(
Texture1DArray<uint> input_texture : register(t0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint width, elements, levels;
  input_texture.GetDimensions(1, width, elements, levels);
  output[0] = input_texture.Load(int3(2, 2, 1));
  output[1] = width;
  output[2] = elements;
  output[3] = levels;
  output[4] = 2;
  output[5] = 2;
  output[6] = 1;
  output[7] = 0xfeed0002;
}
)";

constexpr std::string_view kTexture2DShader = R"(
Texture2D<uint> input_texture : register(t0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint width, height, levels;
  input_texture.GetDimensions(1, width, height, levels);
  output[0] = input_texture.Load(int3(2, 1, 1));
  output[1] = width;
  output[2] = height;
  output[3] = levels;
  output[4] = 2;
  output[5] = 1;
  output[6] = 1;
  output[7] = 0xfeed0003;
}
)";

constexpr std::string_view kTexture2DArrayShader = R"(
Texture2DArray<uint> input_texture : register(t0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint width, height, elements, levels;
  input_texture.GetDimensions(1, width, height, elements, levels);
  output[0] = input_texture.Load(int4(2, 1, 2, 1));
  output[1] = width;
  output[2] = height;
  output[3] = elements;
  output[4] = levels;
  output[5] = 2;
  output[6] = 1;
  output[7] = 0xfeed0004;
}
)";

constexpr std::string_view kTexture3DShader = R"(
Texture3D<uint> input_texture : register(t0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint width, height, depth, levels;
  input_texture.GetDimensions(1, width, height, depth, levels);
  output[0] = input_texture.Load(int4(2, 1, 1, 1));
  output[1] = width;
  output[2] = height;
  output[3] = depth;
  output[4] = levels;
  output[5] = 2;
  output[6] = 1;
  output[7] = 0xfeed0005;
}
)";

constexpr std::string_view kTextureCubeShader = R"(
TextureCube<float> input_texture : register(t0);
SamplerState point_sampler : register(s0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint width, height, levels;
  input_texture.GetDimensions(1, width, height, levels);
  output[0] = asuint(input_texture.SampleLevel(point_sampler,
                                               float3(1.0, 0.0, 0.0), 1.0));
  output[1] = width;
  output[2] = height;
  output[3] = levels;
  output[4] = 1;
  output[5] = 0;
  output[6] = 0;
  output[7] = 0xfeed0006;
}
)";

constexpr std::string_view kTextureCubeArrayShader = R"(
TextureCubeArray<float> input_texture : register(t0);
SamplerState point_sampler : register(s0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint width, height, elements, levels;
  input_texture.GetDimensions(1, width, height, elements, levels);
  output[0] = asuint(input_texture.SampleLevel(
      point_sampler, float4(1.0, 0.0, 0.0, 1.0), 1.0));
  output[1] = width;
  output[2] = height;
  output[3] = elements;
  output[4] = levels;
  output[5] = 1;
  output[6] = 6;
  output[7] = 0xfeed0007;
}
)";

constexpr std::string_view kTexture2DMSShader = R"(
Texture2DMS<float> input_texture : register(t0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint width, height, samples;
  input_texture.GetDimensions(width, height, samples);
  output[0] = asuint(input_texture.Load(int2(2, 1), 3));
  output[1] = width;
  output[2] = height;
  output[3] = samples;
  output[4] = 2;
  output[5] = 1;
  output[6] = 3;
  output[7] = 0xfeed0008;
}
)";

constexpr std::string_view kTexture2DMSArrayShader = R"(
Texture2DMSArray<float> input_texture : register(t0);
RWBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint width, height, elements, samples;
  input_texture.GetDimensions(width, height, elements, samples);
  output[0] = asuint(input_texture.Load(int3(2, 1, 2), 3));
  output[1] = width;
  output[2] = height;
  output[3] = elements;
  output[4] = samples;
  output[5] = 2;
  output[6] = 3;
  output[7] = 0xfeed0009;
}
)";

constexpr std::array<TextureDimensionCase, 9> kCases = {{
    {TextureDimension::Texture1D,
     "Texture1D",
     kTexture1DShader,
     {0x11000102u, 4u, 3u, 0x1du, 2u, 1u, 0x1d1du, 0xfeed0001u}},
    {TextureDimension::Texture1DArray,
     "Texture1DArray",
     kTexture1DArrayShader,
     {0x12020102u, 4u, 3u, 3u, 2u, 2u, 1u, 0xfeed0002u}},
    {TextureDimension::Texture2D,
     "Texture2D",
     kTexture2DShader,
     {0x21000106u, 4u, 2u, 3u, 2u, 1u, 1u, 0xfeed0003u}},
    {TextureDimension::Texture2DArray,
     "Texture2DArray",
     kTexture2DArrayShader,
     {0x22020106u, 4u, 2u, 3u, 3u, 2u, 1u, 0xfeed0004u}},
    {TextureDimension::Texture3D,
     "Texture3D",
     kTexture3DShader,
     {0x3101010eu, 4u, 2u, 2u, 3u, 2u, 1u, 0xfeed0005u}},
    {TextureDimension::TextureCube,
     "TextureCube",
     kTextureCubeShader,
     {0x3f400000u, 2u, 2u, 2u, 1u, 0u, 0u, 0xfeed0006u}},
    {TextureDimension::TextureCubeArray,
     "TextureCubeArray",
     kTextureCubeArrayShader,
     {0x3f600000u, 2u, 2u, 2u, 2u, 1u, 6u, 0xfeed0007u}},
    {TextureDimension::Texture2DMS,
     "Texture2DMS",
     kTexture2DMSShader,
     {0x3f200000u, 4u, 3u, 4u, 2u, 1u, 3u, 0xfeed0008u}},
    {TextureDimension::Texture2DMSArray,
     "Texture2DMSArray",
     kTexture2DMSArrayShader,
     {0x3f000000u, 4u, 3u, 3u, 4u, 2u, 3u, 0xfeed0009u}},
}};

HRESULT CreateOutput(ID3D11Device *device, ID3D11Buffer **buffer,
                     ID3D11UnorderedAccessView **uav) {
  constexpr std::array<UINT, 8> kPoison = {
      0xc33ca55au, 0xc33ca55au, 0xc33ca55au, 0xc33ca55au,
      0xc33ca55au, 0xc33ca55au, 0xc33ca55au, 0xc33ca55au,
  };
  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = sizeof(kPoison);
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = kPoison.data();
  HRESULT hr = device->CreateBuffer(&buffer_desc, &initial, buffer);
  if (FAILED(hr))
    return hr;

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_R32_UINT;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = kPoison.size();
  return device->CreateUnorderedAccessView(*buffer, &uav_desc, uav);
}

HRESULT ReadOutput(ID3D11Device *device, ID3D11DeviceContext *context,
                   ID3D11Buffer *source, std::array<UINT, 8> *values) {
  D3D11_BUFFER_DESC desc = {};
  source->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Buffer> staging;
  HRESULT hr = device->CreateBuffer(&desc, nullptr, staging.put());
  if (FAILED(hr))
    return hr;
  context->CopyResource(staging.get(), source);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;
  std::memcpy(values->data(), mapped.pData, sizeof(*values));
  context->Unmap(staging.get(), 0);
  return S_OK;
}

std::vector<UINT> UintPattern(UINT count, UINT base) {
  std::vector<UINT> values(count);
  for (UINT i = 0; i < count; ++i)
    values[i] = base + i;
  return values;
}

HRESULT CreateTexture1DSrv(ID3D11Device *device, ID3D11DeviceContext *context,
                           bool array, ID3D11ShaderResourceView **srv) {
  D3D11_TEXTURE1D_DESC desc = {};
  desc.Width = 8;
  desc.MipLevels = 3;
  desc.ArraySize = array ? 3 : 1;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture1D> texture;
  HRESULT hr = device->CreateTexture1D(&desc, nullptr, texture.put());
  if (FAILED(hr))
    return hr;

  const UINT slice = array ? 2 : 0;
  const UINT base = array ? 0x12020100u : 0x11000100u;
  const auto values = UintPattern(4, base);
  context->UpdateSubresource(texture.get(),
                             D3D11CalcSubresource(1, slice, desc.MipLevels),
                             nullptr, values.data(), 0, 0);
  return device->CreateShaderResourceView(texture.get(), nullptr, srv);
}

HRESULT CreateTexture2DSrv(ID3D11Device *device, ID3D11DeviceContext *context,
                           bool array, ID3D11ShaderResourceView **srv) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 8;
  desc.Height = 4;
  desc.MipLevels = 3;
  desc.ArraySize = array ? 3 : 1;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, texture.put());
  if (FAILED(hr))
    return hr;

  const UINT slice = array ? 2 : 0;
  const UINT base = array ? 0x22020100u : 0x21000100u;
  const auto values = UintPattern(8, base);
  context->UpdateSubresource(texture.get(),
                             D3D11CalcSubresource(1, slice, desc.MipLevels),
                             nullptr, values.data(), 4 * sizeof(UINT), 0);
  return device->CreateShaderResourceView(texture.get(), nullptr, srv);
}

HRESULT CreateTexture3DSrv(ID3D11Device *device, ID3D11DeviceContext *context,
                           ID3D11ShaderResourceView **srv) {
  D3D11_TEXTURE3D_DESC desc = {};
  desc.Width = 8;
  desc.Height = 4;
  desc.Depth = 4;
  desc.MipLevels = 3;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture3D> texture;
  HRESULT hr = device->CreateTexture3D(&desc, nullptr, texture.put());
  if (FAILED(hr))
    return hr;

  const auto values = UintPattern(16, 0x31010100u);
  context->UpdateSubresource(texture.get(), 1, nullptr, values.data(),
                             4 * sizeof(UINT), 8 * sizeof(UINT));
  return device->CreateShaderResourceView(texture.get(), nullptr, srv);
}

HRESULT CreateCubeSrv(ID3D11Device *device, ID3D11DeviceContext *context,
                      bool array, ID3D11ShaderResourceView **srv) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 4;
  desc.Height = 4;
  desc.MipLevels = 2;
  desc.ArraySize = array ? 12 : 6;
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
  ComPtr<ID3D11Texture2D> texture;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, texture.put());
  if (FAILED(hr))
    return hr;

  const UINT face = array ? 6 : 0;
  const std::array<float, 4> values = {
      array ? 0.875f : 0.75f,
      array ? 0.875f : 0.75f,
      array ? 0.875f : 0.75f,
      array ? 0.875f : 0.75f,
  };
  context->UpdateSubresource(texture.get(),
                             D3D11CalcSubresource(1, face, desc.MipLevels),
                             nullptr, values.data(), 2 * sizeof(float), 0);

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = desc.Format;
  if (array) {
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    srv_desc.TextureCubeArray.MostDetailedMip = 0;
    srv_desc.TextureCubeArray.MipLevels = desc.MipLevels;
    srv_desc.TextureCubeArray.First2DArrayFace = 0;
    srv_desc.TextureCubeArray.NumCubes = 2;
  } else {
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srv_desc.TextureCube.MostDetailedMip = 0;
    srv_desc.TextureCube.MipLevels = desc.MipLevels;
  }
  return device->CreateShaderResourceView(texture.get(), &srv_desc, srv);
}

HRESULT CreateMsaaSrv(ID3D11Device *device, ID3D11DeviceContext *context,
                      bool array, ID3D11ShaderResourceView **srv) {
  UINT quality_levels = 0;
  HRESULT hr = device->CheckMultisampleQualityLevels(DXGI_FORMAT_R32_FLOAT, 4,
                                                     &quality_levels);
  if (FAILED(hr) || quality_levels == 0)
    return FAILED(hr) ? hr : E_NOTIMPL;

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 4;
  desc.Height = 3;
  desc.MipLevels = 1;
  desc.ArraySize = array ? 3 : 1;
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  desc.SampleDesc.Count = 4;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> texture;
  hr = device->CreateTexture2D(&desc, nullptr, texture.put());
  if (FAILED(hr))
    return hr;

  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = desc.Format;
  if (array) {
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
    rtv_desc.Texture2DMSArray.FirstArraySlice = 2;
    rtv_desc.Texture2DMSArray.ArraySize = 1;
  } else {
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
  }
  ComPtr<ID3D11RenderTargetView> rtv;
  hr = device->CreateRenderTargetView(texture.get(), &rtv_desc, rtv.put());
  if (FAILED(hr))
    return hr;
  const float color[4] = {array ? 0.5f : 0.625f, 0.0f, 0.0f, 0.0f};
  context->ClearRenderTargetView(rtv.get(), color);
  return device->CreateShaderResourceView(texture.get(), nullptr, srv);
}

HRESULT CreateInputSrv(ID3D11Device *device, ID3D11DeviceContext *context,
                       TextureDimension dimension,
                       ID3D11ShaderResourceView **srv) {
  switch (dimension) {
  case TextureDimension::Texture1D:
    return CreateTexture1DSrv(device, context, false, srv);
  case TextureDimension::Texture1DArray:
    return CreateTexture1DSrv(device, context, true, srv);
  case TextureDimension::Texture2D:
    return CreateTexture2DSrv(device, context, false, srv);
  case TextureDimension::Texture2DArray:
    return CreateTexture2DSrv(device, context, true, srv);
  case TextureDimension::Texture3D:
    return CreateTexture3DSrv(device, context, srv);
  case TextureDimension::TextureCube:
    return CreateCubeSrv(device, context, false, srv);
  case TextureDimension::TextureCubeArray:
    return CreateCubeSrv(device, context, true, srv);
  case TextureDimension::Texture2DMS:
    return CreateMsaaSrv(device, context, false, srv);
  case TextureDimension::Texture2DMSArray:
    return CreateMsaaSrv(device, context, true, srv);
  }
  return E_INVALIDARG;
}

class D3D11TextureDimensionMatrixSpec
    : public ::testing::TestWithParam<TextureDimensionCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D11TestContext context_;
};

TEST_P(D3D11TextureDimensionMatrixSpec,
       ReadsSelectedSubresourceAndReportsDimensions) {
  const auto &test = GetParam();
  const auto compilation = CompileShader(test.shader, "cs_5_0");
  ASSERT_EQ(compilation.result, S_OK) << compilation.diagnostic_text();
  ComPtr<ID3D11ComputeShader> shader;
  ASSERT_EQ(context_.device()->CreateComputeShader(
                compilation.bytecode->GetBufferPointer(),
                compilation.bytecode->GetBufferSize(), nullptr, shader.put()),
            S_OK);

  ComPtr<ID3D11ShaderResourceView> srv;
  ASSERT_EQ(CreateInputSrv(context_.device(), context_.context(),
                           test.dimension, srv.put()),
            S_OK);
  ComPtr<ID3D11Buffer> output;
  ComPtr<ID3D11UnorderedAccessView> output_uav;
  ASSERT_EQ(CreateOutput(context_.device(), output.put(), output_uav.put()),
            S_OK);
  ComPtr<ID3D11SamplerState> sampler;
  if (test.dimension == TextureDimension::TextureCube ||
      test.dimension == TextureDimension::TextureCubeArray) {
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    ASSERT_EQ(context_.device()->CreateSamplerState(&desc, sampler.put()),
              S_OK);
  }

  ID3D11ShaderResourceView *srvs[] = {srv.get()};
  ID3D11UnorderedAccessView *uavs[] = {output_uav.get()};
  ID3D11SamplerState *samplers[] = {sampler.get()};
  context_.context()->CSSetShader(shader.get(), nullptr, 0);
  context_.context()->CSSetShaderResources(0, 1, srvs);
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
  if (sampler)
    context_.context()->CSSetSamplers(0, 1, samplers);
  context_.context()->Dispatch(1, 1, 1);

  ID3D11ShaderResourceView *null_srv = nullptr;
  ID3D11UnorderedAccessView *null_uav = nullptr;
  ID3D11SamplerState *null_sampler = nullptr;
  context_.context()->CSSetShaderResources(0, 1, &null_srv);
  context_.context()->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
  context_.context()->CSSetSamplers(0, 1, &null_sampler);
  context_.context()->CSSetShader(nullptr, nullptr, 0);

  std::array<UINT, 8> actual = {};
  ASSERT_EQ(
      ReadOutput(context_.device(), context_.context(), output.get(), &actual),
      S_OK);
  EXPECT_EQ(actual, test.expected);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string TextureDimensionName(
    const ::testing::TestParamInfo<TextureDimensionCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(AllDimensions, D3D11TextureDimensionMatrixSpec,
                         ::testing::ValuesIn(kCases), TextureDimensionName);

} // namespace
