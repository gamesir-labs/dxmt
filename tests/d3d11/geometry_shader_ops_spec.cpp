#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kTargetWidth = 16;
constexpr UINT kTargetHeight = 16;

constexpr std::string_view kVertexShader = R"(
struct Output {
  float4 position : SV_Position;
  nointerpolation uint vertex_id : TEXCOORD0;
};

Output main(uint vertex_id : SV_VertexID) {
  Output output;
  output.position = float4(0.0, 0.0, 0.0, 1.0);
  output.vertex_id = vertex_id;
  return output;
}
)";

constexpr std::string_view kPixelShader = R"(
struct Input {
  float4 position : SV_Position;
  nointerpolation float4 color : COLOR0;
};

float4 main(Input input) : SV_Target {
  return input.color;
}
)";

constexpr std::string_view kConstantGreenPixelShader = R"(
float4 main() : SV_Target {
  return float4(0.0, 1.0, 0.0, 1.0);
}
)";

constexpr std::string_view kPrimitiveIdGeometryShader = R"(
struct Input {
  float4 position : SV_Position;
  nointerpolation uint vertex_id : TEXCOORD0;
};
struct Output {
  float4 position : SV_Position;
  nointerpolation float4 color : COLOR0;
};

[maxvertexcount(4)]
void main(point Input input[1], uint primitive_id : SV_PrimitiveID,
          inout TriangleStream<Output> stream) {
  float left = primitive_id == 0 ? -1.0 : 0.0;
  float right = primitive_id == 0 ? 0.0 : 1.0;
  float4 expected = primitive_id == 0
                      ? float4(1.0, 0.0, 0.0, 1.0)
                      : float4(0.0, 1.0, 0.0, 1.0);
  float4 color = input[0].vertex_id == primitive_id
                   ? expected
                   : float4(0.0, 0.0, 1.0, 1.0);
  Output output;
  output.color = color;
  output.position = float4(left, 1.0, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(right, 1.0, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(left, -1.0, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(right, -1.0, 0.0, 1.0);
  stream.Append(output);
  stream.RestartStrip();
}
)";

constexpr std::string_view kPointOutputGeometryShader = R"(
struct Input {
  float4 position : SV_Position;
  nointerpolation uint vertex_id : TEXCOORD0;
};
struct Output {
  float4 position : SV_Position;
  nointerpolation float4 color : COLOR0;
};

[maxvertexcount(3)]
void main(point Input input[1], inout PointStream<Output> stream) {
  Output output;
  output.color = float4(1.0, 0.0, 0.0, 1.0);
  output.position = float4(-0.5, 0.0, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(0.0, 0.0, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(0.5, 0.0, 0.0, 1.0);
  stream.Append(output);
}
)";

constexpr std::string_view kLineOutputGeometryShader = R"(
struct Input {
  float4 position : SV_Position;
  nointerpolation uint vertex_id : TEXCOORD0;
};
struct Output {
  float4 position : SV_Position;
  nointerpolation float4 color : COLOR0;
};

[maxvertexcount(4)]
void main(point Input input[1], inout LineStream<Output> stream) {
  Output output;
  output.color = float4(1.0, 0.0, 0.0, 1.0);
  output.position = float4(-0.75, 0.5, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(0.75, 0.5, 0.0, 1.0);
  stream.Append(output);
  stream.RestartStrip();
  output.color = float4(0.0, 1.0, 0.0, 1.0);
  output.position = float4(-0.75, -0.5, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(0.75, -0.5, 0.0, 1.0);
  stream.Append(output);
  stream.RestartStrip();
}
)";

constexpr std::string_view kMultiplePrimitiveGeometryShader = R"(
struct Input {
  float4 position : SV_Position;
  nointerpolation uint vertex_id : TEXCOORD0;
};
struct Output {
  float4 position : SV_Position;
  nointerpolation float4 color : COLOR0;
};

void emit_vertex(float2 position, float4 color,
                 inout TriangleStream<Output> stream) {
  Output output;
  output.position = float4(position, 0.0, 1.0);
  output.color = color;
  stream.Append(output);
}

[maxvertexcount(6)]
void main(point Input input[1], inout TriangleStream<Output> stream) {
  emit_vertex(float2(-0.9, 0.8), float4(1.0, 0.0, 0.0, 1.0), stream);
  emit_vertex(float2(-0.1, 0.8), float4(1.0, 0.0, 0.0, 1.0), stream);
  emit_vertex(float2(-0.5, -0.8), float4(1.0, 0.0, 0.0, 1.0), stream);
  stream.RestartStrip();
  emit_vertex(float2(0.1, 0.8), float4(0.0, 1.0, 0.0, 1.0), stream);
  emit_vertex(float2(0.9, 0.8), float4(0.0, 1.0, 0.0, 1.0), stream);
  emit_vertex(float2(0.5, -0.8), float4(0.0, 1.0, 0.0, 1.0), stream);
  stream.RestartStrip();
}
)";

// SV_Position is four components, so 256 vertices exactly reach the D3D11
// 1024-component geometry-shader output limit.
constexpr std::string_view kMaximumVertexCountGeometryShader = R"(
struct Input {
  float4 position : SV_Position;
  nointerpolation uint vertex_id : TEXCOORD0;
};
struct Output {
  float4 position : SV_Position;
};

[maxvertexcount(256)]
void main(point Input input[1], inout TriangleStream<Output> stream) {
  Output output;
  output.position = float4(-1.0, 1.0, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(3.0, 1.0, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(-1.0, -3.0, 0.0, 1.0);
  stream.Append(output);
  stream.RestartStrip();
}
)";

struct GeometryInputCase {
  const char *primitive;
  UINT vertex_count;
  D3D11_PRIMITIVE_TOPOLOGY topology;
  const char *name;
};

constexpr std::array kGeometryInputCases = {
    GeometryInputCase{"point", 1, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, "Point"},
    GeometryInputCase{"line", 2, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "Line"},
    GeometryInputCase{"triangle", 3, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
                      "Triangle"},
    GeometryInputCase{"lineadj", 4, D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,
                      "LineAdjacency"},
    GeometryInputCase{"triangleadj", 6,
                      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,
                      "TriangleAdjacency"},
};

std::string GeometryInputShader(const GeometryInputCase &test) {
  std::string source = R"(
struct Input {
  float4 position : SV_Position;
  nointerpolation uint vertex_id : TEXCOORD0;
};
struct Output {
  float4 position : SV_Position;
  nointerpolation float4 color : COLOR0;
};

[maxvertexcount(3)]
void main()";
  source += test.primitive;
  source += " Input input[" + std::to_string(test.vertex_count) + R"(],
          uint primitive_id : SV_PrimitiveID,
          inout TriangleStream<Output> stream) {
  uint total = primitive_id;
  [unroll]
  for (uint i = 0; i < )";
  source += std::to_string(test.vertex_count);
  source += R"(; ++i)
    total += input[i].vertex_id;
  Output output;
  output.color = float4((total + 1.0) / 16.0, 0.25, 0.75, 1.0);
  output.position = float4(-1.0, 1.0, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(3.0, 1.0, 0.0, 1.0);
  stream.Append(output);
  output.position = float4(-1.0, -3.0, 0.0, 1.0);
  stream.Append(output);
  stream.RestartStrip();
}
)";
  return source;
}

uint8_t FloatToUnorm(float value) {
  return static_cast<uint8_t>(std::lround(value * 255.0f));
}

uint32_t PackColor(float red, float green, float blue, float alpha = 1.0f) {
  return static_cast<uint32_t>(FloatToUnorm(red)) |
         (static_cast<uint32_t>(FloatToUnorm(green)) << 8u) |
         (static_cast<uint32_t>(FloatToUnorm(blue)) << 16u) |
         (static_cast<uint32_t>(FloatToUnorm(alpha)) << 24u);
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

class D3D11GeometryShaderOpsSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto vertex = CompileShader(kVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kPixelShader, "ps_5_0");
    const auto constant_green =
        CompileShader(kConstantGreenPixelShader, "ps_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
    ASSERT_EQ(constant_green.result, S_OK) << constant_green.diagnostic_text();
    ASSERT_EQ(context_.device()->CreateVertexShader(
                  vertex.bytecode->GetBufferPointer(),
                  vertex.bytecode->GetBufferSize(), nullptr,
                  vertex_shader_.put()),
              S_OK);
    ASSERT_EQ(
        context_.device()->CreatePixelShader(pixel.bytecode->GetBufferPointer(),
                                             pixel.bytecode->GetBufferSize(),
                                             nullptr, pixel_shader_.put()),
        S_OK);
    ASSERT_EQ(context_.device()->CreatePixelShader(
                  constant_green.bytecode->GetBufferPointer(),
                  constant_green.bytecode->GetBufferSize(), nullptr,
                  constant_green_pixel_shader_.put()),
              S_OK);
  }

  std::vector<uint32_t> Run(std::string_view geometry_source,
                            D3D11_PRIMITIVE_TOPOLOGY topology,
                            UINT vertex_count,
                            bool use_constant_green_pixel_shader = false) {
    const auto geometry = CompileShader(geometry_source, "gs_5_0");
    EXPECT_EQ(geometry.result, S_OK) << geometry.diagnostic_text();
    if (FAILED(geometry.result))
      return {};
    ComPtr<ID3D11GeometryShader> geometry_shader;
    EXPECT_EQ(context_.device()->CreateGeometryShader(
                  geometry.bytecode->GetBufferPointer(),
                  geometry.bytecode->GetBufferSize(), nullptr,
                  geometry_shader.put()),
              S_OK);
    if (!geometry_shader)
      return {};

    D3D11_TEXTURE2D_DESC target_desc = {};
    target_desc.Width = kTargetWidth;
    target_desc.Height = kTargetHeight;
    target_desc.MipLevels = 1;
    target_desc.ArraySize = 1;
    target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.SampleDesc.Count = 1;
    target_desc.Usage = D3D11_USAGE_DEFAULT;
    target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    EXPECT_EQ(
        context_.device()->CreateTexture2D(&target_desc, nullptr, target.put()),
        S_OK);
    if (!target)
      return {};
    EXPECT_EQ(context_.device()->CreateRenderTargetView(target.get(), nullptr,
                                                        target_view.put()),
              S_OK);
    if (!target_view)
      return {};

    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(kTargetWidth);
    viewport.Height = static_cast<float>(kTargetHeight);
    viewport.MaxDepth = 1.0f;
    ID3D11RenderTargetView *target_views[] = {target_view.get()};
    context_.context()->OMSetRenderTargets(1, target_views, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->IASetPrimitiveTopology(topology);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->GSSetShader(geometry_shader.get(), nullptr, 0);
    context_.context()->PSSetShader(use_constant_green_pixel_shader
                                        ? constant_green_pixel_shader_.get()
                                        : pixel_shader_.get(),
                                    nullptr, 0);
    context_.context()->Draw(vertex_count, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
    context_.context()->GSSetShader(nullptr, nullptr, 0);

    D3D11_TEXTURE2D_DESC staging_desc = target_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    EXPECT_EQ(context_.device()->CreateTexture2D(&staging_desc, nullptr,
                                                 staging.put()),
              S_OK);
    if (!staging)
      return {};
    context_.context()->CopyResource(staging.get(), target.get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    EXPECT_EQ(
        context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
        S_OK);
    if (!mapped.pData)
      return {};
    std::vector<uint32_t> pixels(kTargetWidth * kTargetHeight);
    for (UINT y = 0; y < kTargetHeight; ++y) {
      std::memcpy(pixels.data() + y * kTargetWidth,
                  static_cast<const uint8_t *>(mapped.pData) +
                      y * mapped.RowPitch,
                  kTargetWidth * sizeof(uint32_t));
    }
    context_.context()->Unmap(staging.get(), 0);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    return pixels;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11PixelShader> constant_green_pixel_shader_;
};

class D3D11GeometryInputSpec
    : public D3D11GeometryShaderOpsSpec,
      public ::testing::WithParamInterface<GeometryInputCase> {};

TEST_P(D3D11GeometryInputSpec,
       ReadsEveryVertexAndRasterizesTriangleStripOutput) {
  const auto &test = GetParam();
  const auto pixels =
      Run(GeometryInputShader(test), test.topology, test.vertex_count);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  const float sum =
      static_cast<float>((test.vertex_count - 1) * test.vertex_count / 2);
  const uint32_t expected = PackColor((sum + 1.0f) / 16.0f, 0.25f, 0.75f);
  for (UINT y = 0; y < kTargetHeight; ++y) {
    for (UINT x = 0; x < kTargetWidth; ++x) {
      EXPECT_TRUE(ColorMatches(pixels[y * kTargetWidth + x], expected))
          << test.name << " pixel (" << x << ", " << y << ")";
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    InputPrimitive, D3D11GeometryInputSpec,
    ::testing::ValuesIn(kGeometryInputCases),
    [](const ::testing::TestParamInfo<GeometryInputCase> &info) {
      return info.param.name;
    });

TEST_F(D3D11GeometryShaderOpsSpec,
       PrimitiveIdSelectsTwoInputPrimitivesAndTriangleStripRegions) {
  const auto pixels =
      Run(kPrimitiveIdGeometryShader, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, 2);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  EXPECT_TRUE(ColorMatches(pixels[8 * kTargetWidth + 2], PackColor(1, 0, 0)));
  EXPECT_TRUE(ColorMatches(pixels[8 * kTargetWidth + 13], PackColor(0, 1, 0)));
}

TEST_F(D3D11GeometryShaderOpsSpec, EmitsPointListOutput) {
  const auto pixels =
      Run(kPointOutputGeometryShader, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, 1);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  const uint32_t red = PackColor(1, 0, 0);
  const auto red_count =
      std::count_if(pixels.begin(), pixels.end(),
                    [red](uint32_t pixel) { return ColorMatches(pixel, red); });
  EXPECT_EQ(red_count, 3);
}

TEST_F(D3D11GeometryShaderOpsSpec,
       EmitsLineStripsAndRestartsBetweenPrimitives) {
  const auto pixels =
      Run(kLineOutputGeometryShader, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, 1);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  const uint32_t red = PackColor(1, 0, 0);
  const uint32_t green = PackColor(0, 1, 0);
  const auto red_count =
      std::count_if(pixels.begin(), pixels.end(),
                    [red](uint32_t pixel) { return ColorMatches(pixel, red); });
  const auto green_count =
      std::count_if(pixels.begin(), pixels.end(), [green](uint32_t pixel) {
        return ColorMatches(pixel, green);
      });
  EXPECT_GE(red_count, 8);
  EXPECT_GE(green_count, 8);
}

TEST_F(D3D11GeometryShaderOpsSpec,
       AppendAndRestartStripEmitMultipleTrianglePrimitives) {
  const auto pixels = Run(kMultiplePrimitiveGeometryShader,
                          D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, 1);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  EXPECT_TRUE(ColorMatches(pixels[8 * kTargetWidth + 4], PackColor(1, 0, 0)));
  EXPECT_TRUE(ColorMatches(pixels[8 * kTargetWidth + 12], PackColor(0, 1, 0)));
}

TEST_F(D3D11GeometryShaderOpsSpec,
       MaximumVertexCountDeclarationExecutesBoundaryShader) {
  const auto pixels = Run(kMaximumVertexCountGeometryShader,
                          D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, 1, true);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  EXPECT_TRUE(ColorMatches(pixels[8 * kTargetWidth + 8], PackColor(0, 1, 0)));
}

} // namespace
