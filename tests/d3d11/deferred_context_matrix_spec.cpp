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

// Public D3D11 deferred-context matrix: CreateDeferredContext, record on the
// deferred context, FinishCommandList, ExecuteCommandList on the immediate
// context, then readback via the immediate path. Only ID3D11* / DXGI /
// D3D11CreateDevice / d3dcompiler surfaces are used.

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

constexpr std::string_view kSolidPixelShader = R"(
float4 main() : SV_Target {
  return float4(0.25, 0.5, 0.75, 1.0);
}
)";

// R8G8B8A8 little-endian: 0xAABBGGRR
constexpr uint32_t kSolidDrawColor = 0xffbf8040;
constexpr uint32_t kPriorClearColor = 0xff0000ff; // red

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

HRESULT CreateR8G8B8A8RenderTarget(ID3D11Device *device, UINT width,
                                   UINT height, ID3D11Texture2D **texture,
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

void ExpectWholeTarget(const std::vector<uint32_t> &pixels, UINT width,
                       UINT height, uint32_t expected, unsigned tolerance,
                       const char *label) {
  ASSERT_EQ(pixels.size(), static_cast<size_t>(width) * height) << label;
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
// 1) Deferred ClearRenderTargetView color × restore-flag matrix
// ---------------------------------------------------------------------------

struct DeferredClearCase {
  float r, g, b, a;
  BOOL finish_restore;  // FinishCommandList RestoreDeferredContextState
  BOOL execute_restore; // ExecuteCommandList RestoreContextState
  const char *name;
};

class DeferredClearMatrixSpec
    : public ::testing::TestWithParam<DeferredClearCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
  }

  D3D11TestContext context_;
};

TEST_P(DeferredClearMatrixSpec,
       DeferredClearThenExecuteMatchesColorAndRestoreContract) {
  const auto &test = GetParam();

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, target.put(), rtv.put())));

  // Distinct prior contents so a no-op execute would leave the wrong color.
  constexpr float prior[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  context_.context()->ClearRenderTargetView(rtv.get(), prior);

  // Bind a known RTV on the immediate context before execute so restore TRUE
  // can be observed via OMGetRenderTargets; restore FALSE resets it.
  ID3D11RenderTargetView *bound[] = {rtv.get()};
  context_.context()->OMSetRenderTargets(1, bound, nullptr);

  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDeferredContext(0, deferred.put())));
  ASSERT_EQ(deferred->GetType(), D3D11_DEVICE_CONTEXT_DEFERRED);

  const float clear_color[4] = {test.r, test.g, test.b, test.a};
  deferred->ClearRenderTargetView(rtv.get(), clear_color);

  ComPtr<ID3D11CommandList> command_list;
  ASSERT_TRUE(HResultSucceeded(
      deferred->FinishCommandList(test.finish_restore, command_list.put())));
  ASSERT_TRUE(command_list);

  context_.context()->ExecuteCommandList(command_list.get(),
                                         test.execute_restore);

  // RestoreContextState on ExecuteCommandList: TRUE keeps pre-execute state;
  // FALSE returns the immediate context to the default state.
  {
    ComPtr<ID3D11RenderTargetView> got_rtv;
    ComPtr<ID3D11DepthStencilView> got_dsv;
    context_.context()->OMGetRenderTargets(1, got_rtv.put(), got_dsv.put());
    if (test.execute_restore) {
      EXPECT_EQ(got_rtv.get(), rtv.get())
          << "ExecuteCommandList(TRUE) must restore OM render targets";
    } else {
      EXPECT_EQ(got_rtv.get(), nullptr)
          << "ExecuteCommandList(FALSE) must reset OM render targets";
    }
  }

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(context_.device(),
                                             context_.context(), target.get(),
                                             &pixels, &width, &height)));
  ASSERT_EQ(width, kRtWidth);
  ASSERT_EQ(height, kRtHeight);

  const uint32_t expected = PackRgba8(test.r, test.g, test.b, test.a);
  ExpectWholeTarget(pixels, width, height, expected, 1, test.name);
}

std::vector<DeferredClearCase> BuildDeferredClearCases() {
  // Color × Finish restore × Execute restore matrix (subset of colors that
  // already exercise quantization edges; full bool cross-product on flags).
  const struct {
    float r, g, b, a;
    const char *color_name;
  } colors[] = {
      {0.0f, 0.0f, 0.0f, 1.0f, "BlackOpaque"},
      {1.0f, 0.0f, 0.0f, 1.0f, "Red"},
      {0.0f, 1.0f, 0.0f, 1.0f, "Green"},
      {0.0f, 0.0f, 1.0f, 1.0f, "Blue"},
      {0.125f, 0.25f, 0.5f, 1.0f, "PipelineStyle"},
      {0.25f, 0.5f, 0.75f, 1.0f, "QuarterHalfThreeQuarter"},
      {1.0f, 1.0f, 1.0f, 1.0f, "White"},
      {1.0f, 0.0f, 0.0f, 0.5f, "RedHalfAlpha"},
  };

  const BOOL restores[] = {FALSE, TRUE};
  std::vector<DeferredClearCase> cases;
  for (const auto &color : colors) {
    for (const BOOL finish_restore : restores) {
      for (const BOOL execute_restore : restores) {
        DeferredClearCase c = {};
        c.r = color.r;
        c.g = color.g;
        c.b = color.b;
        c.a = color.a;
        c.finish_restore = finish_restore;
        c.execute_restore = execute_restore;
        // Name is filled after push via stable string storage below.
        c.name = color.color_name;
        cases.push_back(c);
      }
    }
  }
  return cases;
}

// Stable name storage for INSTANTIATE (gtest requires stable C strings for
// the default name printer; we supply a custom one that builds from fields).
std::string
DeferredClearName(const ::testing::TestParamInfo<DeferredClearCase> &info) {
  return std::string(info.param.name) + "_Finish" +
         (info.param.finish_restore ? "T" : "F") + "_Exec" +
         (info.param.execute_restore ? "T" : "F");
}

INSTANTIATE_TEST_SUITE_P(ColorRestoreMatrix, DeferredClearMatrixSpec,
                         ::testing::ValuesIn(BuildDeferredClearCases()),
                         DeferredClearName);

// ---------------------------------------------------------------------------
// 2) Deferred fullscreen solid-color draw then execute
// ---------------------------------------------------------------------------

struct DeferredDrawRestoreCase {
  BOOL finish_restore;
  BOOL execute_restore;
  const char *name;
};

class DeferredDrawMatrixSpec
    : public ::testing::TestWithParam<DeferredDrawRestoreCase> {
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

TEST_P(DeferredDrawMatrixSpec,
       DeferredFullscreenTriangleSolidColorMatchesAfterExecute) {
  const auto &test = GetParam();

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, target.put(), rtv.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(rtv.get(), clear);

  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDeferredContext(0, deferred.put())));

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(kRtWidth);
  viewport.Height = static_cast<float>(kRtHeight);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *targets[] = {rtv.get()};
  deferred->OMSetRenderTargets(1, targets, nullptr);
  deferred->RSSetViewports(1, &viewport);
  deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  deferred->VSSetShader(vertex_shader_.get(), nullptr, 0);
  deferred->PSSetShader(pixel_shader_.get(), nullptr, 0);
  deferred->Draw(3, 0);

  ComPtr<ID3D11CommandList> command_list;
  ASSERT_TRUE(HResultSucceeded(
      deferred->FinishCommandList(test.finish_restore, command_list.put())));
  context_.context()->ExecuteCommandList(command_list.get(),
                                         test.execute_restore);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(context_.device(),
                                             context_.context(), target.get(),
                                             &pixels, &width, &height)));
  ExpectWholeTarget(pixels, width, height, kSolidDrawColor, 2, test.name);
}

INSTANTIATE_TEST_SUITE_P(
    RestoreMatrix, DeferredDrawMatrixSpec,
    ::testing::Values(
        DeferredDrawRestoreCase{FALSE, FALSE, "FinishF_ExecF"},
        DeferredDrawRestoreCase{FALSE, TRUE, "FinishF_ExecT"},
        DeferredDrawRestoreCase{TRUE, FALSE, "FinishT_ExecF"},
        DeferredDrawRestoreCase{TRUE, TRUE, "FinishT_ExecT"}),
    [](const ::testing::TestParamInfo<DeferredDrawRestoreCase> &info) {
      return std::string(info.param.name);
    });

// ---------------------------------------------------------------------------
// 3) Two command lists recorded sequentially; both executed (last clear wins)
// ---------------------------------------------------------------------------

class DeferredTwoListSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
  }

  D3D11TestContext context_;
};

TEST_F(DeferredTwoListSpec,
       SequentialClearListsLastWinsWhenBothExecuted) {
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, target.put(), rtv.put())));

  constexpr float seed[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  context_.context()->ClearRenderTargetView(rtv.get(), seed);

  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDeferredContext(0, deferred.put())));

  constexpr float first_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // red
  constexpr float second_color[4] = {0.0f, 1.0f, 0.0f, 1.0f}; // green

  deferred->ClearRenderTargetView(rtv.get(), first_color);
  ComPtr<ID3D11CommandList> first_list;
  ASSERT_TRUE(HResultSucceeded(
      deferred->FinishCommandList(FALSE, first_list.put())));

  // Rebind is not required for ClearRenderTargetView (view is an argument),
  // but re-recording after Finish with restore FALSE is the sequential path.
  deferred->ClearRenderTargetView(rtv.get(), second_color);
  ComPtr<ID3D11CommandList> second_list;
  ASSERT_TRUE(HResultSucceeded(
      deferred->FinishCommandList(FALSE, second_list.put())));

  // Execute both in order; full-target clears mean the last clear wins.
  context_.context()->ExecuteCommandList(first_list.get(), FALSE);
  context_.context()->ExecuteCommandList(second_list.get(), FALSE);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(context_.device(),
                                             context_.context(), target.get(),
                                             &pixels, &width, &height)));
  ExpectWholeTarget(pixels, width, height, PackRgba8(0.0f, 1.0f, 0.0f, 1.0f), 1,
                    "last_clear_wins");
}

TEST_F(DeferredTwoListSpec,
       SequentialClearThenDrawBothEffectsVisible) {
  // List 1: clear to red. List 2: draw solid blue-ish triangle.
  // After both execute, the draw overwrites the clear (both effects applied
  // in order; final pixels match the draw color).
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, target.put(), rtv.put())));

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

  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDeferredContext(0, deferred.put())));

  constexpr float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  deferred->ClearRenderTargetView(rtv.get(), red);
  ComPtr<ID3D11CommandList> clear_list;
  ASSERT_TRUE(HResultSucceeded(
      deferred->FinishCommandList(FALSE, clear_list.put())));

  D3D11_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(kRtWidth);
  viewport.Height = static_cast<float>(kRtHeight);
  viewport.MaxDepth = 1.0f;
  ID3D11RenderTargetView *targets[] = {rtv.get()};
  deferred->OMSetRenderTargets(1, targets, nullptr);
  deferred->RSSetViewports(1, &viewport);
  deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  deferred->VSSetShader(vertex_shader.get(), nullptr, 0);
  deferred->PSSetShader(pixel_shader.get(), nullptr, 0);
  deferred->Draw(3, 0);
  ComPtr<ID3D11CommandList> draw_list;
  ASSERT_TRUE(HResultSucceeded(
      deferred->FinishCommandList(FALSE, draw_list.put())));

  context_.context()->ExecuteCommandList(clear_list.get(), FALSE);
  {
    // Intermediate: only the clear list has run → red.
    std::vector<uint32_t> mid;
    UINT w = 0;
    UINT h = 0;
    ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(
        context_.device(), context_.context(), target.get(), &mid, &w, &h)));
    ExpectWholeTarget(mid, w, h, PackRgba8(1.0f, 0.0f, 0.0f, 1.0f), 1,
                      "after_first_list_clear");
  }

  context_.context()->ExecuteCommandList(draw_list.get(), FALSE);
  {
    std::vector<uint32_t> final_pixels;
    UINT w = 0;
    UINT h = 0;
    ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(context_.device(),
                                               context_.context(), target.get(),
                                               &final_pixels, &w, &h)));
    ExpectWholeTarget(final_pixels, w, h, kSolidDrawColor, 2,
                      "after_second_list_draw");
  }
}

// ---------------------------------------------------------------------------
// 4) Empty command list execute does not crash; prior clear intact
// ---------------------------------------------------------------------------

class DeferredEmptyListSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
  }

  D3D11TestContext context_;
};

TEST_F(DeferredEmptyListSpec,
       EmptyCommandListExecuteLeavesPriorClearIntact) {
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, target.put(), rtv.put())));

  constexpr float prior[4] = {1.0f, 0.0f, 0.0f, 1.0f}; // red
  context_.context()->ClearRenderTargetView(rtv.get(), prior);

  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDeferredContext(0, deferred.put())));

  // Finish with no recorded commands.
  ComPtr<ID3D11CommandList> empty_list;
  ASSERT_TRUE(
      HResultSucceeded(deferred->FinishCommandList(FALSE, empty_list.put())));
  ASSERT_TRUE(empty_list);

  // Both restore flags; empty execute must not crash either way.
  context_.context()->ExecuteCommandList(empty_list.get(), FALSE);

  std::vector<uint32_t> pixels_false;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(
      context_.device(), context_.context(), target.get(), &pixels_false,
      &width, &height)));
  ExpectWholeTarget(pixels_false, width, height, kPriorClearColor, 1,
                    "empty_exec_restore_false");

  // Re-record another empty list and execute with restore TRUE.
  empty_list.reset();
  ASSERT_TRUE(
      HResultSucceeded(deferred->FinishCommandList(TRUE, empty_list.put())));
  ID3D11RenderTargetView *bound[] = {rtv.get()};
  context_.context()->OMSetRenderTargets(1, bound, nullptr);
  context_.context()->ExecuteCommandList(empty_list.get(), TRUE);

  {
    ComPtr<ID3D11RenderTargetView> got_rtv;
    ComPtr<ID3D11DepthStencilView> got_dsv;
    context_.context()->OMGetRenderTargets(1, got_rtv.put(), got_dsv.put());
    EXPECT_EQ(got_rtv.get(), rtv.get())
        << "empty ExecuteCommandList(TRUE) must restore OM targets";
  }

  std::vector<uint32_t> pixels_true;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(
      context_.device(), context_.context(), target.get(), &pixels_true, &width,
      &height)));
  ExpectWholeTarget(pixels_true, width, height, kPriorClearColor, 1,
                    "empty_exec_restore_true");
}

TEST_F(DeferredEmptyListSpec,
       EmptyListBetweenPriorAndLaterClearDoesNotSpoilContents) {
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  ASSERT_TRUE(HResultSucceeded(CreateR8G8B8A8RenderTarget(
      context_.device(), kRtWidth, kRtHeight, target.put(), rtv.put())));

  constexpr float prior[4] = {0.0f, 0.0f, 1.0f, 1.0f}; // blue
  context_.context()->ClearRenderTargetView(rtv.get(), prior);

  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateDeferredContext(0, deferred.put())));

  ComPtr<ID3D11CommandList> empty_list;
  ASSERT_TRUE(
      HResultSucceeded(deferred->FinishCommandList(FALSE, empty_list.put())));
  context_.context()->ExecuteCommandList(empty_list.get(), FALSE);

  // Immediate clear after empty execute still works; proves context usable.
  constexpr float later[4] = {0.0f, 1.0f, 0.0f, 1.0f}; // green
  context_.context()->ClearRenderTargetView(rtv.get(), later);

  std::vector<uint32_t> pixels;
  UINT width = 0;
  UINT height = 0;
  ASSERT_TRUE(HResultSucceeded(ReadbackRgba8(context_.device(),
                                             context_.context(), target.get(),
                                             &pixels, &width, &height)));
  ExpectWholeTarget(pixels, width, height, PackRgba8(0.0f, 1.0f, 0.0f, 1.0f), 1,
                    "later_immediate_clear");
}

} // namespace
