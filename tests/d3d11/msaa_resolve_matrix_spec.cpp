#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// D3D11 ResolveSubresource MSAA sample-count matrix.
// Public D3D11 / DXGI / d3dcompiler API only (D3D11TestContext + CompileShader).
// Real draws into MSAA RTs, ResolveSubresource to single-sample, staging readback.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kRtWidth = 8;
constexpr UINT kRtHeight = 8;

// Fullscreen triangle via SV_VertexID (ids 0,1,2).
constexpr std::string_view kFullscreenVertexShader = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
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

// Solid color from cbuffer so clear vs draw paths can share the same expected
// packing without recompiling per color.
constexpr std::string_view kConstantColorPixelShader = R"(
cbuffer ColorConstants : register(b0) {
  float4 color;
};
float4 main() : SV_Target {
  return color;
}
)";

// R8G8B8A8 little-endian packed as 0xAABBGGRR.
constexpr uint32_t kRed = 0xff0000ff;
constexpr uint32_t kGreen = 0xff00ff00;
constexpr uint32_t kBlue = 0xffff0000;
constexpr uint32_t kSolid = 0xffbf8040; // float4(0.25, 0.5, 0.75, 1.0)

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

HRESULT ReadbackRgba8(ID3D11Device *device, ID3D11DeviceContext *context,
                      ID3D11Texture2D *texture, UINT subresource,
                      std::vector<uint32_t> *pixels, UINT *out_width,
                      UINT *out_height) {
  if (!device || !context || !texture || !pixels)
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  if (desc.SampleDesc.Count != 1)
    return E_INVALIDARG;

  D3D11_TEXTURE2D_DESC staging_desc = desc;
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
  hr = context->Map(staging.get(), subresource, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(hr))
    return hr;

  pixels->assign(static_cast<size_t>(desc.Width) * desc.Height, 0u);
  for (UINT y = 0; y < desc.Height; ++y) {
    for (UINT x = 0; x < desc.Width; ++x) {
      std::memcpy(&(*pixels)[y * desc.Width + x],
                  static_cast<const uint8_t *>(mapped.pData) +
                      y * mapped.RowPitch + x * sizeof(uint32_t),
                  sizeof(uint32_t));
    }
  }
  context->Unmap(staging.get(), subresource);
  if (out_width)
    *out_width = desc.Width;
  if (out_height)
    *out_height = desc.Height;
  return S_OK;
}

// Create MSAA R8G8B8A8_UNORM RT + RTV. Caller treats CreateTexture2D failure
// as "unsupported sample count" (GTEST_SKIP that case only).
HRESULT CreateMsaaRenderTarget(ID3D11Device *device, UINT width, UINT height,
                               UINT sample_count, UINT array_size,
                               ID3D11Texture2D **texture,
                               ID3D11RenderTargetView **rtv) {
  if (!device || !texture || !rtv || sample_count < 2 || array_size < 1)
    return E_INVALIDARG;

  UINT quality_levels = 0;
  const HRESULT qhr = device->CheckMultisampleQualityLevels(
      DXGI_FORMAT_R8G8B8A8_UNORM, sample_count, &quality_levels);
  if (FAILED(qhr) || quality_levels == 0)
    return E_FAIL;

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = array_size;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = sample_count;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;

  ComPtr<ID3D11Texture2D> local;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, local.put());
  if (FAILED(hr))
    return hr;

  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (array_size == 1) {
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
  } else {
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
    rtv_desc.Texture2DMSArray.FirstArraySlice = 0;
    rtv_desc.Texture2DMSArray.ArraySize = 1; // slice 0 only
  }

  ComPtr<ID3D11RenderTargetView> local_rtv;
  hr = device->CreateRenderTargetView(local.get(), &rtv_desc, local_rtv.put());
  if (FAILED(hr))
    return hr;

  *texture = local.release();
  *rtv = local_rtv.release();
  return S_OK;
}

HRESULT CreateResolveDestination(ID3D11Device *device, UINT width, UINT height,
                                 UINT array_size, ID3D11Texture2D **texture) {
  if (!device || !texture || array_size < 1)
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = array_size;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = 0;
  return device->CreateTexture2D(&desc, nullptr, texture);
}

void ExpectSolidResolved(const std::vector<uint32_t> &pixels, UINT width,
                         UINT height, uint32_t expected, unsigned tolerance,
                         const char *label) {
  ASSERT_EQ(pixels.size(), static_cast<size_t>(width) * height);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const uint32_t actual = pixels[y * width + x];
      EXPECT_TRUE(ColorMatches(actual, expected, tolerance))
          << label << " pixel (" << x << "," << y << ") actual=0x" << std::hex
          << actual << " expected=0x" << expected;
    }
  }
}

// ---------------------------------------------------------------------------
// Sample-count matrix fixture (2 / 4 / 8)
// ---------------------------------------------------------------------------

class MsaaResolveSampleMatrixSpec : public ::testing::TestWithParam<UINT> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);

    const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kConstantColorPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();

    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader_.put())));

    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.ByteWidth = 16; // float4
    cb_desc.Usage = D3D11_USAGE_DEFAULT;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ASSERT_TRUE(HResultSucceeded(
        context_.device()->CreateBuffer(&cb_desc, nullptr, color_cb_.put())));

    D3D11_RASTERIZER_DESC rs_desc = {};
    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.CullMode = D3D11_CULL_NONE;
    rs_desc.DepthClipEnable = TRUE;
    rs_desc.MultisampleEnable = TRUE;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
        &rs_desc, rasterizer_.put())));
  }

  // CreateTexture2D failure for a sample count is reported via *hr so the
  // test body can GTEST_SKIP that case only.
  HRESULT CreateMsaaOrFail(UINT sample_count, UINT array_size,
                           ComPtr<ID3D11Texture2D> *msaa,
                           ComPtr<ID3D11RenderTargetView> *rtv) {
    return CreateMsaaRenderTarget(context_.device(), kRtWidth, kRtHeight,
                                  sample_count, array_size, msaa->put(),
                                  rtv->put());
  }

  void SetDrawColor(float r, float g, float b, float a) {
    const float color[4] = {r, g, b, a};
    context_.context()->UpdateSubresource(color_cb_.get(), 0, nullptr, color, 0,
                                          0);
  }

  void DrawFullscreenSolid(ID3D11RenderTargetView *rtv) {
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(kRtWidth);
    viewport.Height = static_cast<float>(kRtHeight);
    viewport.MaxDepth = 1.0f;
    ID3D11RenderTargetView *targets[] = {rtv};
    ID3D11Buffer *cbs[] = {color_cb_.get()};
    context_.context()->OMSetRenderTargets(1, targets, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->RSSetState(rasterizer_.get());
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
    context_.context()->PSSetConstantBuffers(0, 1, cbs);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11Buffer> color_cb_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
};

// 1) Sample count matrix: clear solid color → ResolveSubresource → solid color.
TEST_P(MsaaResolveSampleMatrixSpec, ClearThenResolveProducesSolidColor) {
  const UINT sample_count = GetParam();

  ComPtr<ID3D11Texture2D> msaa;
  ComPtr<ID3D11RenderTargetView> rtv;
  {
    const HRESULT msaa_hr = CreateMsaaOrFail(sample_count, 1, &msaa, &rtv);
    if (FAILED(msaa_hr)) {
      GTEST_SKIP() << sample_count << "x MSAA CreateTexture2D failed (0x"
                   << std::hex << static_cast<unsigned long>(msaa_hr)
                   << "); skipping case";
    }
  }

  ComPtr<ID3D11Texture2D> resolved;
  ASSERT_TRUE(HResultSucceeded(CreateResolveDestination(
      context_.device(), kRtWidth, kRtHeight, 1, resolved.put())));

  constexpr float clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  context_.context()->ClearRenderTargetView(rtv.get(), clear);
  context_.context()->ResolveSubresource(resolved.get(), 0, msaa.get(), 0,
                                         DXGI_FORMAT_R8G8B8A8_UNORM);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(
      ReadbackRgba8(context_.device(), context_.context(), resolved.get(), 0,
                    &pixels, &width, &height)));
  ASSERT_EQ(width, kRtWidth);
  ASSERT_EQ(height, kRtHeight);
  ExpectSolidResolved(pixels, width, height, kSolid, 1,
                      ("samples=" + std::to_string(sample_count) + " clear")
                          .c_str());
}

// 2) Resolve after a real draw (not only clear).
TEST_P(MsaaResolveSampleMatrixSpec, DrawThenResolveProducesSolidColor) {
  const UINT sample_count = GetParam();

  ComPtr<ID3D11Texture2D> msaa;
  ComPtr<ID3D11RenderTargetView> rtv;
  {
    const HRESULT msaa_hr = CreateMsaaOrFail(sample_count, 1, &msaa, &rtv);
    if (FAILED(msaa_hr)) {
      GTEST_SKIP() << sample_count << "x MSAA CreateTexture2D failed (0x"
                   << std::hex << static_cast<unsigned long>(msaa_hr)
                   << "); skipping case";
    }
  }

  ComPtr<ID3D11Texture2D> resolved;
  ASSERT_TRUE(HResultSucceeded(CreateResolveDestination(
      context_.device(), kRtWidth, kRtHeight, 1, resolved.put())));

  // Poison MSAA with black so an incomplete draw cannot pass by accident.
  constexpr float poison[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  context_.context()->ClearRenderTargetView(rtv.get(), poison);
  SetDrawColor(0.25f, 0.5f, 0.75f, 1.0f);
  DrawFullscreenSolid(rtv.get());
  context_.context()->ResolveSubresource(resolved.get(), 0, msaa.get(), 0,
                                         DXGI_FORMAT_R8G8B8A8_UNORM);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(
      ReadbackRgba8(context_.device(), context_.context(), resolved.get(), 0,
                    &pixels, &width, &height)));
  ASSERT_EQ(width, kRtWidth);
  ASSERT_EQ(height, kRtHeight);
  ExpectSolidResolved(pixels, width, height, kSolid, 2,
                      ("samples=" + std::to_string(sample_count) + " draw")
                          .c_str());
}

// 3) Two different clear colors resolve correctly (same sample count).
TEST_P(MsaaResolveSampleMatrixSpec, TwoClearColorsResolveCorrectly) {
  const UINT sample_count = GetParam();

  ComPtr<ID3D11Texture2D> msaa;
  ComPtr<ID3D11RenderTargetView> rtv;
  {
    const HRESULT msaa_hr = CreateMsaaOrFail(sample_count, 1, &msaa, &rtv);
    if (FAILED(msaa_hr)) {
      GTEST_SKIP() << sample_count << "x MSAA CreateTexture2D failed (0x"
                   << std::hex << static_cast<unsigned long>(msaa_hr)
                   << "); skipping case";
    }
  }

  ComPtr<ID3D11Texture2D> resolved_red;
  ComPtr<ID3D11Texture2D> resolved_blue;
  ASSERT_TRUE(HResultSucceeded(CreateResolveDestination(
      context_.device(), kRtWidth, kRtHeight, 1, resolved_red.put())));
  ASSERT_TRUE(HResultSucceeded(CreateResolveDestination(
      context_.device(), kRtWidth, kRtHeight, 1, resolved_blue.put())));

  constexpr float red_clear[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.context()->ClearRenderTargetView(rtv.get(), red_clear);
  context_.context()->ResolveSubresource(resolved_red.get(), 0, msaa.get(), 0,
                                         DXGI_FORMAT_R8G8B8A8_UNORM);

  constexpr float blue_clear[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  context_.context()->ClearRenderTargetView(rtv.get(), blue_clear);
  context_.context()->ResolveSubresource(resolved_blue.get(), 0, msaa.get(), 0,
                                         DXGI_FORMAT_R8G8B8A8_UNORM);

  std::vector<uint32_t> red_pixels;
  std::vector<uint32_t> blue_pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(
      ReadbackRgba8(context_.device(), context_.context(), resolved_red.get(), 0,
                    &red_pixels, &width, &height)));
  ExpectSolidResolved(red_pixels, width, height, kRed, 1,
                      ("samples=" + std::to_string(sample_count) + " red")
                          .c_str());

  ASSERT_TRUE(HResultSucceeded(
      ReadbackRgba8(context_.device(), context_.context(), resolved_blue.get(),
                    0, &blue_pixels, &width, &height)));
  ExpectSolidResolved(blue_pixels, width, height, kBlue, 1,
                      ("samples=" + std::to_string(sample_count) + " blue")
                          .c_str());
}

// 4) Array slice 0 only: MSAA array texture, RTV on slice 0, resolve subresource 0.
TEST_P(MsaaResolveSampleMatrixSpec, ResolvesArraySlice0Only) {
  const UINT sample_count = GetParam();
  constexpr UINT kArraySize = 2;

  ComPtr<ID3D11Texture2D> msaa;
  ComPtr<ID3D11RenderTargetView> rtv;
  {
    const HRESULT msaa_hr =
        CreateMsaaOrFail(sample_count, kArraySize, &msaa, &rtv);
    if (FAILED(msaa_hr)) {
      GTEST_SKIP() << sample_count << "x MSAA CreateTexture2D failed (0x"
                   << std::hex << static_cast<unsigned long>(msaa_hr)
                   << "); skipping case";
    }
  }

  ComPtr<ID3D11Texture2D> resolved;
  ASSERT_TRUE(HResultSucceeded(CreateResolveDestination(
      context_.device(), kRtWidth, kRtHeight, 1, resolved.put())));

  // Clear only the slice-0 RTV to green, then resolve SrcSubresource 0.
  constexpr float green_clear[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  context_.context()->ClearRenderTargetView(rtv.get(), green_clear);
  context_.context()->ResolveSubresource(resolved.get(), 0, msaa.get(), 0,
                                         DXGI_FORMAT_R8G8B8A8_UNORM);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(
      ReadbackRgba8(context_.device(), context_.context(), resolved.get(), 0,
                    &pixels, &width, &height)));
  ASSERT_EQ(width, kRtWidth);
  ASSERT_EQ(height, kRtHeight);
  ExpectSolidResolved(pixels, width, height, kGreen, 1,
                      ("samples=" + std::to_string(sample_count) + " slice0")
                          .c_str());
}

std::string SampleCountName(const ::testing::TestParamInfo<UINT> &info) {
  return "S" + std::to_string(info.param);
}

INSTANTIATE_TEST_SUITE_P(SampleMatrix, MsaaResolveSampleMatrixSpec,
                         ::testing::Values(2u, 4u, 8u), SampleCountName);

} // namespace
