#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string_view>
#include <vector>

// Public D3D11 rasterizer execution coverage. Every case owns its device,
// pipeline state, and resources, so it is safe for the parallel scheduler.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kTargetSize = 32;
constexpr uint32_t kClearColor = 0x00000000u;
constexpr uint32_t kRed = 0xff0000ffu;
constexpr uint32_t kGreen = 0xff00ff00u;

struct Vertex {
  float position[4];
};

using Triangle = std::array<Vertex, 3>;

constexpr std::string_view kVertexShader = R"(
struct Input {
  float4 position : POSITION;
};

float4 main(Input input) : SV_Position {
  return input.position;
}
)";

constexpr std::string_view kRedPixelShader = R"(
float4 main() : SV_Target {
  return float4(1.0, 0.0, 0.0, 1.0);
}
)";

constexpr std::string_view kGreenPixelShader = R"(
float4 main() : SV_Target {
  return float4(0.0, 1.0, 0.0, 1.0);
}
)";

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
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

HRESULT CreateRenderTarget(ID3D11Device *device, ID3D11Texture2D **texture,
                           ID3D11RenderTargetView **view) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = kTargetSize;
  desc.Height = kTargetSize;
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

HRESULT Readback(ID3D11Device *device, ID3D11DeviceContext *context,
                 ID3D11Texture2D *texture, std::vector<uint32_t> *pixels) {
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

constexpr Triangle kClockwiseTriangle = {{
    {{-0.75f, 0.75f, 0.5f, 1.0f}},
    {{0.75f, 0.75f, 0.5f, 1.0f}},
    {{0.0f, -0.75f, 0.5f, 1.0f}},
}};

constexpr Triangle kCounterClockwiseTriangle = {{
    kClockwiseTriangle[0],
    kClockwiseTriangle[2],
    kClockwiseTriangle[1],
}};

class D3D11RasterizationOpsSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));

    const auto vertex = CompileShader(kVertexShader, "vs_5_0");
    const auto red = CompileShader(kRedPixelShader, "ps_5_0");
    const auto green = CompileShader(kGreenPixelShader, "ps_5_0");
    ASSERT_TRUE(HResultSucceeded(vertex.result)) << vertex.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(red.result)) << red.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(green.result)) << green.diagnostic_text();
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateVertexShader(
        vertex.bytecode->GetBufferPointer(), vertex.bytecode->GetBufferSize(),
        nullptr, vertex_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        red.bytecode->GetBufferPointer(), red.bytecode->GetBufferSize(),
        nullptr, red_pixel_shader_.put())));
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreatePixelShader(
        green.bytecode->GetBufferPointer(), green.bytecode->GetBufferSize(),
        nullptr, green_pixel_shader_.put())));

    const D3D11_INPUT_ELEMENT_DESC element = {
        "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
        0,          0, D3D11_INPUT_PER_VERTEX_DATA,
        0};
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateInputLayout(
        &element, 1, vertex.bytecode->GetBufferPointer(),
        vertex.bytecode->GetBufferSize(), input_layout_.put())));
  }

  ComPtr<ID3D11Buffer> CreateVertexBuffer(const Triangle &triangle) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(triangle);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = triangle.data();
    ComPtr<ID3D11Buffer> buffer;
    EXPECT_TRUE(HResultSucceeded(
        context_.device()->CreateBuffer(&desc, &data, buffer.put())));
    return buffer;
  }

  ComPtr<ID3D11RasterizerState>
  CreateRasterizer(const D3D11_RASTERIZER_DESC &desc) {
    ComPtr<ID3D11RasterizerState> state;
    EXPECT_TRUE(HResultSucceeded(
        context_.device()->CreateRasterizerState(&desc, state.put())));
    return state;
  }

  void BindDrawState(ID3D11Buffer *vertices,
                     ID3D11RenderTargetView *target_view,
                     ID3D11DepthStencilView *depth_view,
                     ID3D11RasterizerState *rasterizer,
                     ID3D11PixelShader *pixel_shader) {
    constexpr UINT stride = sizeof(Vertex);
    constexpr UINT offset = 0;
    ID3D11Buffer *buffers[] = {vertices};
    ID3D11RenderTargetView *views[] = {target_view};
    const D3D11_VIEWPORT viewport = {0.0f,
                                     0.0f,
                                     static_cast<float>(kTargetSize),
                                     static_cast<float>(kTargetSize),
                                     0.0f,
                                     1.0f};
    context_.context()->OMSetRenderTargets(1, views, depth_view);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->RSSetState(rasterizer);
    context_.context()->IASetInputLayout(input_layout_.get());
    context_.context()->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader, nullptr, 0);
  }

  std::vector<uint32_t> Render(const Triangle &triangle,
                               const D3D11_RASTERIZER_DESC &raster_desc) {
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    EXPECT_TRUE(HResultSucceeded(CreateRenderTarget(
        context_.device(), target.put(), target_view.put())));
    auto vertices = CreateVertexBuffer(triangle);
    auto rasterizer = CreateRasterizer(raster_desc);
    if (!target || !target_view || !vertices || !rasterizer)
      return {};

    constexpr float clear[4] = {};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);
    BindDrawState(vertices.get(), target_view.get(), nullptr, rasterizer.get(),
                  red_pixel_shader_.get());
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

    std::vector<uint32_t> pixels;
    EXPECT_TRUE(HResultSucceeded(Readback(context_.device(), context_.context(),
                                          target.get(), &pixels)));
    return pixels;
  }

  uint32_t RenderCoplanarPair(bool apply_depth_bias) {
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    EXPECT_TRUE(HResultSucceeded(CreateRenderTarget(
        context_.device(), target.put(), target_view.put())));
    auto vertices = CreateVertexBuffer(kClockwiseTriangle);

    D3D11_TEXTURE2D_DESC depth_desc = {};
    depth_desc.Width = kTargetSize;
    depth_desc.Height = kTargetSize;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11DepthStencilView> depth_view;
    EXPECT_TRUE(HResultSucceeded(
        context_.device()->CreateTexture2D(&depth_desc, nullptr, depth.put())));
    if (depth) {
      EXPECT_TRUE(HResultSucceeded(context_.device()->CreateDepthStencilView(
          depth.get(), nullptr, depth_view.put())));
    }

    D3D11_DEPTH_STENCIL_DESC depth_state_desc = {};
    depth_state_desc.DepthEnable = TRUE;
    depth_state_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth_state_desc.DepthFunc = D3D11_COMPARISON_LESS;
    ComPtr<ID3D11DepthStencilState> depth_state;
    EXPECT_TRUE(HResultSucceeded(context_.device()->CreateDepthStencilState(
        &depth_state_desc, depth_state.put())));

    auto base_desc = RasterizerDesc();
    auto base_rasterizer = CreateRasterizer(base_desc);
    auto biased_desc = base_desc;
    biased_desc.DepthBias = apply_depth_bias ? -16 : 0;
    auto second_rasterizer = CreateRasterizer(biased_desc);
    if (!target || !target_view || !vertices || !depth_view || !depth_state ||
        !base_rasterizer || !second_rasterizer)
      return 0;

    constexpr float clear[4] = {};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);
    context_.context()->ClearDepthStencilView(depth_view.get(),
                                              D3D11_CLEAR_DEPTH, 1.0f, 0);
    context_.context()->OMSetDepthStencilState(depth_state.get(), 0);
    BindDrawState(vertices.get(), target_view.get(), depth_view.get(),
                  base_rasterizer.get(), red_pixel_shader_.get());
    context_.context()->Draw(3, 0);
    context_.context()->RSSetState(second_rasterizer.get());
    context_.context()->PSSetShader(green_pixel_shader_.get(), nullptr, 0);
    context_.context()->Draw(3, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);

    std::vector<uint32_t> pixels;
    EXPECT_TRUE(HResultSucceeded(Readback(context_.device(), context_.context(),
                                          target.get(), &pixels)));
    if (pixels.size() != kTargetSize * kTargetSize)
      return 0;
    return pixels[(kTargetSize / 2) * kTargetSize + kTargetSize / 2];
  }

  static size_t CountColor(const std::vector<uint32_t> &pixels,
                           uint32_t expected) {
    return std::count_if(
        pixels.begin(), pixels.end(),
        [expected](uint32_t pixel) { return ColorMatches(pixel, expected); });
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> red_pixel_shader_;
  ComPtr<ID3D11PixelShader> green_pixel_shader_;
  ComPtr<ID3D11InputLayout> input_layout_;
};

struct CullCase {
  D3D11_CULL_MODE cull_mode;
  BOOL front_counter_clockwise;
  bool counter_clockwise_vertices;
  bool expect_visible;
  const char *name;
};

class D3D11CullModeSpec : public D3D11RasterizationOpsSpec,
                          public ::testing::WithParamInterface<CullCase> {};

TEST_P(D3D11CullModeSpec, RasterizesOnlyRequestedFaces) {
  const auto &test = GetParam();
  auto desc = RasterizerDesc();
  desc.CullMode = test.cull_mode;
  desc.FrontCounterClockwise = test.front_counter_clockwise;
  const auto pixels =
      Render(test.counter_clockwise_vertices ? kCounterClockwiseTriangle
                                             : kClockwiseTriangle,
             desc);
  ASSERT_EQ(pixels.size(), kTargetSize * kTargetSize);
  const uint32_t actual =
      pixels[(kTargetSize / 2) * kTargetSize + kTargetSize / 2];
  const uint32_t expected = test.expect_visible ? kRed : kClearColor;
  EXPECT_TRUE(ColorMatches(actual, expected))
      << test.name << " center was 0x" << std::hex << actual;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

INSTANTIATE_TEST_SUITE_P(
    FaceSelection, D3D11CullModeSpec,
    ::testing::Values(
        CullCase{D3D11_CULL_NONE, FALSE, false, true, "CullNoneClockwise"},
        CullCase{D3D11_CULL_NONE, FALSE, true, true,
                 "CullNoneCounterClockwise"},
        CullCase{D3D11_CULL_BACK, FALSE, false, true, "ClockwiseFrontCullBack"},
        CullCase{D3D11_CULL_BACK, FALSE, true, false,
                 "CounterClockwiseBackCullBack"},
        CullCase{D3D11_CULL_FRONT, FALSE, false, false,
                 "ClockwiseFrontCullFront"},
        CullCase{D3D11_CULL_FRONT, FALSE, true, true,
                 "CounterClockwiseBackCullFront"},
        CullCase{D3D11_CULL_BACK, TRUE, false, false,
                 "ClockwiseBackWhenCounterClockwiseFront"},
        CullCase{D3D11_CULL_BACK, TRUE, true, true,
                 "CounterClockwiseFrontWhenSelected"}),
    [](const ::testing::TestParamInfo<CullCase> &info) {
      return info.param.name;
    });

TEST_F(D3D11RasterizationOpsSpec,
       TrianglePartiallyOutsideClipVolumeKeepsVisibleRegion) {
  constexpr Triangle triangle = {{
      {{-1.5f, -0.8f, 0.5f, 1.0f}},
      {{0.0f, 0.8f, 0.5f, 1.0f}},
      {{0.8f, -0.8f, 0.5f, 1.0f}},
  }};
  const auto pixels = Render(triangle, RasterizerDesc());
  ASSERT_EQ(pixels.size(), kTargetSize * kTargetSize);
  EXPECT_TRUE(ColorMatches(pixels[16 * kTargetSize + 16], kRed));
  EXPECT_TRUE(ColorMatches(pixels[2 * kTargetSize + 30], kClearColor));
}

TEST_F(D3D11RasterizationOpsSpec,
       TriangleCrossingNearPlaneClipsOnlyOutsidePortion) {
  constexpr Triangle baseline = {{
      {{-0.8f, -0.8f, 0.5f, 1.0f}},
      {{0.0f, 0.8f, 0.5f, 1.0f}},
      {{0.8f, -0.8f, 0.5f, 1.0f}},
  }};
  auto crossing = baseline;
  crossing[0].position[2] = -0.5f;
  const auto baseline_pixels = Render(baseline, RasterizerDesc());
  const auto crossing_pixels = Render(crossing, RasterizerDesc());
  ASSERT_EQ(baseline_pixels.size(), kTargetSize * kTargetSize);
  ASSERT_EQ(crossing_pixels.size(), kTargetSize * kTargetSize);
  const size_t baseline_count = CountColor(baseline_pixels, kRed);
  const size_t crossing_count = CountColor(crossing_pixels, kRed);
  EXPECT_GT(crossing_count, 0u);
  EXPECT_LT(crossing_count, baseline_count);
}

TEST_F(D3D11RasterizationOpsSpec,
       TriangleCrossingFarPlaneClipsOnlyOutsidePortion) {
  constexpr Triangle baseline = {{
      {{-0.8f, -0.8f, 0.5f, 1.0f}},
      {{0.0f, 0.8f, 0.5f, 1.0f}},
      {{0.8f, -0.8f, 0.5f, 1.0f}},
  }};
  auto crossing = baseline;
  crossing[0].position[2] = 1.5f;
  const auto baseline_pixels = Render(baseline, RasterizerDesc());
  const auto crossing_pixels = Render(crossing, RasterizerDesc());
  ASSERT_EQ(baseline_pixels.size(), kTargetSize * kTargetSize);
  ASSERT_EQ(crossing_pixels.size(), kTargetSize * kTargetSize);
  const size_t baseline_count = CountColor(baseline_pixels, kRed);
  const size_t crossing_count = CountColor(crossing_pixels, kRed);
  EXPECT_GT(crossing_count, 0u);
  EXPECT_LT(crossing_count, baseline_count);
}

TEST_F(D3D11RasterizationOpsSpec, DegenerateTriangleProducesNoFragments) {
  constexpr Triangle triangle = {{
      {{-0.75f, 0.0f, 0.5f, 1.0f}},
      {{0.0f, 0.0f, 0.5f, 1.0f}},
      {{0.75f, 0.0f, 0.5f, 1.0f}},
  }};
  const auto pixels = Render(triangle, RasterizerDesc());
  ASSERT_EQ(pixels.size(), kTargetSize * kTargetSize);
  EXPECT_EQ(CountColor(pixels, kRed), 0u);
  EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](uint32_t pixel) {
    return ColorMatches(pixel, kClearColor);
  }));
}

TEST_F(D3D11RasterizationOpsSpec,
       NegativeDepthBiasMovesCoplanarTriangleTowardCamera) {
  const uint32_t without_bias = RenderCoplanarPair(false);
  const uint32_t with_bias = RenderCoplanarPair(true);
  EXPECT_TRUE(ColorMatches(without_bias, kRed))
      << "unbiased coplanar draw center was 0x" << std::hex << without_bias;
  EXPECT_TRUE(ColorMatches(with_bias, kGreen))
      << "biased coplanar draw center was 0x" << std::hex << with_bias;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
