#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string_view>
#include <vector>

// D3D11 MSAA execution controls through public D3D11 and HLSL APIs only.
// Every test owns its device and resources, so the cases are parallel-safe.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kSize = 8;
constexpr UINT kSampleCount = 4;

constexpr std::string_view kFullscreenVertexShader = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  const float2 positions[3] = {
    float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
  };
  return float4(positions[vertex_id], 0.5, 1.0);
}
)";

constexpr std::string_view kWhitePixelShader = R"(
float4 main() : SV_Target {
  return 1.0;
}
)";

constexpr std::string_view kSampleIndexPixelShader = R"(
float4 main(uint sample_index : SV_SampleIndex) : SV_Target {
  return float4((sample_index + 1) * 0.25, 0.0, 0.0, 1.0);
}
)";

constexpr std::string_view kCoverageOutputPixelShader = R"(
struct Output {
  float4 color : SV_Target;
  uint coverage : SV_Coverage;
};

Output main() {
  Output output;
  output.color = float4(0.0, 1.0, 0.0, 1.0);
  output.coverage = 0x5;
  return output;
}
)";

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

bool ColorMatches(std::uint32_t actual, std::uint32_t expected,
                  unsigned tolerance) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    const int actual_channel = (actual >> shift) & 0xff;
    const int expected_channel = (expected >> shift) & 0xff;
    if (std::abs(actual_channel - expected_channel) >
        static_cast<int>(tolerance))
      return false;
  }
  return true;
}

HRESULT ReadbackRgba8(ID3D11Device *device, ID3D11DeviceContext *context,
                      ID3D11Texture2D *texture,
                      std::vector<std::uint32_t> *pixels) {
  if (!device || !context || !texture || !pixels)
    return E_INVALIDARG;

  D3D11_TEXTURE2D_DESC staging_desc = {};
  texture->GetDesc(&staging_desc);
  if (staging_desc.SampleDesc.Count != 1 || staging_desc.ArraySize != 1 ||
      staging_desc.MipLevels != 1)
    return E_INVALIDARG;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;

  ComPtr<ID3D11Texture2D> staging;
  HRESULT hr = device->CreateTexture2D(&staging_desc, nullptr, staging.put());
  if (FAILED(hr))
    return hr;
  context->CopyResource(staging.get(), texture);

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;

  pixels->assign(
      static_cast<std::size_t>(staging_desc.Width) * staging_desc.Height, 0u);
  for (UINT y = 0; y < staging_desc.Height; ++y) {
    for (UINT x = 0; x < staging_desc.Width; ++x) {
      std::memcpy(&(*pixels)[y * staging_desc.Width + x],
                  static_cast<const std::uint8_t *>(mapped.pData) +
                      y * mapped.RowPitch + x * sizeof(std::uint32_t),
                  sizeof(std::uint32_t));
    }
  }
  context->Unmap(staging.get(), 0);
  return S_OK;
}

class D3D11MsaaExecutionOpsSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    UINT quality_levels = 0;
    const HRESULT query_hr = context_.device()->CheckMultisampleQualityLevels(
        DXGI_FORMAT_R8G8B8A8_UNORM, kSampleCount, &quality_levels);
    if (FAILED(query_hr) || quality_levels == 0)
      GTEST_SKIP() << "4x R8G8B8A8_UNORM MSAA is unavailable";

    const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));

    D3D11_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    rasterizer_desc.MultisampleEnable = TRUE;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
        &rasterizer_desc, rasterizer_.put())));
  }

  std::vector<std::uint32_t> RenderAndResolve(std::string_view pixel_source,
                                              UINT sample_mask) {
    const auto pixel = CompileShader(pixel_source, "ps_5_0");
    EXPECT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();
    if (FAILED(pixel.result))
      return {};

    ComPtr<ID3D11PixelShader> pixel_shader;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader.put())));
    if (!pixel_shader)
      return {};

    D3D11_TEXTURE2D_DESC msaa_desc = {};
    msaa_desc.Width = kSize;
    msaa_desc.Height = kSize;
    msaa_desc.MipLevels = 1;
    msaa_desc.ArraySize = 1;
    msaa_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msaa_desc.SampleDesc.Count = kSampleCount;
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
    resolved_desc.BindFlags = 0;
    ComPtr<ID3D11Texture2D> resolved;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
        &resolved_desc, nullptr, resolved.put())));
    if (!msaa_target || !target_view || !resolved)
      return {};

    constexpr float black[4] = {};
    context_.context()->ClearRenderTargetView(target_view.get(), black);
    ID3D11RenderTargetView *views[] = {target_view.get()};
    const D3D11_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
        0.0f, 1.0f};
    context_.context()->OMSetRenderTargets(1, views, nullptr);
    context_.context()->OMSetBlendState(nullptr, nullptr, sample_mask);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->RSSetState(rasterizer_.get());
    context_.context()->IASetInputLayout(nullptr);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
    context_.context()->ResolveSubresource(resolved.get(), 0, msaa_target.get(),
                                           0, DXGI_FORMAT_R8G8B8A8_UNORM);

    std::vector<std::uint32_t> pixels;
    EXPECT_TRUE(HResultSucceeded(ReadbackRgba8(
        context_.device(), context_.context(), resolved.get(), &pixels)));
    return pixels;
  }

  void ExpectSolid(const std::vector<std::uint32_t> &pixels,
                   std::uint32_t expected, unsigned tolerance) {
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(kSize) * kSize);
    for (UINT y = 0; y < kSize; ++y) {
      for (UINT x = 0; x < kSize; ++x) {
        const std::uint32_t actual = pixels[y * kSize + x];
        EXPECT_TRUE(ColorMatches(actual, expected, tolerance))
            << "pixel (" << x << ", " << y << ") actual=0x" << std::hex
            << actual << " expected=0x" << expected;
      }
    }
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
};

TEST_F(D3D11MsaaExecutionOpsSpec, SampleMaskControlsResolvedCoverage) {
  const auto pixels = RenderAndResolve(kWhitePixelShader, 0x5u);
  ExpectSolid(pixels, 0x80808080u, 2);
}

TEST_F(D3D11MsaaExecutionOpsSpec,
       SampleIndexShaderInputForcesPerSampleShading) {
  const auto pixels = RenderAndResolve(kSampleIndexPixelShader, 0xffffffffu);
  ExpectSolid(pixels, 0xff00009fu, 2);
}

TEST_F(D3D11MsaaExecutionOpsSpec, CoverageShaderOutputLimitsWrittenSamples) {
  const auto pixels = RenderAndResolve(kCoverageOutputPixelShader, 0xffffffffu);
  ExpectSolid(pixels, 0x80008000u, 2);
}

} // namespace
