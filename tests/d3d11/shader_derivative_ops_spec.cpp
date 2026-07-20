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
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
  return float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0),
                0.0, 1.0);
}
)";

constexpr std::string_view kDirectionalDerivativeShader = R"(
float4 main(float4 position : SV_Position) : SV_Target {
  float horizontal = position.x * 2.0 + 5.0;
  float vertical = position.y * 3.0 + 7.0;
  return float4(ddx(horizontal), ddy(horizontal),
                ddx(vertical), ddy(vertical));
}
)";

constexpr std::string_view kCoarseFineDerivativeShader = R"(
float4 main(float4 position : SV_Position) : SV_Target {
  float gradient = position.x * 2.0 + position.y * 3.0;
  return float4(ddx_coarse(gradient), ddy_coarse(gradient),
                ddx_fine(gradient), ddy_fine(gradient));
}
)";

constexpr std::string_view kFlattenedDivergentDerivativeShader = R"(
float4 main(float4 position : SV_Position) : SV_Target {
  uint checker = (uint(position.x) ^ uint(position.y)) & 1;
  [flatten]
  if (checker == 0)
    return float4(ddx(position.x * 2.0),
                  ddy(position.y * 3.0), 1.0, 0.0);
  return float4(ddx(position.x * 4.0) * 0.5,
                ddy(position.y * 6.0) * 0.5, 0.0, 1.0);
}
)";

constexpr std::string_view kHelperLaneDerivativeShader = R"(
float4 main(float4 position : SV_Position) : SV_Target {
  clip(min(position.x - 1.0, position.y - 1.0));
  return float4(ddx(position.x), ddy(position.y),
                fwidth(position.x + position.y), 1.0);
}
)";

HRESULT CreateTarget(ID3D11Device *device, UINT width, UINT height,
                     ID3D11Texture2D **texture, ID3D11RenderTargetView **rtv) {
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

HRESULT ReadTarget(ID3D11Device *device, ID3D11DeviceContext *context,
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

void ExpectFloat4(const Float4 &actual, const Float4 &expected, UINT x,
                  UINT y) {
  for (std::size_t component = 0; component < actual.size(); ++component) {
    EXPECT_FLOAT_EQ(actual[component], expected[component])
        << "pixel (" << x << ", " << y << ") component " << component;
  }
}

class D3D11ShaderDerivativeOpsSpec : public ::testing::Test {
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

  std::vector<Float4> Run(std::string_view source, UINT width = 8,
                          UINT height = 8) {
    auto pixel_shader = CompilePixelShader(source);
    EXPECT_TRUE(pixel_shader);
    if (!pixel_shader)
      return {};
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> rtv;
    EXPECT_EQ(
        CreateTarget(context_.device(), width, height, target.put(), rtv.put()),
        S_OK);
    if (!target || !rtv)
      return {};

    const float clear[4] = {-100.0f, -100.0f, -100.0f, -100.0f};
    context_.context()->ClearRenderTargetView(rtv.get(), clear);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    ID3D11RenderTargetView *rtvs[] = {rtv.get()};
    context_.context()->OMSetRenderTargets(1, rtvs, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
    context_.context()->PSSetShader(nullptr, nullptr, 0);

    std::vector<Float4> pixels;
    EXPECT_EQ(ReadTarget(context_.device(), context_.context(), target.get(),
                         &pixels),
              S_OK);
    return pixels;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
};

TEST_F(D3D11ShaderDerivativeOpsSpec,
       DdxAndDdyReportHorizontalAndVerticalGradients) {
  const auto pixels = Run(kDirectionalDerivativeShader);
  ASSERT_EQ(pixels.size(), 64u);
  for (UINT y = 0; y < 8; ++y) {
    for (UINT x = 0; x < 8; ++x)
      ExpectFloat4(pixels[y * 8 + x], {2.0f, 0.0f, 0.0f, 3.0f}, x, y);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ShaderDerivativeOpsSpec,
       CoarseAndFineDerivativesReportLinearGradient) {
  const auto pixels = Run(kCoarseFineDerivativeShader);
  ASSERT_EQ(pixels.size(), 64u);
  for (UINT y = 0; y < 8; ++y) {
    for (UINT x = 0; x < 8; ++x)
      ExpectFloat4(pixels[y * 8 + x], {2.0f, 3.0f, 2.0f, 3.0f}, x, y);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ShaderDerivativeOpsSpec,
       FlattenedCheckerboardBranchesKeepDeterministicDerivatives) {
  const auto pixels = Run(kFlattenedDivergentDerivativeShader);
  ASSERT_EQ(pixels.size(), 64u);
  for (UINT y = 0; y < 8; ++y) {
    for (UINT x = 0; x < 8; ++x) {
      const bool even = ((x ^ y) & 1u) == 0;
      ExpectFloat4(pixels[y * 8 + x],
                   even ? Float4{2.0f, 3.0f, 1.0f, 0.0f}
                        : Float4{2.0f, 3.0f, 0.0f, 1.0f},
                   x, y);
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ShaderDerivativeOpsSpec,
       DiscardedQuadNeighborsRemainHelperLanesForFwidth) {
  const auto pixels = Run(kHelperLaneDerivativeShader);
  ASSERT_EQ(pixels.size(), 64u);
  for (UINT y = 0; y < 8; ++y) {
    for (UINT x = 0; x < 8; ++x) {
      const Float4 expected = x == 0 || y == 0
                                  ? Float4{-100.0f, -100.0f, -100.0f, -100.0f}
                                  : Float4{1.0f, 1.0f, 2.0f, 1.0f};
      ExpectFloat4(pixels[y * 8 + x], expected, x, y);
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
