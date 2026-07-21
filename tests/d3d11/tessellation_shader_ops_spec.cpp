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
constexpr uint32_t kClearColor = 0xff000000u;

constexpr std::string_view kVertexShader = R"(
struct Output {
  float seed : SEED;
};

Output main(uint vertex_id : SV_VertexID) {
  Output output;
  output.seed = vertex_id + 1.0;
  return output;
}
)";

constexpr std::string_view kPixelShader = R"(
struct Input {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

float4 main(Input input) : SV_Target {
  return input.color;
}
)";

struct TessellationPartitionCase {
  const char *partition;
  const char *name;
};

constexpr std::array kPartitionCases = {
    TessellationPartitionCase{"integer", "Integer"},
    TessellationPartitionCase{"fractional_even", "FractionalEven"},
    TessellationPartitionCase{"fractional_odd", "FractionalOdd"},
};

std::string TriangleHullShader(std::string_view partition, float edge0,
                               float edge1, float edge2, float inside) {
  std::string source = R"(
struct Input {
  float seed : SEED;
};
struct ControlPoint {
  float4 position : POSITION;
  float phase_value : PHASE;
};
struct PatchConstants {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
  float marker : MARKER;
};

PatchConstants patch_constants(InputPatch<Input, 3> patch,
                               uint primitive_id : SV_PrimitiveID) {
  PatchConstants output;
  output.edges[0] = )";
  source += std::to_string(edge0);
  source += ";\n  output.edges[1] = " + std::to_string(edge1);
  source += ";\n  output.edges[2] = " + std::to_string(edge2);
  source += ";\n  output.inside = " + std::to_string(inside);
  source += R"(;
  output.marker = patch[0].seed + patch[1].seed + patch[2].seed
                  + primitive_id;
  return output;
}

[domain("tri")]
[outputcontrolpoints(3)]
[outputtopology("triangle_ccw")]
[partitioning(")";
  source += partition;
  source += R"(")]
[patchconstantfunc("patch_constants")]
[maxtessfactor(64.0)]
ControlPoint main(InputPatch<Input, 3> patch,
                  uint control_point_id : SV_OutputControlPointID) {
  ControlPoint output;
  if (control_point_id == 0)
    output.position = float4(-0.8, -0.8, 0.0, 1.0);
  else if (control_point_id == 1)
    output.position = float4(0.0, 0.8, 0.0, 1.0);
  else
    output.position = float4(0.8, -0.8, 0.0, 1.0);
  output.phase_value = patch[control_point_id].seed * 2.0;
  return output;
}
)";
  return source;
}

constexpr std::string_view kTriangleDomainShader = R"(
struct ControlPoint {
  float4 position : POSITION;
  float phase_value : PHASE;
};
struct PatchConstants {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
  float marker : MARKER;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[domain("tri")]
Output main(PatchConstants constants,
            float3 location : SV_DomainLocation,
            const OutputPatch<ControlPoint, 3> patch) {
  Output output;
  output.position = patch[0].position * location.x
                  + patch[1].position * location.y
                  + patch[2].position * location.z;
  float phase_value = patch[0].phase_value * location.x
                    + patch[1].phase_value * location.y
                    + patch[2].phase_value * location.z;
  output.color = float4(constants.marker / 6.0,
                        phase_value / 6.0, location.z, 1.0);
  return output;
}
)";

std::string QuadHullShader(std::string_view partition, float edge0, float edge1,
                           float edge2, float edge3, float inside0,
                           float inside1) {
  std::string source = R"(
struct Input {
  float seed : SEED;
};
struct ControlPoint {
  float4 position : POSITION;
  float phase_value : PHASE;
};
struct PatchConstants {
  float edges[4] : SV_TessFactor;
  float inside[2] : SV_InsideTessFactor;
  float marker : MARKER;
};

PatchConstants patch_constants(InputPatch<Input, 4> patch,
                               uint primitive_id : SV_PrimitiveID) {
  PatchConstants output;
  output.edges[0] = )";
  source += std::to_string(edge0);
  source += ";\n  output.edges[1] = " + std::to_string(edge1);
  source += ";\n  output.edges[2] = " + std::to_string(edge2);
  source += ";\n  output.edges[3] = " + std::to_string(edge3);
  source += ";\n  output.inside[0] = " + std::to_string(inside0);
  source += ";\n  output.inside[1] = " + std::to_string(inside1);
  source += R"(;
  output.marker = patch[0].seed + patch[1].seed
                  + patch[2].seed + patch[3].seed + primitive_id;
  return output;
}

[domain("quad")]
[outputcontrolpoints(4)]
[outputtopology("triangle_ccw")]
[partitioning(")";
  source += partition;
  source += R"(")]
[patchconstantfunc("patch_constants")]
[maxtessfactor(64.0)]
ControlPoint main(InputPatch<Input, 4> patch,
                  uint control_point_id : SV_OutputControlPointID) {
  ControlPoint output;
  if (control_point_id == 0)
    output.position = float4(-0.8, 0.8, 0.0, 1.0);
  else if (control_point_id == 1)
    output.position = float4(0.8, 0.8, 0.0, 1.0);
  else if (control_point_id == 2)
    output.position = float4(-0.8, -0.8, 0.0, 1.0);
  else
    output.position = float4(0.8, -0.8, 0.0, 1.0);
  output.phase_value = patch[control_point_id].seed * 2.0;
  return output;
}
)";
  return source;
}

constexpr std::string_view kQuadDomainShader = R"(
struct ControlPoint {
  float4 position : POSITION;
  float phase_value : PHASE;
};
struct PatchConstants {
  float edges[4] : SV_TessFactor;
  float inside[2] : SV_InsideTessFactor;
  float marker : MARKER;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[domain("quad")]
Output main(PatchConstants constants,
            float2 location : SV_DomainLocation,
            const OutputPatch<ControlPoint, 4> patch) {
  Output output;
  float4 top = lerp(patch[0].position, patch[1].position, location.x);
  float4 bottom = lerp(patch[2].position, patch[3].position, location.x);
  output.position = lerp(top, bottom, location.y);
  float top_value = lerp(patch[0].phase_value,
                         patch[1].phase_value, location.x);
  float bottom_value = lerp(patch[2].phase_value,
                            patch[3].phase_value, location.x);
  float phase_value = lerp(top_value, bottom_value, location.y);
  output.color = float4(constants.marker / 10.0,
                        phase_value / 8.0, location.x, 1.0);
  return output;
}
)";

std::string IsolineHullShader(std::string_view partition, float density,
                              float detail) {
  std::string source = R"(
struct Input {
  float seed : SEED;
};
struct ControlPoint {
  float phase_value : PHASE;
};
struct PatchConstants {
  float edges[2] : SV_TessFactor;
  float marker : MARKER;
};

PatchConstants patch_constants(InputPatch<Input, 2> patch,
                               uint primitive_id : SV_PrimitiveID) {
  PatchConstants output;
  output.edges[0] = )";
  source += std::to_string(density);
  source += ";\n  output.edges[1] = " + std::to_string(detail);
  source += R"(;
  output.marker = patch[0].seed + patch[1].seed + primitive_id;
  return output;
}

[domain("isoline")]
[outputcontrolpoints(2)]
[outputtopology("line")]
[partitioning(")";
  source += partition;
  source += R"(")]
[patchconstantfunc("patch_constants")]
[maxtessfactor(64.0)]
ControlPoint main(InputPatch<Input, 2> patch,
                  uint control_point_id : SV_OutputControlPointID) {
  ControlPoint output;
  output.phase_value = patch[control_point_id].seed * 2.0;
  return output;
}
)";
  return source;
}

constexpr std::string_view kIsolineDomainShader = R"(
struct ControlPoint {
  float phase_value : PHASE;
};
struct PatchConstants {
  float edges[2] : SV_TessFactor;
  float marker : MARKER;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[domain("isoline")]
Output main(PatchConstants constants,
            float2 location : SV_DomainLocation,
            const OutputPatch<ControlPoint, 2> patch) {
  Output output;
  output.position = float4(location.x * 1.5 - 0.75,
                           0.75 - location.y * 2.25, 0.0, 1.0);
  output.color = float4(1.0 - location.y, location.y,
                        constants.marker *
                            (patch[0].phase_value + patch[1].phase_value) / 18.0,
                        1.0);
  return output;
}
)";

constexpr std::string_view kCurvedIsolineDomainShader = R"(
struct ControlPoint {
  float phase_value : PHASE;
};
struct PatchConstants {
  float edges[2] : SV_TessFactor;
  float marker : MARKER;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[domain("isoline")]
Output main(PatchConstants constants,
            float2 location : SV_DomainLocation,
            const OutputPatch<ControlPoint, 2> patch) {
  Output output;
  float curve = 4.0 * location.x * (1.0 - location.x);
  output.position = float4(location.x * 1.5 - 0.75,
                           0.75 - location.y - curve * 0.2, 0.0, 1.0);
  output.color = float4(1.0 - location.y, location.y,
                        constants.marker *
                            (patch[0].phase_value + patch[1].phase_value) / 18.0,
                        1.0);
  return output;
}
)";

constexpr std::string_view kMaximumDensityIsolineDomainShader = R"(
struct ControlPoint {
  float phase_value : PHASE;
};
struct PatchConstants {
  float edges[2] : SV_TessFactor;
  float marker : MARKER;
};
struct Output {
  float4 position : SV_Position;
  float4 color : COLOR0;
};

[domain("isoline")]
Output main(PatchConstants constants,
            float2 location : SV_DomainLocation,
            const OutputPatch<ControlPoint, 2> patch) {
  Output output;
  output.position = float4(location.x * 1.5 - 0.75,
                           0.99609375 - location.y * 2.0, 0.0, 1.0);
  output.color = float4(1.0 - location.y, location.y,
                        constants.marker *
                            (patch[0].phase_value + patch[1].phase_value) / 18.0,
                        1.0);
  return output;
}
)";

uint8_t FloatToUnorm(float value) {
  return static_cast<uint8_t>(std::lround(value * 255.0f));
}

uint32_t PackColor(float red, float green, float blue, float alpha = 1.0f) {
  return static_cast<uint32_t>(FloatToUnorm(red)) |
         (static_cast<uint32_t>(FloatToUnorm(green)) << 8u) |
         (static_cast<uint32_t>(FloatToUnorm(blue)) << 16u) |
         (static_cast<uint32_t>(FloatToUnorm(alpha)) << 24u);
}

bool ColorMatches(uint32_t actual, uint32_t expected, unsigned tolerance = 4) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    const int actual_channel = (actual >> shift) & 0xff;
    const int expected_channel = (expected >> shift) & 0xff;
    if (std::abs(actual_channel - expected_channel) >
        static_cast<int>(tolerance))
      return false;
  }
  return true;
}

class D3D11TessellationShaderOpsSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    const auto vertex = CompileShader(kVertexShader, "vs_5_0");
    const auto pixel = CompileShader(kPixelShader, "ps_5_0");
    ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
    ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
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
    D3D11_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    ASSERT_EQ(context_.device()->CreateRasterizerState(&rasterizer_desc,
                                                       rasterizer_.put()),
              S_OK);
  }

  std::vector<uint32_t>
  Run(std::string_view hull_source, std::string_view domain_source,
      D3D11_PRIMITIVE_TOPOLOGY topology, UINT control_point_count,
      UINT target_width = kTargetWidth, UINT target_height = kTargetHeight) {
    const auto hull = CompileShader(hull_source, "hs_5_0");
    const auto domain = CompileShader(domain_source, "ds_5_0");
    EXPECT_EQ(hull.result, S_OK) << hull.diagnostic_text();
    EXPECT_EQ(domain.result, S_OK) << domain.diagnostic_text();
    if (FAILED(hull.result) || FAILED(domain.result))
      return {};
    ComPtr<ID3D11HullShader> hull_shader;
    ComPtr<ID3D11DomainShader> domain_shader;
    EXPECT_EQ(context_.device()->CreateHullShader(
                  hull.bytecode->GetBufferPointer(),
                  hull.bytecode->GetBufferSize(), nullptr, hull_shader.put()),
              S_OK);
    EXPECT_EQ(context_.device()->CreateDomainShader(
                  domain.bytecode->GetBufferPointer(),
                  domain.bytecode->GetBufferSize(), nullptr,
                  domain_shader.put()),
              S_OK);
    if (!hull_shader || !domain_shader)
      return {};

    D3D11_TEXTURE2D_DESC target_desc = {};
    target_desc.Width = target_width;
    target_desc.Height = target_height;
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
    EXPECT_EQ(context_.device()->CreateRenderTargetView(target.get(), nullptr,
                                                        target_view.put()),
              S_OK);
    if (!target || !target_view)
      return {};

    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context_.context()->ClearRenderTargetView(target_view.get(), clear);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(target_width);
    viewport.Height = static_cast<float>(target_height);
    viewport.MaxDepth = 1.0f;
    ID3D11RenderTargetView *target_views[] = {target_view.get()};
    context_.context()->OMSetRenderTargets(1, target_views, nullptr);
    context_.context()->RSSetViewports(1, &viewport);
    context_.context()->RSSetState(rasterizer_.get());
    context_.context()->IASetPrimitiveTopology(topology);
    context_.context()->VSSetShader(vertex_shader_.get(), nullptr, 0);
    context_.context()->HSSetShader(hull_shader.get(), nullptr, 0);
    context_.context()->DSSetShader(domain_shader.get(), nullptr, 0);
    context_.context()->PSSetShader(pixel_shader_.get(), nullptr, 0);
    context_.context()->Draw(control_point_count, 0);
    context_.context()->OMSetRenderTargets(0, nullptr, nullptr);
    context_.context()->HSSetShader(nullptr, nullptr, 0);
    context_.context()->DSSetShader(nullptr, nullptr, 0);

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
    std::vector<uint32_t> pixels(target_width * target_height);
    for (UINT y = 0; y < target_height; ++y) {
      std::memcpy(pixels.data() + y * target_width,
                  static_cast<const uint8_t *>(mapped.pData) +
                      y * mapped.RowPitch,
                  target_width * sizeof(uint32_t));
    }
    context_.context()->Unmap(staging.get(), 0);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    return pixels;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
};

class D3D11TrianglePartitionSpec
    : public D3D11TessellationShaderOpsSpec,
      public ::testing::WithParamInterface<TessellationPartitionCase> {};

TEST_P(D3D11TrianglePartitionSpec,
       ExecutesControlPointPatchConstantAndDomainPhases) {
  const auto &test = GetParam();
  const auto pixels =
      Run(TriangleHullShader(test.partition, 3.25f, 3.25f, 3.25f, 3.25f),
          kTriangleDomainShader,
          D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, 3);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  const uint32_t expected = PackColor(1.0f, 4.15625f / 6.0f, 0.30859375f, 1.0f);
  EXPECT_TRUE(ColorMatches(pixels[8 * kTargetWidth + 8], expected))
      << test.name << " center pixel was 0x" << std::hex
      << pixels[8 * kTargetWidth + 8];
  EXPECT_GT(std::count_if(pixels.begin(), pixels.end(),
                          [](uint32_t pixel) { return pixel != kClearColor; }),
            50);
}

INSTANTIATE_TEST_SUITE_P(
    Partitioning, D3D11TrianglePartitionSpec,
    ::testing::ValuesIn(kPartitionCases),
    [](const ::testing::TestParamInfo<TessellationPartitionCase> &info) {
      return info.param.name;
    });

TEST_F(D3D11TessellationShaderOpsSpec,
       QuadDomainInterpolatesControlPointsAndPatchConstants) {
  const auto pixels =
      Run(QuadHullShader("integer", 4, 4, 4, 4, 4, 4), kQuadDomainShader,
          D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST, 4);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  const uint32_t expected = PackColor(1.0f, 5.234375f / 8.0f, 0.5390625f, 1.0f);
  EXPECT_TRUE(ColorMatches(pixels[8 * kTargetWidth + 8], expected))
      << "center pixel was 0x" << std::hex << pixels[8 * kTargetWidth + 8];
}

TEST_F(D3D11TessellationShaderOpsSpec,
       IsolineDensityAndDetailGenerateIndependentLines) {
  constexpr UINT kDensity = 3;
  constexpr UINT kDetail = 4;
  constexpr UINT kSize = 64;
  const auto pixels =
      Run(IsolineHullShader("integer", kDensity, kDetail),
          kCurvedIsolineDomainShader,
          D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST, 2, kSize, kSize);
  ASSERT_EQ(pixels.size(), kSize * kSize);

  std::string missing_vertices;
  for (UINT line = 0; line < kDensity; ++line) {
    const float line_location = static_cast<float>(line) / kDensity;
    const uint32_t expected =
        PackColor(1.0f - line_location, line_location, 1.0f);
    for (UINT vertex = 0; vertex <= kDetail; ++vertex) {
      const float detail_location = static_cast<float>(vertex) / kDetail;
      const float curve =
          4.0f * detail_location * (1.0f - detail_location);
      const int center_x =
          static_cast<int>(std::lround(8.0f + detail_location * 48.0f));
      const int center_y = static_cast<int>(
          std::lround(8.0f + line_location * 32.0f + curve * 6.4f));
      bool found = false;
      for (int y = std::max(0, center_y - 2);
           y <= std::min(static_cast<int>(kSize) - 1, center_y + 2); ++y) {
        for (int x = std::max(0, center_x - 2);
             x <= std::min(static_cast<int>(kSize) - 1, center_x + 2); ++x) {
          found |= ColorMatches(
              pixels[static_cast<UINT>(y) * kSize + static_cast<UINT>(x)],
              expected);
        }
      }
      if (!found)
        missing_vertices += " (" + std::to_string(line) + "," +
                            std::to_string(vertex) + ")";
    }
  }
  EXPECT_TRUE(missing_vertices.empty())
      << "missing isoline/detail vertices:" << missing_vertices;
}

TEST_F(D3D11TessellationShaderOpsSpec, ZeroDetailFactorCullsIsolinePatch) {
  const auto pixels =
      Run(IsolineHullShader("integer", 3, 0), kIsolineDomainShader,
          D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST, 2);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(),
                          [](uint32_t pixel) { return pixel == kClearColor; }));
}

TEST_F(D3D11TessellationShaderOpsSpec,
       MaximumDensityExecutesSixtyFourIsolines) {
  constexpr UINT kDensity = 64;
  constexpr UINT kRowsPerLine = 4;
  constexpr UINT kHeight = kDensity * kRowsPerLine;
  const auto pixels = Run(IsolineHullShader("integer", kDensity, 1),
                          kMaximumDensityIsolineDomainShader,
                          D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST, 2,
                          kTargetWidth, kHeight);
  ASSERT_EQ(pixels.size(), kTargetWidth * kHeight);
  std::string missing_lines;
  for (UINT line = 0; line < kDensity; ++line) {
    const float location = static_cast<float>(line) / kDensity;
    const uint32_t expected = PackColor(1.0f - location, location, 1.0f);
    bool found = false;
    for (uint32_t pixel : pixels)
      found |= ColorMatches(pixel, expected, 1);
    if (!found)
      missing_lines += " " + std::to_string(line);
  }
  EXPECT_TRUE(missing_lines.empty()) << "missing isolines:" << missing_lines;
}

TEST_F(D3D11TessellationShaderOpsSpec, ZeroEdgeFactorCullsTrianglePatch) {
  const auto pixels =
      Run(TriangleHullShader("integer", 0, 4, 4, 4), kTriangleDomainShader,
          D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, 3);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(),
                          [](uint32_t pixel) { return pixel == kClearColor; }));
}

TEST_F(D3D11TessellationShaderOpsSpec,
       MaximumTessellationFactorExecutesTrianglePatch) {
  const auto pixels =
      Run(TriangleHullShader("integer", 64, 64, 64, 64), kTriangleDomainShader,
          D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, 3);
  ASSERT_EQ(pixels.size(), kTargetWidth * kTargetHeight);
  EXPECT_NE(pixels[8 * kTargetWidth + 8], kClearColor);
}

} // namespace
