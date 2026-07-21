#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string_view>
#include <vector>

// Public D3D11 interpolation execution coverage. Tests use independent
// devices and resources and are safe under the default parallel scheduler.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kSize = 16;
using Float4 = std::array<float, 4>;

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

HRESULT ReadFloatTarget(ID3D11Device *device, ID3D11DeviceContext *context,
                        ID3D11Texture2D *texture, std::vector<Float4> *pixels) {
  if (!device || !context || !texture || !pixels)
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
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
  pixels->resize(static_cast<size_t>(desc.Width) * desc.Height);
  for (UINT y = 0; y < desc.Height; ++y) {
    std::memcpy(pixels->data() + static_cast<size_t>(y) * desc.Width,
                static_cast<const uint8_t *>(mapped.pData) +
                    static_cast<size_t>(y) * mapped.RowPitch,
                static_cast<size_t>(desc.Width) * sizeof(Float4));
  }
  context->Unmap(staging.get(), 0);
  return S_OK;
}

HRESULT ReadRgba8Target(ID3D11Device *device, ID3D11DeviceContext *context,
                        ID3D11Texture2D *texture,
                        std::vector<uint32_t> *pixels) {
  if (!device || !context || !texture || !pixels)
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
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
  pixels->resize(static_cast<size_t>(desc.Width) * desc.Height);
  for (UINT y = 0; y < desc.Height; ++y) {
    std::memcpy(pixels->data() + static_cast<size_t>(y) * desc.Width,
                static_cast<const uint8_t *>(mapped.pData) +
                    static_cast<size_t>(y) * mapped.RowPitch,
                static_cast<size_t>(desc.Width) * sizeof(uint32_t));
  }
  context->Unmap(staging.get(), 0);
  return S_OK;
}

D3D11_RASTERIZER_DESC RasterizerDesc() {
  D3D11_RASTERIZER_DESC desc = {};
  desc.FillMode = D3D11_FILL_SOLID;
  desc.CullMode = D3D11_CULL_NONE;
  desc.DepthClipEnable = TRUE;
  return desc;
}

struct InterpolationVertex {
  float position[4];
  float value;
  float flat;
};

constexpr std::string_view kInterpolationVertexShader = R"(
struct Input {
  float4 position : POSITION;
  float value : VALUE0;
  float flat : VALUE1;
};
struct Output {
  float4 position : SV_Position;
  linear float perspective_value : TEXCOORD0;
  noperspective float affine_value : TEXCOORD1;
  nointerpolation float flat_value : TEXCOORD2;
  noperspective centroid float centroid_value : TEXCOORD3;
};

Output main(Input input) {
  Output output;
  output.position = input.position;
  output.perspective_value = input.value;
  output.affine_value = input.value;
  output.flat_value = input.flat;
  output.centroid_value = input.value;
  return output;
}
)";

constexpr std::string_view kInterpolationPixelShader = R"(
struct Input {
  float4 position : SV_Position;
  linear float perspective_value : TEXCOORD0;
  noperspective float affine_value : TEXCOORD1;
  nointerpolation float flat_value : TEXCOORD2;
  noperspective centroid float centroid_value : TEXCOORD3;
};

float4 main(Input input) : SV_Target {
  return float4(input.perspective_value, input.affine_value,
                input.flat_value, input.centroid_value);
}
)";

constexpr std::array<InterpolationVertex, 3> kInterpolationTriangle = {{
    {{-0.8f, -0.8f, 0.5f, 1.0f}, 0.0f, 0.125f},
    {{-1.6f, 1.6f, 1.0f, 2.0f}, 1.0f, 0.5f},
    {{3.2f, -3.2f, 2.0f, 4.0f}, 0.0f, 0.875f},
}};

constexpr UINT kInterpolationSampleX = 6;
constexpr UINT kInterpolationSampleY = 9;

class D3D11InterpolationOpsSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    const auto vertex = CompileShader(kInterpolationVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kInterpolationPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader_.put())));

    const std::array<D3D11_INPUT_ELEMENT_DESC, 3> elements = {{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         offsetof(InterpolationVertex, position), D3D11_INPUT_PER_VERTEX_DATA,
         0},
        {"VALUE", 0, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(InterpolationVertex, value), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"VALUE", 1, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(InterpolationVertex, flat), D3D11_INPUT_PER_VERTEX_DATA, 0},
    }};
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateInputLayout(
        elements.data(), elements.size(), vertex.bytecode->GetBufferPointer(),
        vertex.bytecode->GetBufferSize(), input_layout_.put())));
  }

  Float4 RenderSample() {
    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = sizeof(kInterpolationTriangle);
    buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA buffer_data = {};
    buffer_data.pSysMem = kInterpolationTriangle.data();
    ComPtr<ID3D11Buffer> vertices;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
        &buffer_desc, &buffer_data, vertices.put())));

    D3D11_TEXTURE2D_DESC target_desc = {};
    target_desc.Width = kSize;
    target_desc.Height = kSize;
    target_desc.MipLevels = 1;
    target_desc.ArraySize = 1;
    target_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    target_desc.SampleDesc.Count = 1;
    target_desc.Usage = D3D11_USAGE_DEFAULT;
    target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
        &target_desc, nullptr, target.put())));
    if (target) {
      EXPECT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
          target.get(), nullptr, target_view.put())));
    }

    const auto raster_desc = RasterizerDesc();
    ComPtr<ID3D11RasterizerState> rasterizer;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
        &raster_desc, rasterizer.put())));
    if (!vertices || !target_view || !rasterizer)
      return {};

    constexpr UINT stride = sizeof(InterpolationVertex);
    constexpr UINT offset = 0;
    ID3D11Buffer *buffers[] = {vertices.get()};
    ID3D11RenderTargetView *views[] = {target_view.get()};
    const D3D11_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
        0.0f, 1.0f};
    constexpr float clear[4] = {-100.0f, -100.0f, -100.0f, -100.0f};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);
    context_.context()->OMSetRenderTargets(1, views, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->RSSetState(rasterizer.get());
    context_.context()->IASetInputLayout(input_layout_.get());
    context_.context()->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

    std::vector<Float4> pixels;
    EXPECT_TRUE(HResultSucceeded(ReadFloatTarget(
        context_.device(), context_.context(), target.get(), &pixels)));
    if (pixels.size() != kSize * kSize)
      return {};
    return pixels[kInterpolationSampleY * kSize + kInterpolationSampleX];
  }

  static float AffineExpected() {
    const float ndc_y =
        1.0f - (static_cast<float>(kInterpolationSampleY) + 0.5f) * 2.0f /
                   static_cast<float>(kSize);
    return (ndc_y + 0.8f) / 1.6f;
  }

  static float PerspectiveExpected() {
    const float ndc_x = (static_cast<float>(kInterpolationSampleX) + 0.5f) *
                            2.0f / static_cast<float>(kSize) -
                        1.0f;
    const float lambda_b = AffineExpected();
    const float lambda_c = (ndc_x + 0.8f) / 1.6f;
    const float lambda_a = 1.0f - lambda_b - lambda_c;
    return (lambda_b / 2.0f) / (lambda_a + lambda_b / 2.0f + lambda_c / 4.0f);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11InputLayout> input_layout_;
};

TEST_F(D3D11InterpolationOpsSpec, LinearInterpolationIsPerspectiveCorrect) {
  const Float4 actual = RenderSample();
  EXPECT_NEAR(actual[0], PerspectiveExpected(), 1.0e-4f);
  EXPECT_GT(std::abs(actual[0] - actual[1]), 0.01f);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11InterpolationOpsSpec, NoperspectiveInterpolationUsesScreenWeights) {
  const Float4 actual = RenderSample();
  EXPECT_NEAR(actual[1], AffineExpected(), 1.0e-4f);
  EXPECT_GT(std::abs(actual[0] - actual[1]), 0.01f);
}

TEST_F(D3D11InterpolationOpsSpec, NointerpolationUsesLeadingVertexValue) {
  const Float4 actual = RenderSample();
  EXPECT_NEAR(actual[2], kInterpolationTriangle[0].flat, 1.0e-6f);
}

TEST_F(D3D11InterpolationOpsSpec,
       CentroidOnSingleSampleTargetMatchesPixelCenter) {
  const Float4 actual = RenderSample();
  EXPECT_NEAR(actual[3], AffineExpected(), 1.0e-4f);
}

constexpr std::string_view kSampleVertexShader = R"(
struct Output {
  float4 position : SV_Position;
  float ndc_x : TEXCOORD0;
};

Output main(uint vertex_id : SV_VertexID) {
  float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
  position = position * float2(2.0, -2.0) + float2(-1.0, 1.0);
  Output output;
  output.position = float4(position, 0.5, 1.0);
  output.ndc_x = position.x;
  return output;
}
)";

constexpr std::string_view kSamplePixelShader = R"(
struct Input {
  float4 position : SV_Position;
  sample float ndc_x : TEXCOORD0;
};

float4 main(Input input) : SV_Target {
  return float4(input.ndc_x > 0.0625 ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}
)";

class D3D11MsaaInterpolationSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    UINT quality_levels = 0;
    const HRESULT hr = context_.device()->CheckMultisampleQualityLevels(
        DXGI_FORMAT_R8G8B8A8_UNORM, 4, &quality_levels);
    if (FAILED(hr) || quality_levels == 0)
      GTEST_SKIP() << "4x R8G8B8A8_UNORM MSAA is not supported";
  }

  std::vector<uint32_t> RenderAndResolve(std::string_view vertex_source,
                                         std::string_view pixel_source) {
    const auto vertex = CompileShader(vertex_source, "vs_5_0");
    const auto pixel = CompileShader(pixel_source, "ps_5_0");
    EXPECT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    EXPECT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();
    if (FAILED(vertex.result) || FAILED(pixel.result))
      return {};

    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader.put())));
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader.put())));

    D3D11_TEXTURE2D_DESC msaa_desc = {};
    msaa_desc.Width = kSize;
    msaa_desc.Height = kSize;
    msaa_desc.MipLevels = 1;
    msaa_desc.ArraySize = 1;
    msaa_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msaa_desc.SampleDesc.Count = 4;
    msaa_desc.SampleDesc.Quality = 0;
    msaa_desc.Usage = D3D11_USAGE_DEFAULT;
    msaa_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> msaa_target;
    ComPtr<ID3D11RenderTargetView> target_view;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
        &msaa_desc, nullptr, msaa_target.put())));
    if (msaa_target) {
      EXPECT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
          msaa_target.get(), nullptr, target_view.put())));
    }

    D3D11_TEXTURE2D_DESC resolved_desc = msaa_desc;
    resolved_desc.SampleDesc.Count = 1;
    resolved_desc.SampleDesc.Quality = 0;
    resolved_desc.BindFlags = 0;
    ComPtr<ID3D11Texture2D> resolved;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
        &resolved_desc, nullptr, resolved.put())));

    const auto raster_desc = RasterizerDesc();
    ComPtr<ID3D11RasterizerState> rasterizer;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
        &raster_desc, rasterizer.put())));
    if (!vertex_shader || !pixel_shader || !target_view || !resolved ||
        !rasterizer)
      return {};

    constexpr float clear[4] = {};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);
    ID3D11RenderTargetView *views[] = {target_view.get()};
    const D3D11_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
        0.0f, 1.0f};
    context_.context()->OMSetRenderTargets(1, views, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->RSSetState(rasterizer.get());
    context_.context()->IASetInputLayout(nullptr);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
    context_.context()->ResolveSubresource(resolved.get(), 0, msaa_target.get(),
                                           0, DXGI_FORMAT_R8G8B8A8_UNORM);

    std::vector<uint32_t> pixels;
    EXPECT_TRUE(HResultSucceeded(ReadRgba8Target(
        context_.device(), context_.context(), resolved.get(), &pixels)));
    return pixels;
  }

  D3D11TestContext context_;
};

TEST_F(D3D11MsaaInterpolationSpec,
       SampleModifierEvaluatesSeparatelyAtSampleLocations) {
  const auto probe = CompileShader(kSamplePixelShader, "ps_5_0");
  // Wine's bundled compiler does not parse this Shader Model 4.1 modifier.
  // The Windows oracle uses native D3DCompile and executes the assertions.
  if (FAILED(probe.result) &&
      probe.diagnostic_text().find("Unknown modifier \"sample\"") !=
          std::string_view::npos) {
    GTEST_SKIP() << "Wine D3DCompile does not support the HLSL sample "
                    "interpolation modifier";
  }
  const auto pixels = RenderAndResolve(kSampleVertexShader, kSamplePixelShader);
  ASSERT_EQ(pixels.size(), kSize * kSize);
  const unsigned red = pixels[8 * kSize + 8] & 0xffu;
  EXPECT_GT(red, 64u);
  EXPECT_LT(red, 192u);
}

} // namespace
