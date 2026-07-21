#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string>
#include <string_view>

// Public D3D11 viewport and scissor execution coverage. GPU results are
// observed through render-target staging readback; every case owns its state
// and resources so the default parallel scheduler is safe.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kTargetSize = 32;
constexpr uint32_t kClearColor = 0x00000000;
constexpr uint32_t kSolidColor = 0xffbf8040;
constexpr uint32_t kRed = 0xff0000ff;
constexpr uint32_t kGreen = 0xff00ff00;

constexpr std::string_view kFullscreenVertexShader = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_Position {
  float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
  return float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0),
                0.5, 1.0);
}
)";

constexpr std::string_view kSolidPixelShader = R"(
float4 main() : SV_Target {
  return float4(0.25, 0.5, 0.75, 1.0);
}
)";

constexpr std::string_view kViewportVertexShader = R"(
struct Output {
  float4 position : SV_Position;
};

Output main(uint vertex_id : SV_VertexID) {
  Output output;
  uint local_id = vertex_id % 3;
  float2 position = float2((local_id << 1) & 2, local_id & 2);
  output.position = float4(
      position * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.5, 1.0);
  return output;
}
)";

constexpr std::string_view kViewportGeometryShader = R"(
struct Input {
  float4 position : SV_Position;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
  uint viewport : SV_ViewportArrayIndex;
};

[maxvertexcount(3)]
void main(triangle Input input[3], uint primitive_id : SV_PrimitiveID,
          inout TriangleStream<Output> stream) {
  Output output;
  output.color = primitive_id == 0
                     ? float4(1.0, 0.0, 0.0, 1.0)
                     : float4(0.0, 1.0, 0.0, 1.0);
  output.viewport = primitive_id;
  [unroll]
  for (uint vertex = 0; vertex < 3; ++vertex) {
    output.position = input[vertex].position;
    stream.Append(output);
  }
}
)";

constexpr std::string_view kColorPixelShader = R"(
float4 main(float4 color : COLOR0) : SV_Target {
  return color;
}
)";

constexpr std::string_view kDepthVertexShader = R"(
cbuffer DrawParams : register(b0) {
  float4 draw_color;
  float draw_depth;
  float3 padding;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

Output main(uint vertex_id : SV_VertexID) {
  float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
  Output output;
  output.position = float4(
      position * float2(2.0, -2.0) + float2(-1.0, 1.0), draw_depth, 1.0);
  output.color = draw_color;
  return output;
}
)";

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex
         << static_cast<unsigned long>(hr);
}

bool ColorMatches(uint32_t actual, uint32_t expected, unsigned tolerance = 2) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    const int actual_channel = (actual >> shift) & 0xff;
    const int expected_channel = (expected >> shift) & 0xff;
    if (std::abs(actual_channel - expected_channel) >
        static_cast<int>(tolerance))
      return false;
  }
  return true;
}

HRESULT CreateRenderTarget(ID3D11Device *device, UINT width, UINT height,
                           ID3D11Texture2D **texture,
                           ID3D11RenderTargetView **view) {
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
  return device->CreateRenderTargetView(*texture, nullptr, view);
}

HRESULT ReadPixel(ID3D11Device *device, ID3D11DeviceContext *context,
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

HRESULT CreateRasterizer(ID3D11Device *device, bool scissor_enabled,
                         ID3D11RasterizerState **state) {
  D3D11_RASTERIZER_DESC desc = {};
  desc.FillMode = D3D11_FILL_SOLID;
  desc.CullMode = D3D11_CULL_NONE;
  desc.DepthClipEnable = TRUE;
  desc.ScissorEnable = scissor_enabled;
  return device->CreateRasterizerState(&desc, state);
}

struct ViewportBoundaryCase {
  D3D11_VIEWPORT viewport;
  UINT inside_x;
  UINT inside_y;
  UINT outside_x;
  UINT outside_y;
  const char *name;
};

constexpr std::array<ViewportBoundaryCase, 3> kViewportBoundaryCases = {{
    {{4.25f, 5.75f, 20.5f, 18.25f, 0.0f, 1.0f}, 10, 10, 3, 10,
      "Fractional"},
    {{-8.0f, -4.0f, 24.0f, 20.0f, 0.0f, 1.0f}, 8, 8, 20, 8,
      "NegativeTopLeft"},
    {{24.0f, 20.0f, 16.0f, 20.0f, 0.0f, 1.0f}, 28, 26, 8, 8,
      "PartiallyOutsideTarget"},
}};

class D3D11ViewportBoundarySpec
    : public ::testing::TestWithParam<ViewportBoundaryCase> {
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
    ASSERT_TRUE(HResultSucceeded(
        CreateRasterizer(context_.device(), false, rasterizer_.put())));
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
};

TEST_P(D3D11ViewportBoundarySpec, ClipsFullscreenDrawToViewportBounds) {
  const auto &test = GetParam();
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kTargetSize, kTargetSize, target.put(),
      target_view.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);
  ID3D11RenderTargetView *views[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, views, nullptr);
  context_.context()->RSSetViewports(1, &test.viewport);
  context_.context()->RSSetState(rasterizer_.get());
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
  context_.context()->Draw(3, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  uint32_t inside = 0;
  uint32_t outside = 0xffffffffu;
  ASSERT_TRUE(HResultSucceeded(ReadPixel(
      context_.device(), context_.context(), target.get(), test.inside_x,
      test.inside_y, &inside)));
  ASSERT_TRUE(HResultSucceeded(ReadPixel(
      context_.device(), context_.context(), target.get(), test.outside_x,
      test.outside_y, &outside)));
  EXPECT_TRUE(ColorMatches(inside, kSolidColor))
      << test.name << " inside pixel was 0x" << std::hex << inside;
  EXPECT_EQ(outside, kClearColor)
      << test.name << " outside pixel was 0x" << std::hex << outside;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ViewportBoundaryName(
    const ::testing::TestParamInfo<ViewportBoundaryCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(BoundaryCases, D3D11ViewportBoundarySpec,
                         ::testing::ValuesIn(kViewportBoundaryCases),
                         ViewportBoundaryName);

class D3D11ViewportScissorOpsSpec : public ::testing::Test {
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

  void Draw(ID3D11RenderTargetView *target_view,
            const D3D11_VIEWPORT &viewport, ID3D11RasterizerState *rasterizer,
            const D3D11_RECT *scissor) {
    ID3D11RenderTargetView *views[] = {target_view};
    context_.context()->OMSetRenderTargets(1, views, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    if (scissor)
      context_.context()->RSSetScissorRects(1, scissor);
    context_.context()->RSSetState(rasterizer);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
};

TEST_F(D3D11ViewportScissorOpsSpec, ZeroWidthViewportProducesNoFragments) {
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kTargetSize, kTargetSize, target.put(),
      target_view.put())));
  ComPtr<ID3D11RasterizerState> rasterizer;
  ASSERT_TRUE(HResultSucceeded(
      CreateRasterizer(context_.device(), false, rasterizer.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);
  const D3D11_VIEWPORT viewport = {8.0f, 0.0f, 0.0f, 32.0f, 0.0f, 1.0f};
  Draw(target_view.get(), viewport, rasterizer.get(), nullptr);

  uint32_t actual = 0xffffffffu;
  ASSERT_TRUE(HResultSucceeded(ReadPixel(context_.device(), context_.context(),
                                         target.get(), 8, 16, &actual)));
  EXPECT_EQ(actual, kClearColor);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ViewportScissorOpsSpec,
       PartiallyOutsideScissorClipsToRenderTarget) {
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kTargetSize, kTargetSize, target.put(),
      target_view.put())));
  ComPtr<ID3D11RasterizerState> rasterizer;
  ASSERT_TRUE(HResultSucceeded(
      CreateRasterizer(context_.device(), true, rasterizer.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);
  const D3D11_VIEWPORT viewport = {0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f};
  const D3D11_RECT scissor = {-8, -4, 12, 14};
  Draw(target_view.get(), viewport, rasterizer.get(), &scissor);

  uint32_t inside = 0;
  uint32_t outside = 0xffffffffu;
  ASSERT_TRUE(HResultSucceeded(ReadPixel(context_.device(), context_.context(),
                                         target.get(), 4, 4, &inside)));
  ASSERT_TRUE(HResultSucceeded(ReadPixel(context_.device(), context_.context(),
                                         target.get(), 20, 4, &outside)));
  EXPECT_TRUE(ColorMatches(inside, kSolidColor))
      << "inside pixel was 0x" << std::hex << inside;
  EXPECT_EQ(outside, kClearColor)
      << "outside pixel was 0x" << std::hex << outside;
}

TEST_F(D3D11ViewportScissorOpsSpec, EmptyScissorProducesNoFragments) {
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kTargetSize, kTargetSize, target.put(),
      target_view.put())));
  ComPtr<ID3D11RasterizerState> rasterizer;
  ASSERT_TRUE(HResultSucceeded(
      CreateRasterizer(context_.device(), true, rasterizer.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);
  const D3D11_VIEWPORT viewport = {0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f};
  const D3D11_RECT scissor = {8, 4, 8, 28};
  Draw(target_view.get(), viewport, rasterizer.get(), &scissor);

  uint32_t actual = 0xffffffffu;
  ASSERT_TRUE(HResultSucceeded(ReadPixel(context_.device(), context_.context(),
                                         target.get(), 8, 16, &actual)));
  EXPECT_EQ(actual, kClearColor);
}

TEST_F(D3D11ViewportScissorOpsSpec,
       GeometryShaderRoutesPrimitivesToViewportAndScissorArrays) {
  const auto vertex = CompileShader(kViewportVertexShader, "vs_5_0");
  const auto geometry = CompileShader(kViewportGeometryShader, "gs_5_0");
  const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
  ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
  ASSERT_TRUE(HResultSucceeded(geometry.result)) << geometry.diagnostic_text();
  ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();

  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11GeometryShader> geometry_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
      vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
      nullptr, vertex_shader.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateGeometryShader(
      geometry.bytecode->GetBufferPointer(), geometry.bytecode->GetBufferSize(),
      nullptr, geometry_shader.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
      pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
      nullptr, pixel_shader.put())));

  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  ASSERT_TRUE(HResultSucceeded(CreateRenderTarget(
      context_.device(), kTargetSize, kTargetSize, target.put(),
      target_view.put())));
  ComPtr<ID3D11RasterizerState> rasterizer;
  ASSERT_TRUE(HResultSucceeded(
      CreateRasterizer(context_.device(), true, rasterizer.put())));

  constexpr float clear[4] = {};
  context_.context()->ClearRenderTargetView(target_view.get(), clear);
  const std::array<D3D11_VIEWPORT, 2> viewports = {{
      {0.0f, 0.0f, 16.0f, 32.0f, 0.0f, 1.0f},
      {16.0f, 0.0f, 16.0f, 32.0f, 0.0f, 1.0f},
  }};
  const std::array<D3D11_RECT, 2> scissors = {{
      {0, 0, 16, 16},
      {16, 16, 32, 32},
  }};
  ID3D11RenderTargetView *views[] = {target_view.get()};
  context_.context()->OMSetRenderTargets(1, views, nullptr);
  context_.context()->RSSetViewports(viewports.size(), viewports.data());
  context_.context()->RSSetScissorRects(scissors.size(), scissors.data());
  context_.context()->RSSetState(rasterizer.get());
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->GSSetShader(geometry_shader.get(), nullptr, 0);
  context_.context()->PSSetShader(pixel_shader.get(), nullptr, 0);
  context_.context()->Draw(6, 0);
  context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

  struct Sample {
    UINT x;
    UINT y;
    uint32_t expected;
    const char *label;
  };
  constexpr std::array<Sample, 4> samples = {{
      {8, 8, kRed, "viewport 0 inside scissor 0"},
      {8, 24, kClearColor, "viewport 0 outside scissor 0"},
      {24, 8, kClearColor, "viewport 1 outside scissor 1"},
      {24, 24, kGreen, "viewport 1 inside scissor 1"},
  }};
  for (const auto &sample : samples) {
    uint32_t actual = 0xffffffffu;
    ASSERT_TRUE(HResultSucceeded(
        ReadPixel(context_.device(), context_.context(), target.get(), sample.x,
                  sample.y, &actual)));
    EXPECT_TRUE(ColorMatches(actual, sample.expected))
        << sample.label << " was 0x" << std::hex << actual;
  }
}

class D3D11ViewportDepthRangeSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vertex = CompileShader(kDepthVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kColorPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(pixel.result)) << pixel.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        pixel.bytecode->GetBufferPointer(), pixel.bytecode->GetBufferSize(),
        nullptr, pixel_shader_.put())));

    D3D11_BUFFER_DESC params_desc = {};
    params_desc.ByteWidth = sizeof(DrawParams);
    params_desc.Usage = D3D11_USAGE_DEFAULT;
    params_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
        &params_desc, nullptr, params_.put())));

    D3D11_DEPTH_STENCIL_DESC depth_state_desc = {};
    depth_state_desc.DepthEnable = TRUE;
    depth_state_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth_state_desc.DepthFunc = D3D11_COMPARISON_LESS;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateDepthStencilState(
        &depth_state_desc, depth_state_.put())));
    ASSERT_TRUE(HResultSucceeded(
        CreateRasterizer(context_.device(), false, rasterizer_.put())));
  }

  uint32_t Render(float min_depth, float max_depth) {
    constexpr UINT kSize = 16;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    EXPECT_TRUE(HResultSucceeded(CreateRenderTarget(
        context_.device(), kSize, kSize, target.put(), target_view.put())));
    if (!target_view)
      return 0;

    D3D11_TEXTURE2D_DESC depth_desc = {};
    depth_desc.Width = kSize;
    depth_desc.Height = kSize;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11DepthStencilView> depth_view;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
        &depth_desc, nullptr, depth.put())));
    if (!depth)
      return 0;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateDepthStencilView(
        depth.get(), nullptr, depth_view.put())));
    if (!depth_view)
      return 0;

    constexpr float clear[4] = {};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);
    context_.context()->ClearDepthStencilView(
        depth_view.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    const D3D11_VIEWPORT viewport = {0.0f, 0.0f, 16.0f, 16.0f,
                                     min_depth, max_depth};
    ID3D11RenderTargetView *views[] = {target_view.get()};
    ID3D11Buffer *buffers[] = {params_.get()};
    context_.context()->OMSetRenderTargets(1, views, depth_view.get());
    context_.context()->OMSetDepthStencilState(depth_state_.get(), 0);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->RSSetState(rasterizer_.get());
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->VSSetConstantBuffers(0, 1, buffers);
    context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);

    const DrawParams near_draw = {{1.0f, 0.0f, 0.0f, 1.0f}, 0.0f, {}};
    context_.context()->UpdateSubresource(params_.get(), 0, nullptr, &near_draw,
                                          0, 0);
    context_.context()->Draw(3, 0);
    const DrawParams far_draw = {{0.0f, 1.0f, 0.0f, 1.0f}, 1.0f, {}};
    context_.context()->UpdateSubresource(params_.get(), 0, nullptr, &far_draw,
                                          0, 0);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

    uint32_t actual = 0;
    EXPECT_TRUE(HResultSucceeded(ReadPixel(context_.device(), context_.context(),
                                           target.get(), 8, 8, &actual)));
    return actual;
  }

  struct DrawParams {
    float color[4];
    float depth;
    float padding[3];
  };

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11Buffer> params_;
  ComPtr<ID3D11DepthStencilState> depth_state_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
};

TEST_F(D3D11ViewportDepthRangeSpec, ZeroToOneRangeKeepsNearDrawInFront) {
  const uint32_t actual = Render(0.0f, 1.0f);
  EXPECT_TRUE(ColorMatches(actual, kRed))
      << "normal depth range center was 0x" << std::hex << actual;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ViewportDepthRangeSpec, ReversedRangeKeepsFarNdcDrawInFront) {
  const uint32_t actual = Render(1.0f, 0.0f);
  EXPECT_TRUE(ColorMatches(actual, kGreen))
      << "reversed depth range center was 0x" << std::hex << actual;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
