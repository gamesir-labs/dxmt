#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string>
#include <vector>

// D3D11 clear/blend value matrices via public COM APIs only.
// RTV color clears: ClearRenderTargetView.
// RTV rect clears: ClearView (ID3D11DeviceContext1; D3D11 has no rect form of
// ClearRenderTargetView).
// Depth/stencil: ClearDepthStencilView + draw-based observation.
// Blend: CreateBlendState + OMSetBlendState + draw over known destination.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kRtWidth = 8;
constexpr UINT kRtHeight = 8;

constexpr std::string_view kFullscreenVertexShader = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
  return float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)";

// Fullscreen triangle with configurable clip-space depth from a cbuffer.
constexpr std::string_view kDepthVertexShader = R"(
cbuffer DepthConstants : register(b0) {
  float depth;
};
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
  float2 ndc = position * float2(2.0, -2.0) + float2(-1.0, 1.0);
  return float4(ndc, depth, 1.0);
}
)";

constexpr std::string_view kSolidWhitePixelShader = R"(
float4 main() : SV_Target {
  return float4(1.0, 1.0, 1.0, 1.0);
}
)";

constexpr std::string_view kConstantColorPixelShader = R"(
cbuffer ColorConstants : register(b0) {
  float4 color;
};
float4 main() : SV_Target {
  return color;
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

// Pack float RGBA [0,1] into little-endian R8G8B8A8_UNORM as uint32 (0xAABBGGRR).
uint32_t PackRgba8(float r, float g, float b, float a) {
  const auto quantize = [](float v) -> uint32_t {
    return static_cast<uint32_t>(
        std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  return quantize(r) | (quantize(g) << 8) | (quantize(b) << 16) |
         (quantize(a) << 24);
}

HRESULT ReadbackRgba8(ID3D11Device *device, ID3D11DeviceContext *context,
                      ID3D11Texture2D *texture, std::vector<uint32_t> *pixels,
                      UINT *out_width, UINT *out_height) {
  if (!device || !context || !texture || !pixels)
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
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
  hr = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
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
  context->Unmap(staging.get(), 0);
  if (out_width)
    *out_width = desc.Width;
  if (out_height)
    *out_height = desc.Height;
  return S_OK;
}

HRESULT CreateR8G8B8A8RenderTarget(ID3D11Device *device, UINT width,
                                   UINT height, ID3D11Texture2D **texture,
                                   ID3D11RenderTargetView **rtv) {
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

// ---------------------------------------------------------------------------
// 1) ClearRenderTargetView color matrix
// ---------------------------------------------------------------------------

struct ClearRtvColorCase {
  float r, g, b, a;
  const char *name;
};

class ClearRtvColorMatrixSpec
    : public ::testing::TestWithParam<ClearRtvColorCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
  }

  D3D11TestContext context_;
};

TEST_P(ClearRtvColorMatrixSpec, WholeTargetMatchesClearColor) {
  const auto &test = GetParam();
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, target.put(), rtv.put())));

  const float color[4] = {test.r, test.g, test.b, test.a};
  context_.context()->ClearRenderTargetView(rtv.get(), color);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(context_.device(),
                                             context_.context(), target.get(),
                                             &pixels, &width, &height)));
  ASSERT_EQ(width, kRtWidth);
  ASSERT_EQ(height, kRtHeight);

  const uint32_t expected = PackRgba8(test.r, test.g, test.b, test.a);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const uint32_t actual = pixels[y * width + x];
      EXPECT_TRUE(ColorMatches(actual, expected, 1))
          << "pixel (" << x << "," << y << ") actual=0x" << std::hex << actual
          << " expected=0x" << expected << " color=(" << std::dec << test.r
          << "," << test.g << "," << test.b << "," << test.a << ")";
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    ColorMatrix, ClearRtvColorMatrixSpec,
    ::testing::Values(
        ClearRtvColorCase{0.0f, 0.0f, 0.0f, 0.0f, "Zero"},
        ClearRtvColorCase{0.0f, 0.0f, 0.0f, 1.0f, "BlackOpaque"},
        ClearRtvColorCase{1.0f, 1.0f, 1.0f, 1.0f, "White"},
        ClearRtvColorCase{1.0f, 0.0f, 0.0f, 1.0f, "Red"},
        ClearRtvColorCase{0.0f, 1.0f, 0.0f, 1.0f, "Green"},
        ClearRtvColorCase{0.0f, 0.0f, 1.0f, 1.0f, "Blue"},
        ClearRtvColorCase{0.5f, 0.5f, 0.5f, 1.0f, "MidGray"},
        ClearRtvColorCase{0.25f, 0.5f, 0.75f, 1.0f, "QuarterHalfThreeQuarter"},
        ClearRtvColorCase{0.125f, 0.0f, 0.0f, 1.0f, "LowRedEdge"},
        ClearRtvColorCase{1.0f, 0.0f, 0.0f, 0.5f, "RedHalfAlpha"},
        ClearRtvColorCase{1.0f, 1.0f, 0.0f, 1.0f, "Yellow"},
        ClearRtvColorCase{0.0f, 1.0f, 1.0f, 1.0f, "Cyan"},
        ClearRtvColorCase{1.0f, 0.0f, 1.0f, 1.0f, "Magenta"}),
    [](const ::testing::TestParamInfo<ClearRtvColorCase> &info) {
      return std::string(info.param.name);
    });

// ---------------------------------------------------------------------------
// 2) Rect clear of RTV (ClearView with D3D11_RECT array)
// ---------------------------------------------------------------------------

struct ClearRtvRectCase {
  LONG left, top, right, bottom;
  const char *name;
};

class ClearRtvRectMatrixSpec
    : public ::testing::TestWithParam<ClearRtvRectCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_TRUE(HResultSucceeded(context_.context()->QueryInterface(
        __uuidof(ID3D11DeviceContext1),
        reinterpret_cast<void **>(context1_.put()))));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11DeviceContext1> context1_;
};

TEST_P(ClearRtvRectMatrixSpec, RectClearLeavesOutsideUnchanged) {
  const auto &rect_case = GetParam();
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, target.put(), rtv.put())));

  constexpr float fill_color[4] = {0.0f, 0.0f, 1.0f, 1.0f}; // blue
  constexpr float rect_color[4] = {1.0f, 0.0f, 0.0f, 1.0f}; // red
  context_.context()->ClearRenderTargetView(rtv.get(), fill_color);

  const D3D11_RECT rect = {rect_case.left, rect_case.top, rect_case.right,
                           rect_case.bottom};
  // D3D11.1 ClearView is the public API that accepts a D3D11_RECT array for an
  // RTV (ClearRenderTargetView has no rect overload).
  context1_->ClearView(rtv.get(), rect_color, &rect, 1);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(context_.device(),
                                             context_.context(), target.get(),
                                             &pixels, &width, &height)));

  const uint32_t expected_inside = PackRgba8(1.0f, 0.0f, 0.0f, 1.0f);
  const uint32_t expected_outside = PackRgba8(0.0f, 0.0f, 1.0f, 1.0f);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const bool inside = static_cast<LONG>(x) >= rect.left &&
                          static_cast<LONG>(x) < rect.right &&
                          static_cast<LONG>(y) >= rect.top &&
                          static_cast<LONG>(y) < rect.bottom;
      const uint32_t actual = pixels[y * width + x];
      const uint32_t expected = inside ? expected_inside : expected_outside;
      EXPECT_TRUE(ColorMatches(actual, expected, 1))
          << "pixel (" << x << "," << y << ") inside=" << inside
          << " actual=0x" << std::hex << actual << " expected=0x" << expected;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    RectMatrix, ClearRtvRectMatrixSpec,
    ::testing::Values(ClearRtvRectCase{0, 0, 4, 4, "TopLeftQuadrant"},
                      ClearRtvRectCase{0, 0, 1, 1, "SinglePixel"},
                      ClearRtvRectCase{2, 2, 6, 6, "CenterBlock"},
                      ClearRtvRectCase{4, 0, 8, 8, "RightHalf"},
                      ClearRtvRectCase{0, 4, 8, 8, "BottomHalf"},
                      ClearRtvRectCase{1, 1, 7, 5, "InsetRect"}),
    [](const ::testing::TestParamInfo<ClearRtvRectCase> &info) {
      return std::string(info.param.name);
    });

// ---------------------------------------------------------------------------
// 3) ClearDepthStencilView value matrix (draw-based observation)
// ---------------------------------------------------------------------------

struct ClearDepthCase {
  float clear_depth;
  float draw_depth;
  D3D11_COMPARISON_FUNC depth_func;
  bool expect_draw_visible;
  const char *name;
};

class ClearDepthValueMatrixSpec
    : public ::testing::TestWithParam<ClearDepthCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vs = CompileShader(kDepthVertexShader, "vs_5_0");
    const auto ps = CompileShader(kSolidWhitePixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vs.result)) << vs.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(ps.result)) << ps.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vs.bytecode->GetBufferPointer(), vs.bytecode->GetBufferSize(), nullptr,
        vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        ps.bytecode->GetBufferPointer(), ps.bytecode->GetBufferSize(), nullptr,
        pixel_shader_.put())));

    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.ByteWidth = 16; // float + pad to 16-byte cbuffer size
    cb_desc.Usage = D3D11_USAGE_DEFAULT;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ASSERT_TRUE(HResultSucceeded(
        context_.device()->CreateBuffer(&cb_desc, nullptr, depth_cb_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11Buffer> depth_cb_;
};

TEST_P(ClearDepthValueMatrixSpec, DrawVisibilityMatchesClearedDepth) {
  const auto &test = GetParam();

  ComPtr<ID3D11Texture2D> color;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, color.put(), rtv.put())));

  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_desc.Width = kRtWidth;
  depth_desc.Height = kRtHeight;
  depth_desc.MipLevels = 1;
  depth_desc.ArraySize = 1;
  depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Usage = D3D11_USAGE_DEFAULT;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11DepthStencilView> dsv;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&depth_desc, nullptr, depth.put())));
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDepthStencilView(depth.get(), nullptr, dsv.put())));

  D3D11_DEPTH_STENCIL_DESC ds_desc = {};
  ds_desc.DepthEnable = TRUE;
  ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  ds_desc.DepthFunc = test.depth_func;
  ds_desc.StencilEnable = FALSE;
  ComPtr<ID3D11DepthStencilState> ds_state;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDepthStencilState(&ds_desc, ds_state.put())));

  constexpr float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  context_.context()->ClearRenderTargetView(rtv.get(), black);
  context_.context()->ClearDepthStencilView(
      dsv.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, test.clear_depth, 0);

  const float depth_constants[4] = {test.draw_depth, 0.0f, 0.0f, 0.0f};
  context_.context()->UpdateSubresource(depth_cb_.get(), 0, nullptr,
                                        depth_constants, 0, 0);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(kRtWidth);
  viewport.Height = static_cast<float>(kRtHeight);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *rtvs[] = {rtv.get()};
  ID3D11Buffer *cbs[] = {depth_cb_.get()};
  context_.context()->OMSetRenderTargets(1, rtvs, dsv.get());
  context_.context()->OMSetDepthStencilState(ds_state.get(), 0);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->VSSetConstantBuffers(0, 1, cbs);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(
      context_.device(), context_.context(), color.get(), &pixels, &width,
      &height)));

  const uint32_t expected =
      test.expect_draw_visible ? PackRgba8(1, 1, 1, 1) : PackRgba8(0, 0, 0, 1);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const uint32_t actual = pixels[y * width + x];
      EXPECT_TRUE(ColorMatches(actual, expected, 1))
          << "pixel (" << x << "," << y << ") clear_depth=" << test.clear_depth
          << " draw_depth=" << test.draw_depth << " actual=0x" << std::hex
          << actual << " expected=0x" << expected;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    DepthMatrix, ClearDepthValueMatrixSpec,
    ::testing::Values(
        // Equal depth after clear must pass EQUAL.
        ClearDepthCase{0.0f, 0.0f, D3D11_COMPARISON_EQUAL, true, "Eq0"},
        ClearDepthCase{0.25f, 0.25f, D3D11_COMPARISON_EQUAL, true, "Eq025"},
        ClearDepthCase{0.5f, 0.5f, D3D11_COMPARISON_EQUAL, true, "Eq05"},
        ClearDepthCase{0.75f, 0.75f, D3D11_COMPARISON_EQUAL, true, "Eq075"},
        ClearDepthCase{1.0f, 1.0f, D3D11_COMPARISON_EQUAL, true, "Eq1"},
        // Mismatched depth must fail EQUAL.
        ClearDepthCase{0.5f, 0.25f, D3D11_COMPARISON_EQUAL, false,
                       "EqMismatch"},
        // LESS: draw closer than cleared far plane.
        ClearDepthCase{1.0f, 0.0f, D3D11_COMPARISON_LESS, true, "LessNearVsFar"},
        ClearDepthCase{0.0f, 1.0f, D3D11_COMPARISON_LESS, false,
                       "LessFarVsNear"},
        // GREATER: draw farther than cleared near plane.
        ClearDepthCase{0.0f, 1.0f, D3D11_COMPARISON_GREATER, true,
                       "GreaterFarVsNear"},
        ClearDepthCase{1.0f, 0.0f, D3D11_COMPARISON_GREATER, false,
                       "GreaterNearVsFar"}),
    [](const ::testing::TestParamInfo<ClearDepthCase> &info) {
      return std::string(info.param.name);
    });

struct ClearStencilCase {
  UINT8 clear_stencil;
  UINT8 draw_ref;
  D3D11_COMPARISON_FUNC stencil_func;
  bool expect_draw_visible;
  const char *name;
};

class ClearStencilValueMatrixSpec
    : public ::testing::TestWithParam<ClearStencilCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vs = CompileShader(kFullscreenVertexShader, "vs_5_0");
    const auto ps = CompileShader(kSolidWhitePixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vs.result)) << vs.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(ps.result)) << ps.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vs.bytecode->GetBufferPointer(), vs.bytecode->GetBufferSize(), nullptr,
        vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        ps.bytecode->GetBufferPointer(), ps.bytecode->GetBufferSize(), nullptr,
        pixel_shader_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
};

TEST_P(ClearStencilValueMatrixSpec, DrawVisibilityMatchesClearedStencil) {
  const auto &test = GetParam();

  ComPtr<ID3D11Texture2D> color;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, color.put(), rtv.put())));

  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_desc.Width = kRtWidth;
  depth_desc.Height = kRtHeight;
  depth_desc.MipLevels = 1;
  depth_desc.ArraySize = 1;
  depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Usage = D3D11_USAGE_DEFAULT;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11DepthStencilView> dsv;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&depth_desc, nullptr, depth.put())));
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDepthStencilView(depth.get(), nullptr, dsv.put())));

  D3D11_DEPTH_STENCIL_DESC ds_desc = {};
  ds_desc.DepthEnable = FALSE;
  ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  ds_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
  ds_desc.StencilEnable = TRUE;
  ds_desc.StencilReadMask = 0xff;
  ds_desc.StencilWriteMask = 0xff;
  ds_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
  ds_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
  ds_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
  ds_desc.FrontFace.StencilFunc = test.stencil_func;
  ds_desc.BackFace = ds_desc.FrontFace;
  ComPtr<ID3D11DepthStencilState> ds_state;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDepthStencilState(&ds_desc, ds_state.put())));

  constexpr float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  context_.context()->ClearRenderTargetView(rtv.get(), black);
  context_.context()->ClearDepthStencilView(
      dsv.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f,
      test.clear_stencil);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(kRtWidth);
  viewport.Height = static_cast<float>(kRtHeight);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *rtvs[] = {rtv.get()};
  context_.context()->OMSetRenderTargets(1, rtvs, dsv.get());
  context_.context()->OMSetDepthStencilState(ds_state.get(), test.draw_ref);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(
      context_.device(), context_.context(), color.get(), &pixels, &width,
      &height)));

  const uint32_t expected =
      test.expect_draw_visible ? PackRgba8(1, 1, 1, 1) : PackRgba8(0, 0, 0, 1);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const uint32_t actual = pixels[y * width + x];
      EXPECT_TRUE(ColorMatches(actual, expected, 1))
          << "pixel (" << x << "," << y
          << ") clear_stencil=" << static_cast<unsigned>(test.clear_stencil)
          << " draw_ref=" << static_cast<unsigned>(test.draw_ref)
          << " actual=0x" << std::hex << actual << " expected=0x" << expected;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    StencilMatrix, ClearStencilValueMatrixSpec,
    ::testing::Values(
        ClearStencilCase{0, 0, D3D11_COMPARISON_EQUAL, true, "Eq0"},
        ClearStencilCase{1, 1, D3D11_COMPARISON_EQUAL, true, "Eq1"},
        ClearStencilCase{127, 127, D3D11_COMPARISON_EQUAL, true, "Eq127"},
        ClearStencilCase{128, 128, D3D11_COMPARISON_EQUAL, true, "Eq128"},
        ClearStencilCase{254, 254, D3D11_COMPARISON_EQUAL, true, "Eq254"},
        ClearStencilCase{255, 255, D3D11_COMPARISON_EQUAL, true, "Eq255"},
        ClearStencilCase{0x55, 0x55, D3D11_COMPARISON_EQUAL, true, "Eq55"},
        ClearStencilCase{0x55, 0xaa, D3D11_COMPARISON_EQUAL, false,
                         "EqMismatch"},
        ClearStencilCase{10, 20, D3D11_COMPARISON_LESS, false, "LessFail"},
        ClearStencilCase{20, 10, D3D11_COMPARISON_LESS, true, "LessPass"}),
    [](const ::testing::TestParamInfo<ClearStencilCase> &info) {
      return std::string(info.param.name);
    });

// ---------------------------------------------------------------------------
// 4) Blend equation matrix
// ---------------------------------------------------------------------------

struct BlendCase {
  D3D11_BLEND src;
  D3D11_BLEND dest;
  D3D11_BLEND_OP op;
  D3D11_BLEND src_alpha;
  D3D11_BLEND dest_alpha;
  D3D11_BLEND_OP alpha_op;
  std::array<float, 4> source;
  std::array<float, 4> destination;
  std::array<float, 4> expected;
  const char *name;
};

class BlendEquationMatrixSpec : public ::testing::TestWithParam<BlendCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vs = CompileShader(kFullscreenVertexShader, "vs_5_0");
    const auto ps = CompileShader(kConstantColorPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vs.result)) << vs.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(ps.result)) << ps.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vs.bytecode->GetBufferPointer(), vs.bytecode->GetBufferSize(), nullptr,
        vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        ps.bytecode->GetBufferPointer(), ps.bytecode->GetBufferSize(), nullptr,
        pixel_shader_.put())));

    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.ByteWidth = 16;
    cb_desc.Usage = D3D11_USAGE_DEFAULT;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ASSERT_TRUE(HResultSucceeded(
        context_.device()->CreateBuffer(&cb_desc, nullptr, color_cb_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11Buffer> color_cb_;
};

TEST_P(BlendEquationMatrixSpec, BlendedColorMatchesEquation) {
  const auto &test = GetParam();

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, target.put(), rtv.put())));

  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.RenderTarget[0].BlendEnable = TRUE;
  blend_desc.RenderTarget[0].SrcBlend = test.src;
  blend_desc.RenderTarget[0].DestBlend = test.dest;
  blend_desc.RenderTarget[0].BlendOp = test.op;
  blend_desc.RenderTarget[0].SrcBlendAlpha = test.src_alpha;
  blend_desc.RenderTarget[0].DestBlendAlpha = test.dest_alpha;
  blend_desc.RenderTarget[0].BlendOpAlpha = test.alpha_op;
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D11BlendState> blend;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBlendState(&blend_desc, blend.put())));

  context_.context()->ClearRenderTargetView(rtv.get(), test.destination.data());
  context_.context()->UpdateSubresource(color_cb_.get(), 0, nullptr,
                                        test.source.data(), 0, 0);

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(kRtWidth);
  viewport.Height = static_cast<float>(kRtHeight);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *rtvs[] = {rtv.get()};
  ID3D11Buffer *cbs[] = {color_cb_.get()};
  context_.context()->OMSetRenderTargets(1, rtvs, nullptr);
  context_.context()->OMSetBlendState(blend.get(), nullptr, 0xffffffff);
  context_.context()->RSSetViewports(1, &viewport);
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->PSSetConstantBuffers(0, 1, cbs);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(context_.device(),
                                             context_.context(), target.get(),
                                             &pixels, &width, &height)));

  const uint32_t expected = PackRgba8(test.expected[0], test.expected[1],
                                      test.expected[2], test.expected[3]);
  // Tolerance 2 accounts for 8-bit quantization of intermediate blend terms.
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const uint32_t actual = pixels[y * width + x];
      EXPECT_TRUE(ColorMatches(actual, expected, 2))
          << "pixel (" << x << "," << y << ") case=" << test.name
          << " actual=0x" << std::hex << actual << " expected=0x" << expected;
    }
  }
}

// Source (1, 0, 0, 0.25) over dest (0, 0, 1, 1):
//   SRC_ALPHA/INV_SRC_ALPHA RGB: 0.25*(1,0,0) + 0.75*(0,0,1) = (0.25, 0, 0.75)
//   ONE/ZERO alpha: 1*0.25 + 0*1 = 0.25
// Source (0.75, 0.25, 0.5, 0.5) over dest (0.25, 0.5, 0.75, 0.25):
//   SRC_ALPHA/INV_SRC_ALPHA RGB:
//     0.5*(0.75,0.25,0.5) + 0.5*(0.25,0.5,0.75) = (0.5, 0.375, 0.625)
//   ONE/ZERO alpha: 0.5
// ONE/ZERO: result equals source.
INSTANTIATE_TEST_SUITE_P(
    Equations, BlendEquationMatrixSpec,
    ::testing::Values(
        BlendCase{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
                  D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO,
                  D3D11_BLEND_OP_ADD,
                  {1.0f, 0.0f, 0.0f, 0.25f},
                  {0.0f, 0.0f, 1.0f, 1.0f},
                  {0.25f, 0.0f, 0.75f, 0.25f},
                  "SrcAlphaInvSrcAlpha"},
        BlendCase{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
                  D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO,
                  D3D11_BLEND_OP_ADD,
                  {0.75f, 0.25f, 0.5f, 0.5f},
                  {0.25f, 0.5f, 0.75f, 0.25f},
                  {0.5f, 0.375f, 0.625f, 0.5f},
                  "SrcAlphaInvSrcAlphaMid"},
        BlendCase{D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD,
                  D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD,
                  {1.0f, 0.0f, 0.0f, 0.25f},
                  {0.0f, 0.0f, 1.0f, 1.0f},
                  {1.0f, 0.0f, 0.0f, 0.25f},
                  "OneZero"},
        BlendCase{D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD,
                  D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD,
                  {0.75f, 0.25f, 0.5f, 0.5f},
                  {0.25f, 0.5f, 0.75f, 0.25f},
                  {0.75f, 0.25f, 0.5f, 0.5f},
                  "OneZeroMid"}),
    [](const ::testing::TestParamInfo<BlendCase> &info) {
      return std::string(info.param.name);
    });

} // namespace
