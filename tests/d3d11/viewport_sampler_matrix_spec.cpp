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

// Public D3D11 viewport / scissor / sampler matrices.
// Exercises only ID3D11* / DXGI / D3D11CreateDevice / d3dcompiler surfaces.
// GPU results are observed via staging texture readback.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kRtSize = 32;

// Fullscreen NDC triangle (SV_VertexID 0..2).
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

// R8G8B8A8 little-endian: 0xAABBGGRR
constexpr uint32_t kSolidColor = 0xffbf8040;
constexpr uint32_t kClearColor = 0x00000000;
constexpr uint32_t kRed = 0xff0000ff;
constexpr uint32_t kGreen = 0xff00ff00;
constexpr uint32_t kBlue = 0xffff0000;
constexpr uint32_t kWhite = 0xffffffff;
// Linear mid-edge blend of R and G: (0.5, 0.5, 0, 1).
constexpr uint32_t kRedGreenBlend = 0xff008080;

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

// Pixel center (x+0.5, y+0.5) lies inside the viewport rectangle.
bool PixelCenterInViewport(UINT x, UINT y, const D3D11_VIEWPORT &vp) {
  const float cx = static_cast<float>(x) + 0.5f;
  const float cy = static_cast<float>(y) + 0.5f;
  return cx >= vp.TopLeftX && cx < vp.TopLeftX + vp.Width && cy >= vp.TopLeftY &&
         cy < vp.TopLeftY + vp.Height;
}

// ---------------------------------------------------------------------------
// 1) Viewport size / position matrix on a 32×32 RT
// ---------------------------------------------------------------------------

struct ViewportCase {
  float top_left_x;
  float top_left_y;
  float width;
  float height;
  // Stable interior sample (pixel center inside viewport).
  UINT inside_x;
  UINT inside_y;
  // Optional exterior sample (pixel center outside viewport); UINT_MAX = none.
  UINT outside_x;
  UINT outside_y;
  const char *name;
};

std::vector<ViewportCase> BuildViewportCases() {
  return {
      // Full RT
      {0.0f, 0.0f, 32.0f, 32.0f, 16, 16, UINT_MAX, UINT_MAX, "Full32x32"},
      // Half RT (top-left quadrant)
      {0.0f, 0.0f, 16.0f, 16.0f, 8, 8, 24, 24, "HalfTopLeft"},
      // Offset viewport (centered 16×16)
      {8.0f, 8.0f, 16.0f, 16.0f, 16, 16, 0, 0, "OffsetCenter16"},
      // Right half
      {16.0f, 0.0f, 16.0f, 32.0f, 24, 16, 8, 16, "RightHalf"},
      // Bottom half
      {0.0f, 16.0f, 32.0f, 16.0f, 16, 24, 16, 8, "BottomHalf"},
      // Bottom-right quarter
      {16.0f, 16.0f, 16.0f, 16.0f, 24, 24, 8, 8, "BottomRightQuarter"},
      // Narrow strip offset
      {4.0f, 12.0f, 24.0f, 8.0f, 16, 16, 16, 4, "OffsetStrip"},
  };
}

class ViewportMatrixSpec : public ::testing::TestWithParam<ViewportCase> {
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

TEST_P(ViewportMatrixSpec, FullscreenTriangleFillsViewportCenterLeavesOutsideClear) {
  const auto &test = GetParam();

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kRtSize, kRtSize, target.put(), target_view.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);

  D3D11_VIEWPORT viewport = {};
  viewport.TopLeftX = test.top_left_x;
  viewport.TopLeftY = test.top_left_y;
  viewport.Width = test.width;
  viewport.Height = test.height;
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;

  ASSERT_TRUE(PixelCenterInViewport(test.inside_x, test.inside_y, viewport))
      << "inside sample not in viewport for case " << test.name;
  if (test.outside_x != UINT_MAX) {
    ASSERT_FALSE(
        PixelCenterInViewport(test.outside_x, test.outside_y, viewport))
        << "outside sample still in viewport for case " << test.name;
  }

  ID3D11RenderTargetView *targets[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t inside = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), test.inside_x,
      test.inside_y, &inside)));
  EXPECT_TRUE(ColorMatches(inside, kSolidColor, 2))
      << "viewport center (" << test.inside_x << "," << test.inside_y
      << ") was 0x" << std::hex << inside << " case=" << test.name;

  if (test.outside_x != UINT_MAX) {
    uint32_t outside = 0xffffffffu;
    ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
        context_.device(), context_.context(), target.get(), test.outside_x,
        test.outside_y, &outside)));
    EXPECT_EQ(outside, kClearColor)
        << "outside (" << test.outside_x << "," << test.outside_y << ") was 0x"
        << std::hex << outside << " case=" << test.name;
  }
}

std::string
ViewportName(const ::testing::TestParamInfo<ViewportCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(SizePositionMatrix, ViewportMatrixSpec,
                         ::testing::ValuesIn(BuildViewportCases()),
                         ViewportName);

// ---------------------------------------------------------------------------
// 2) Scissor smaller than viewport interaction
// ---------------------------------------------------------------------------

struct ViewportScissorCase {
  float vp_x;
  float vp_y;
  float vp_w;
  float vp_h;
  LONG sc_left;
  LONG sc_top;
  LONG sc_right;
  LONG sc_bottom;
  UINT inside_x;
  UINT inside_y;
  // Outside scissor (may still be inside viewport).
  UINT outside_x;
  UINT outside_y;
  const char *name;
};

std::vector<ViewportScissorCase> BuildViewportScissorCases() {
  return {
      // Full viewport, scissor is a centered 16×16 (strictly smaller).
      {0.0f, 0.0f, 32.0f, 32.0f, 8, 8, 24, 24, 16, 16, 4, 16,
       "FullVp_CenterScissor"},
      // Full viewport, left-half scissor.
      {0.0f, 0.0f, 32.0f, 32.0f, 0, 0, 16, 32, 8, 16, 24, 16,
       "FullVp_LeftScissor"},
      // Half viewport with even smaller interior scissor.
      {0.0f, 0.0f, 16.0f, 16.0f, 4, 4, 12, 12, 8, 8, 2, 8,
       "HalfVp_InnerScissor"},
      // Offset viewport with scissor nested inside it.
      {8.0f, 8.0f, 16.0f, 16.0f, 10, 10, 22, 22, 16, 16, 9, 16,
       "OffsetVp_NestedScissor"},
  };
}

class ViewportScissorMatrixSpec
    : public ::testing::TestWithParam<ViewportScissorCase> {
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

TEST_P(ViewportScissorMatrixSpec, ScissorSmallerThanViewportRestrictsDraw) {
  const auto &test = GetParam();

  // Scissor must be strictly smaller in at least one dimension than viewport.
  const float sc_w = static_cast<float>(test.sc_right - test.sc_left);
  const float sc_h = static_cast<float>(test.sc_bottom - test.sc_top);
  ASSERT_TRUE(sc_w < test.vp_w || sc_h < test.vp_h)
      << "case " << test.name << " does not exercise scissor < viewport";

  // Inside sample is within both scissor [left,right) × [top,bottom) and
  // viewport; outside sample is outside the scissor.
  ASSERT_GE(static_cast<LONG>(test.inside_x), test.sc_left);
  ASSERT_LT(static_cast<LONG>(test.inside_x), test.sc_right);
  ASSERT_GE(static_cast<LONG>(test.inside_y), test.sc_top);
  ASSERT_LT(static_cast<LONG>(test.inside_y), test.sc_bottom);
  const bool outside_scissor =
      static_cast<LONG>(test.outside_x) < test.sc_left ||
      static_cast<LONG>(test.outside_x) >= test.sc_right ||
      static_cast<LONG>(test.outside_y) < test.sc_top ||
      static_cast<LONG>(test.outside_y) >= test.sc_bottom;
  ASSERT_TRUE(outside_scissor) << "outside sample still in scissor";

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kRtSize, kRtSize, target.put(), target_view.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);

  D3D11_VIEWPORT viewport = {};
  viewport.TopLeftX = test.vp_x;
  viewport.TopLeftY = test.vp_y;
  viewport.Width = test.vp_w;
  viewport.Height = test.vp_h;
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  D3D11_RECT scissor = {test.sc_left, test.sc_top, test.sc_right,
                        test.sc_bottom};

  ID3D11RenderTargetView *targets[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->RSSetScissorRects(1, &scissor);
  context_.context()->RSSetState(rasterizer_.get());
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t inside = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), test.inside_x,
      test.inside_y, &inside)));
  EXPECT_TRUE(ColorMatches(inside, kSolidColor, 2))
      << "scissor inside (" << test.inside_x << "," << test.inside_y
      << ") was 0x" << std::hex << inside << " case=" << test.name;

  uint32_t outside = 0xffffffffu;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), test.outside_x,
      test.outside_y, &outside)));
  EXPECT_EQ(outside, kClearColor)
      << "scissor outside (" << test.outside_x << "," << test.outside_y
      << ") was 0x" << std::hex << outside << " case=" << test.name;
}

std::string ViewportScissorName(
    const ::testing::TestParamInfo<ViewportScissorCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(InteractionMatrix, ViewportScissorMatrixSpec,
                         ::testing::ValuesIn(BuildViewportScissorCases()),
                         ViewportScissorName);

// ---------------------------------------------------------------------------
// 3) Sampler filter × address-mode matrix on a 2×2 known-color texture
// ---------------------------------------------------------------------------

// Source layout (B8G8R8A8 as uint32 / little-endian R8G8B8A8_UNORM):
//   texel (0,0)=R  (1,0)=G
//         (0,1)=B  (1,1)=W
// Texel centers: (0.25,0.25), (0.75,0.25), (0.25,0.75), (0.75,0.75).
// Mid-edge between R and G: UV (0.5, 0.25).

struct SamplerCase {
  D3D11_FILTER filter;
  D3D11_TEXTURE_ADDRESS_MODE address_u;
  D3D11_TEXTURE_ADDRESS_MODE address_v;
  float u;
  float v;
  uint32_t expected;
  unsigned tolerance;
  const char *name;
};

const char *FilterTag(D3D11_FILTER filter) {
  switch (filter) {
  case D3D11_FILTER_MIN_MAG_MIP_POINT:
    return "Point";
  case D3D11_FILTER_MIN_MAG_MIP_LINEAR:
    return "Linear";
  default:
    return "Filter";
  }
}

const char *AddressTag(D3D11_TEXTURE_ADDRESS_MODE mode) {
  switch (mode) {
  case D3D11_TEXTURE_ADDRESS_CLAMP:
    return "Clamp";
  case D3D11_TEXTURE_ADDRESS_WRAP:
    return "Wrap";
  default:
    return "Addr";
  }
}

std::vector<SamplerCase> BuildSamplerCases() {
  constexpr unsigned kPointTol = 2;
  // Linear mid-edge may round 0.5*255 to 127 or 128 depending on HW.
  constexpr unsigned kLinearTol = 4;

  return {
      // ---- Filter distinction at texel center vs mid-edge ----
      // Exact texel centers: point and linear agree.
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.25f, 0.25f, kRed, kPointTol,
       "PointClamp_Texel00"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.75f, 0.25f, kGreen, kPointTol,
       "PointClamp_Texel10"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.25f, 0.75f, kBlue, kPointTol,
       "PointClamp_Texel01"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.75f, 0.75f, kWhite, kPointTol,
       "PointClamp_Texel11"},
      {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.25f, 0.25f, kRed, kPointTol,
       "LinearClamp_Texel00"},

      // Mid-edge U between texel centers 0.25 (R) and 0.75 (G).
      // Point uses nearest (avoid exact 0.5 tie): 0.4→R, 0.6→G.
      // Linear at exact mid-edge 0.5: 50/50 R+G → ~0xff008080 (not pure R/G).
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.4f, 0.25f, kRed, kPointTol,
       "PointClamp_NearMidEdgeU_R"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.6f, 0.25f, kGreen, kPointTol,
       "PointClamp_NearMidEdgeU_G"},
      {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.5f, 0.25f, kRedGreenBlend, kLinearTol,
       "LinearClamp_MidEdgeU"},

      // Mid-edge V between texel centers 0.25 (R) and 0.75 (B).
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.25f, 0.4f, kRed, kPointTol,
       "PointClamp_NearMidEdgeV_R"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.25f, 0.6f, kBlue, kPointTol,
       "PointClamp_NearMidEdgeV_B"},
      // Linear mid-edge V: blend R(1,0,0,1) + B(0,0,1,1) → (0.5,0,0.5,1)
      // = 0xff800080
      {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.25f, 0.5f, 0xff800080u, kLinearTol,
       "LinearClamp_MidEdgeV"},

      // ---- Address modes with UV outside [0,1] ----
      // U < 0: clamp → edge texel R; wrap → frac → 0.75 → G
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, -0.25f, 0.25f, kRed, kPointTol,
       "PointClamp_UNeg"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP,
       D3D11_TEXTURE_ADDRESS_CLAMP, -0.25f, 0.25f, kGreen, kPointTol,
       "PointWrapU_UNeg"},

      // V < 0: clamp → R; wrap → 0.75 → B
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.25f, -0.25f, kRed, kPointTol,
       "PointClamp_VNeg"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_WRAP, 0.25f, -0.25f, kBlue, kPointTol,
       "PointWrapV_VNeg"},

      // U > 1: clamp → last U texel G (at v center of row 0);
      // wrap 1.25 → 0.25 → R
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 1.25f, 0.25f, kGreen, kPointTol,
       "PointClamp_UGt1"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 1.25f, 0.25f, kRed, kPointTol,
       "PointWrapU_UGt1"},

      // V > 1: clamp → last V texel B; wrap 1.25 → 0.25 → R
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 0.25f, 1.25f, kBlue, kPointTol,
       "PointClamp_VGt1"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_WRAP, 0.25f, 1.25f, kRed, kPointTol,
       "PointWrapV_VGt1"},

      // Both U,V > 1: clamp → W; wrap → (0.25,0.25) → R
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP,
       D3D11_TEXTURE_ADDRESS_CLAMP, 1.25f, 1.25f, kWhite, kPointTol,
       "PointClamp_UVGt1"},
      {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP,
       D3D11_TEXTURE_ADDRESS_WRAP, 1.25f, 1.25f, kRed, kPointTol,
       "PointWrapUV_UVGt1"},
  };
}

class SamplerMatrixSpec : public ::testing::TestWithParam<SamplerCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kTextureSamplePixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();

    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader_.put())));

    // 2×2 known colors: R G / B W
    constexpr uint32_t kTexels[4] = {kRed, kGreen, kBlue, kWhite};
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
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
        &source_desc, &initial, source_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateShaderResourceView(
        source_.get(), nullptr, source_srv_.put())));

    D3D11_BUFFER_DESC uv_cb_desc = {};
    uv_cb_desc.ByteWidth = 16;
    uv_cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    uv_cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    uv_cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ASSERT_TRUE(HResultSucceeded(
        context_.device()->CreateBuffer(&uv_cb_desc, nullptr, uv_cb_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11Texture2D> source_;
  ComPtr<ID3D11ShaderResourceView> source_srv_;
  ComPtr<ID3D11Buffer> uv_cb_;
};

TEST_P(SamplerMatrixSpec, SampleLevelMatchesFilterAndAddressMode) {
  const auto &test = GetParam();

  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = test.filter;
  sampler_desc.AddressU = test.address_u;
  sampler_desc.AddressV = test.address_v;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxAnisotropy = 1;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MinLOD = 0.0f;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateSamplerState(&sampler_desc, sampler.put())));

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      uv_cb_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  float *uv_data = static_cast<float *>(mapped.pData);
  uv_data[0] = test.u;
  uv_data[1] = test.v;
  uv_data[2] = 0.0f;
  uv_data[3] = 0.0f;
  context_.context()->Unmap(uv_cb_.get(), 0);

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), 4, 4, target.put(), target_view.put())));
  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = 4.0f;
  viewport.Height = 4.0f;
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *targets[] = {target_view.get()};
  ID3D11ShaderResourceView *srvs[] = {source_srv_.get()};
  ID3D11SamplerState *samplers[] = {sampler.get()};
  ID3D11Buffer *cbs[] = {uv_cb_.get()};
  context_.context()->OMSetRenderTargets(1, targets, nullptr);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->PSSetShaderResources(0, 1, srvs);
  context_.context()->PSSetSamplers(0, 1, samplers);
  context_.context()->PSSetConstantBuffers(0, 1, cbs);
  context_.context()->Draw(3, 0);
  ID3D11ShaderResourceView *null_srv[] = {nullptr};
  context_.context()->PSSetShaderResources(0, 1, null_srv);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t actual = 0;
  ASSERT_TRUE(HResultSucceeded(ReadTexturePixel(
      context_.device(), context_.context(), target.get(), 2, 2, &actual)));
  EXPECT_TRUE(ColorMatches(actual, test.expected, test.tolerance))
      << "sample " << test.name << " filter=" << FilterTag(test.filter)
      << " addrU=" << AddressTag(test.address_u)
      << " addrV=" << AddressTag(test.address_v) << " uv=(" << test.u << ","
      << test.v << ") was 0x" << std::hex << actual << " expected 0x"
      << test.expected;
}

std::string SamplerName(const ::testing::TestParamInfo<SamplerCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(FilterAddressMatrix, SamplerMatrixSpec,
                         ::testing::ValuesIn(BuildSamplerCases()), SamplerName);

} // namespace
