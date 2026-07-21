#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string_view>

// Public D3D11 multiple-render-target execution coverage. Every case owns its
// device and resources and is safe under the default parallel scheduler.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kWidth = 4;
constexpr UINT kHeight = 4;

constexpr std::string_view kFullscreenVertexShader = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
  return float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0),
                0.0, 1.0);
}
)";

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

uint32_t PackRgba8(float r, float g, float b, float a) {
  const auto channel = [](float value) {
    return static_cast<uint32_t>(std::lround(value * 255.0f));
  };
  return channel(r) | (channel(g) << 8) | (channel(b) << 16) |
         (channel(a) << 24);
}

bool ColorMatches(uint32_t actual, uint32_t expected, unsigned tolerance = 1) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    const int actual_channel = (actual >> shift) & 0xff;
    const int expected_channel = (expected >> shift) & 0xff;
    if (std::abs(actual_channel - expected_channel) >
        static_cast<int>(tolerance))
      return false;
  }
  return true;
}

struct RenderTarget {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> view;
};

HRESULT CreateRenderTarget(ID3D11Device *device, DXGI_FORMAT format,
                           RenderTarget *target) {
  if (!device || !target)
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = kWidth;
  desc.Height = kHeight;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, target->texture.put());
  if (FAILED(hr))
    return hr;
  return device->CreateRenderTargetView(target->texture.get(), nullptr,
                                        target->view.put());
}

template <typename T>
HRESULT ReadCenterPixel(ID3D11Device *device, ID3D11DeviceContext *context,
                        ID3D11Texture2D *texture, T *pixel) {
  if (!device || !context || !texture || !pixel)
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
  const auto *source = static_cast<const uint8_t *>(mapped.pData) +
                       (kHeight / 2) * mapped.RowPitch +
                       (kWidth / 2) * sizeof(T);
  std::memcpy(pixel, source, sizeof(*pixel));
  context->Unmap(staging.get(), 0);
  return S_OK;
}

D3D11_BLEND_DESC DefaultIndependentBlendDesc() {
  D3D11_BLEND_DESC desc = {};
  desc.IndependentBlendEnable = TRUE;
  for (auto &target : desc.RenderTarget) {
    target.SrcBlend = D3D11_BLEND_ONE;
    target.DestBlend = D3D11_BLEND_ZERO;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_ZERO;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  }
  return desc;
}

class D3D11MrtOpsSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    const auto vertex = CompileShader(kFullscreenVertexShader, "vs_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
  }

  ComPtr<ID3D11PixelShader> CreatePixelShader(std::string_view source) {
    const auto pixel = CompileShader(source, "ps_5_0");
    EXPECT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();
    if (FAILED(pixel.result))
      return {};
    ComPtr<ID3D11PixelShader> shader;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, shader.put())));
    return shader;
  }

  void Draw(ID3D11PixelShader *pixel_shader, UINT target_count,
            ID3D11RenderTargetView *const *target_views,
            ID3D11BlendState *blend = nullptr) {
    const D3D11_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight),
        0.0f, 1.0f};
    context_.context()->OMSetRenderTargets(target_count, target_views, nullptr);
    context_.context()->OMSetBlendState(blend, nullptr, 0xffffffff);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->IASetInputLayout(nullptr);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader, nullptr, 0);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
    context_.context()->OMSetBlendState(nullptr, nullptr, 0xffffffff);
  }

  uint32_t ReadRgba8(const RenderTarget &target) {
    uint32_t pixel = 0;
    EXPECT_TRUE(HResultSucceeded(ReadCenterPixel(
        context_.device(), context_.context(), target.texture.get(), &pixel)));
    return pixel;
  }

  void ExpectRgba8(const RenderTarget &target, uint32_t expected) {
    const uint32_t actual = ReadRgba8(target);
    EXPECT_TRUE(ColorMatches(actual, expected))
        << "actual=0x" << std::hex << actual << " expected=0x" << expected;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
};

TEST_F(D3D11MrtOpsSpec, ZeroRenderTargetsUnbindsEverySlot) {
  std::array<RenderTarget, 2> targets;
  std::array<ID3D11RenderTargetView *, 2> views = {};
  for (size_t index = 0; index < targets.size(); ++index) {
    ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
        context_.device(), DXGI_FORMAT_R8G8B8A8_UNORM, &targets[index])));
    views[index] = targets[index].view.get();
  }

  context_.context()->OMSetRenderTargets(views.size(), views.data(), nullptr);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  std::array<ID3D11RenderTargetView *, 2> queried = {};
  context_.context()->OMGetRenderTargets(queried.size(), queried.data(),
                                         nullptr);
  for (auto *view : queried) {
    EXPECT_EQ(view, nullptr);
    if (view)
      view->Release();
  }

  Draw(nullptr, 0, nullptr);
  context_.context()->Flush();
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11MrtOpsSpec, WritesTwoRenderTargetsInSingleDraw) {
  constexpr std::string_view pixel_source = R"(
struct Output {
  float4 first : SV_Target0;
  float4 second : SV_Target1;
};
Output main() {
  Output output;
  output.first = float4(1.0, 0.0, 0.0, 1.0);
  output.second = float4(0.0, 1.0, 0.0, 1.0);
  return output;
}
)";
  const auto pixel_shader = CreatePixelShader(pixel_source);
  ASSERT_TRUE(pixel_shader);
  std::array<RenderTarget, 2> targets;
  std::array<ID3D11RenderTargetView *, 2> views = {};
  constexpr float clear[4] = {};
  for (size_t index = 0; index < targets.size(); ++index) {
    ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
        context_.device(), DXGI_FORMAT_R8G8B8A8_UNORM, &targets[index])));
    views[index] = targets[index].view.get();
    context_.context()->ClearRenderTargetView(views[index], clear);
  }

  Draw(pixel_shader.get(), views.size(), views.data());

  ExpectRgba8(targets[0], PackRgba8(1.0f, 0.0f, 0.0f, 1.0f));
  ExpectRgba8(targets[1], PackRgba8(0.0f, 1.0f, 0.0f, 1.0f));
}

TEST_F(D3D11MrtOpsSpec, WritesAllEightRenderTargets) {
  constexpr std::string_view pixel_source = R"(
struct Output {
  float4 target0 : SV_Target0;
  float4 target1 : SV_Target1;
  float4 target2 : SV_Target2;
  float4 target3 : SV_Target3;
  float4 target4 : SV_Target4;
  float4 target5 : SV_Target5;
  float4 target6 : SV_Target6;
  float4 target7 : SV_Target7;
};
Output main() {
  Output output;
  output.target0 = float4(1.0, 0.0, 0.0, 1.0);
  output.target1 = float4(0.0, 1.0, 0.0, 1.0);
  output.target2 = float4(0.0, 0.0, 1.0, 1.0);
  output.target3 = float4(1.0, 1.0, 0.0, 1.0);
  output.target4 = float4(1.0, 0.0, 1.0, 1.0);
  output.target5 = float4(0.0, 1.0, 1.0, 1.0);
  output.target6 = float4(0.25, 0.25, 0.25, 1.0);
  output.target7 = float4(1.0, 1.0, 1.0, 1.0);
  return output;
}
)";
  const auto pixel_shader = CreatePixelShader(pixel_source);
  ASSERT_TRUE(pixel_shader);
  ASSERT_GE(context_.feature_level(), D3D_FEATURE_LEVEL_11_0);
  std::array<RenderTarget, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
  std::array<ID3D11RenderTargetView *, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>
      views = {};
  constexpr float clear[4] = {};
  for (size_t index = 0; index < targets.size(); ++index) {
    ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
        context_.device(), DXGI_FORMAT_R8G8B8A8_UNORM, &targets[index])));
    views[index] = targets[index].view.get();
    context_.context()->ClearRenderTargetView(views[index], clear);
  }

  Draw(pixel_shader.get(), views.size(), views.data());

  constexpr std::array<std::array<float, 4>, 8> expected = {{
      {1.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 1.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 1.0f, 1.0f},
      {0.0f, 1.0f, 1.0f, 1.0f},
      {0.25f, 0.25f, 0.25f, 1.0f},
      {1.0f, 1.0f, 1.0f, 1.0f},
  }};
  for (size_t index = 0; index < targets.size(); ++index) {
    SCOPED_TRACE(::testing::Message() << "render target " << index);
    const auto &color = expected[index];
    ExpectRgba8(targets[index],
                PackRgba8(color[0], color[1], color[2], color[3]));
  }
}

TEST_F(D3D11MrtOpsSpec, SupportsDifferentRenderTargetFormats) {
  constexpr std::string_view pixel_source = R"(
struct Output {
  float4 unorm_value : SV_Target0;
  float4 float_value : SV_Target1;
};
Output main() {
  Output output;
  output.unorm_value = float4(0.25, 0.5, 0.75, 1.0);
  output.float_value = float4(10.0, -2.0, 0.125, 4.0);
  return output;
}
)";
  const auto pixel_shader = CreatePixelShader(pixel_source);
  ASSERT_TRUE(pixel_shader);
  RenderTarget unorm_target;
  RenderTarget float_target;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), DXGI_FORMAT_R8G8B8A8_UNORM, &unorm_target)));
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), DXGI_FORMAT_R32G32B32A32_FLOAT, &float_target)));
  std::array<ID3D11RenderTargetView *, 2> views = {unorm_target.view.get(),
                                                   float_target.view.get()};

  Draw(pixel_shader.get(), views.size(), views.data());

  ExpectRgba8(unorm_target, PackRgba8(0.25f, 0.5f, 0.75f, 1.0f));
  std::array<float, 4> actual = {};
  ASSERT_TRUE(
      HResultSucceeded(ReadCenterPixel(context_.device(), context_.context(),
                                       float_target.texture.get(), &actual)));
  constexpr std::array<float, 4> expected = {10.0f, -2.0f, 0.125f, 4.0f};
  for (size_t channel = 0; channel < actual.size(); ++channel)
    EXPECT_FLOAT_EQ(actual[channel], expected[channel])
        << "channel " << channel;
}

TEST_F(D3D11MrtOpsSpec, AppliesIndependentRenderTargetWriteMasks) {
  constexpr std::string_view pixel_source = R"(
struct Output {
  float4 first : SV_Target0;
  float4 second : SV_Target1;
};
Output main() {
  Output output;
  output.first = float4(0.9, 0.8, 0.7, 0.6);
  output.second = float4(0.6, 0.7, 0.8, 0.9);
  return output;
}
)";
  const auto pixel_shader = CreatePixelShader(pixel_source);
  ASSERT_TRUE(pixel_shader);
  std::array<RenderTarget, 2> targets;
  std::array<ID3D11RenderTargetView *, 2> views = {};
  const std::array<std::array<float, 4>, 2> clear = {{
      {0.1f, 0.2f, 0.3f, 0.4f},
      {0.4f, 0.3f, 0.2f, 0.1f},
  }};
  for (size_t index = 0; index < targets.size(); ++index) {
    ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
        context_.device(), DXGI_FORMAT_R8G8B8A8_UNORM, &targets[index])));
    views[index] = targets[index].view.get();
    context_.context()->ClearRenderTargetView(views[index],
                                              clear[index].data());
  }
  auto blend_desc = DefaultIndependentBlendDesc();
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_BLUE;
  blend_desc.RenderTarget[1].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_ALPHA;
  ComPtr<ID3D11BlendState> blend;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBlendState(&blend_desc, blend.put())));

  Draw(pixel_shader.get(), views.size(), views.data(), blend.get());

  ExpectRgba8(targets[0], PackRgba8(0.9f, 0.2f, 0.7f, 0.4f));
  ExpectRgba8(targets[1], PackRgba8(0.4f, 0.7f, 0.2f, 0.9f));
}

TEST_F(D3D11MrtOpsSpec, AppliesIndependentBlendStates) {
  constexpr std::string_view pixel_source = R"(
struct Output {
  float4 first : SV_Target0;
  float4 second : SV_Target1;
};
Output main() {
  Output output;
  output.first = float4(0.2, 0.3, 0.4, 0.5);
  output.second = output.first;
  return output;
}
)";
  const auto pixel_shader = CreatePixelShader(pixel_source);
  ASSERT_TRUE(pixel_shader);
  std::array<RenderTarget, 2> targets;
  std::array<ID3D11RenderTargetView *, 2> views = {};
  constexpr float clear[4] = {0.1f, 0.2f, 0.3f, 0.4f};
  for (size_t index = 0; index < targets.size(); ++index) {
    ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
        context_.device(), DXGI_FORMAT_R8G8B8A8_UNORM, &targets[index])));
    views[index] = targets[index].view.get();
    context_.context()->ClearRenderTargetView(views[index], clear);
  }
  auto blend_desc = DefaultIndependentBlendDesc();
  blend_desc.RenderTarget[1].BlendEnable = TRUE;
  blend_desc.RenderTarget[1].DestBlend = D3D11_BLEND_ONE;
  blend_desc.RenderTarget[1].DestBlendAlpha = D3D11_BLEND_ONE;
  ComPtr<ID3D11BlendState> blend;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBlendState(&blend_desc, blend.put())));

  Draw(pixel_shader.get(), views.size(), views.data(), blend.get());

  ExpectRgba8(targets[0], PackRgba8(0.2f, 0.3f, 0.4f, 0.5f));
  ExpectRgba8(targets[1], PackRgba8(0.3f, 0.5f, 0.7f, 0.9f));
}

TEST_F(D3D11MrtOpsSpec, LeavesUnwrittenRenderTargetUnchanged) {
  constexpr std::string_view pixel_source = R"(
float4 main() : SV_Target0 {
  return float4(1.0, 0.25, 0.5, 1.0);
}
)";
  const auto pixel_shader = CreatePixelShader(pixel_source);
  ASSERT_TRUE(pixel_shader);
  std::array<RenderTarget, 2> targets;
  std::array<ID3D11RenderTargetView *, 2> views = {};
  const std::array<std::array<float, 4>, 2> clear = {{
      {0.0f, 0.0f, 0.0f, 0.0f},
      {0.125f, 0.375f, 0.625f, 0.875f},
  }};
  for (size_t index = 0; index < targets.size(); ++index) {
    ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
        context_.device(), DXGI_FORMAT_R8G8B8A8_UNORM, &targets[index])));
    views[index] = targets[index].view.get();
    context_.context()->ClearRenderTargetView(views[index],
                                              clear[index].data());
  }

  Draw(pixel_shader.get(), views.size(), views.data());

  ExpectRgba8(targets[0], PackRgba8(1.0f, 0.25f, 0.5f, 1.0f));
  ExpectRgba8(targets[1], PackRgba8(0.125f, 0.375f, 0.625f, 0.875f));
}

TEST_F(D3D11MrtOpsSpec, SupportsNullRenderTargetSlot) {
  constexpr std::string_view pixel_source = R"(
struct Output {
  float4 first : SV_Target0;
  float4 third : SV_Target2;
};
Output main() {
  Output output;
  output.first = float4(0.25, 0.5, 0.75, 1.0);
  output.third = float4(0.75, 0.5, 0.25, 1.0);
  return output;
}
)";
  const auto pixel_shader = CreatePixelShader(pixel_source);
  ASSERT_TRUE(pixel_shader);
  RenderTarget first;
  RenderTarget third;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), DXGI_FORMAT_R8G8B8A8_UNORM, &first)));
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), DXGI_FORMAT_R8G8B8A8_UNORM, &third)));
  std::array<ID3D11RenderTargetView *, 3> views = {first.view.get(), nullptr,
                                                   third.view.get()};
  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(first.view.get(), clear);
  context_.context()->ClearRenderTargetView(third.view.get(), clear);

  Draw(pixel_shader.get(), views.size(), views.data());

  ExpectRgba8(first, PackRgba8(0.25f, 0.5f, 0.75f, 1.0f));
  ExpectRgba8(third, PackRgba8(0.75f, 0.5f, 0.25f, 1.0f));
}

} // namespace
