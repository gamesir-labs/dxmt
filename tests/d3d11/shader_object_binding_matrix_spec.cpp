#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Public-D3D11 shader-object binding-state coverage. Four distinct public-API
// shader objects at each of six stages form exactly 4^6 = 4096 pipeline states.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kShaderObjectBindingCaseCount = 4096;
constexpr UINT kVariantsPerStage = 4;
constexpr UINT kShaderStageCount = 6;

const dxmt::test::LogicalCaseFamilyRegistration kShaderObjectBindingCases(
    "D3D11ShaderObjectBindingMatrixSpec."
    "RoundTrips4096SixStageShaderCombinations",
    "D3D11.GetShader.Binding.", kShaderObjectBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate",
      "D3DCompile,CreateVertexShader,CreatePixelShader,CreateGeometryShader,"
      "CreateHullShader,CreateDomainShader,CreateComputeShader,VSSetShader,"
      "VSGetShader,PSSetShader,PSGetShader,GSSetShader,GSGetShader,HSSetShader,"
      "HSGetShader,DSSetShader,DSGetShader,CSSetShader,CSGetShader,"
      "ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "four distinctly compiled public-API shader objects for each of the six "
     "programmable stages",
     "bind every one of the 4096 six-stage shader combinations, query each "
     "shader and its zero class-instance count, release getter references, "
     "then unbind and query every stage again",
     "each stage returns the exact selected shader object with no class "
     "instances, and every stage returns null after unbinding",
     "logical ID, selected-case count, six shader variant indexes, failing "
     "stage and phase, expected and actual addresses, class-instance count, "
     "and exact replay argument"});

const dxmt::test::TestCostRegistration
    kShaderObjectBindingCost("D3D11ShaderObjectBindingMatrixSpec."
                             "RoundTrips4096SixStageShaderCombinations",
                             dxmt::test::kResourceTestCost);

enum class ShaderStage : UINT {
  Vertex,
  Pixel,
  Geometry,
  Hull,
  Domain,
  Compute
};

const char *ShaderStageName(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return "VS";
  case ShaderStage::Pixel:
    return "PS";
  case ShaderStage::Geometry:
    return "GS";
  case ShaderStage::Hull:
    return "HS";
  case ShaderStage::Domain:
    return "DS";
  case ShaderStage::Compute:
    return "CS";
  }
  return "unknown";
}

std::string VariantLiteral(UINT variant) {
  return std::to_string(variant + 1u);
}

std::string VertexShaderSource(UINT variant) {
  return "float4 main(uint id : SV_VertexID) : SV_Position { return float4(" +
         VariantLiteral(variant) + ".0 / 64.0, (float)id / 64.0, 0.0, 1.0); }";
}

std::string PixelShaderSource(UINT variant) {
  return "float4 main() : SV_Target { return float4(" +
         VariantLiteral(variant) + ".0 / 4.0, 0.0, 0.0, 1.0); }";
}

std::string GeometryShaderSource(UINT variant) {
  return R"(
struct Vertex { float4 position : SV_Position; };
[maxvertexcount(1)]
void main(point Vertex input[1], inout PointStream<Vertex> stream) {
  Vertex output = input[0];
  output.position.x += )" +
         VariantLiteral(variant) +
         R"(.0 / 64.0;
  stream.Append(output);
}
)";
}

std::string HullShaderSource(UINT variant) {
  return R"(
struct ControlPoint { float4 position : POSITION; };
struct PatchConstants {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};
PatchConstants patch_constants(InputPatch<ControlPoint, 3> patch) {
  PatchConstants output;
  output.edges[0] = )" +
         VariantLiteral(variant) +
         R"(.0;
  output.edges[1] = 1.0;
  output.edges[2] = 1.0;
  output.inside = 1.0;
  return output;
}
[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patch_constants")]
ControlPoint main(InputPatch<ControlPoint, 3> patch,
                  uint id : SV_OutputControlPointID) {
  ControlPoint output = patch[id];
  output.position.x += )" +
         VariantLiteral(variant) +
         R"(.0 / 64.0;
  return output;
}
)";
}

std::string DomainShaderSource(UINT variant) {
  return R"(
struct ControlPoint { float4 position : POSITION; };
struct PatchConstants {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};
[domain("tri")]
float4 main(PatchConstants constants, float3 location : SV_DomainLocation,
            const OutputPatch<ControlPoint, 3> patch) : SV_Position {
  float4 position = patch[0].position * location.x
                  + patch[1].position * location.y
                  + patch[2].position * location.z;
  position.x += )" +
         VariantLiteral(variant) +
         R"(.0 / 64.0;
  return position;
}
)";
}

std::string ComputeShaderSource(UINT variant) {
  return R"(
RWByteAddressBuffer output_buffer : register(u0);
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  output_buffer.Store(0, id.x + )" +
         VariantLiteral(variant) +
         R"(u);
}
)";
}

void GetStageShader(ID3D11DeviceContext *context, ShaderStage stage,
                    IUnknown **shader, UINT *class_instance_count) {
  *shader = nullptr;
  switch (stage) {
  case ShaderStage::Vertex: {
    ID3D11VertexShader *typed = nullptr;
    context->VSGetShader(&typed, nullptr, class_instance_count);
    *shader = typed;
    break;
  }
  case ShaderStage::Pixel: {
    ID3D11PixelShader *typed = nullptr;
    context->PSGetShader(&typed, nullptr, class_instance_count);
    *shader = typed;
    break;
  }
  case ShaderStage::Geometry: {
    ID3D11GeometryShader *typed = nullptr;
    context->GSGetShader(&typed, nullptr, class_instance_count);
    *shader = typed;
    break;
  }
  case ShaderStage::Hull: {
    ID3D11HullShader *typed = nullptr;
    context->HSGetShader(&typed, nullptr, class_instance_count);
    *shader = typed;
    break;
  }
  case ShaderStage::Domain: {
    ID3D11DomainShader *typed = nullptr;
    context->DSGetShader(&typed, nullptr, class_instance_count);
    *shader = typed;
    break;
  }
  case ShaderStage::Compute: {
    ID3D11ComputeShader *typed = nullptr;
    context->CSGetShader(&typed, nullptr, class_instance_count);
    *shader = typed;
    break;
  }
  }
}

struct ShaderPools {
  std::array<ComPtr<ID3D11VertexShader>, kVariantsPerStage> vertex;
  std::array<ComPtr<ID3D11PixelShader>, kVariantsPerStage> pixel;
  std::array<ComPtr<ID3D11GeometryShader>, kVariantsPerStage> geometry;
  std::array<ComPtr<ID3D11HullShader>, kVariantsPerStage> hull;
  std::array<ComPtr<ID3D11DomainShader>, kVariantsPerStage> domain;
  std::array<ComPtr<ID3D11ComputeShader>, kVariantsPerStage> compute;

  IUnknown *at(ShaderStage stage, UINT variant) const {
    switch (stage) {
    case ShaderStage::Vertex:
      return vertex[variant].get();
    case ShaderStage::Pixel:
      return pixel[variant].get();
    case ShaderStage::Geometry:
      return geometry[variant].get();
    case ShaderStage::Hull:
      return hull[variant].get();
    case ShaderStage::Domain:
      return domain[variant].get();
    case ShaderStage::Compute:
      return compute[variant].get();
    }
    return nullptr;
  }
};

class D3D11ShaderObjectBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ShaderObjectBindingMatrixSpec,
       RoundTrips4096SixStageShaderCombinations) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kShaderObjectBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kShaderObjectBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kShaderObjectBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  ShaderPools shaders;
  for (UINT variant = 0; variant < kVariantsPerStage; ++variant) {
    const auto vertex = CompileShader(VertexShaderSource(variant), "vs_5_0");
    const auto pixel = CompileShader(PixelShaderSource(variant), "ps_5_0");
    const auto geometry =
        CompileShader(GeometryShaderSource(variant), "gs_5_0");
    const auto hull = CompileShader(HullShaderSource(variant), "hs_5_0");
    const auto domain = CompileShader(DomainShaderSource(variant), "ds_5_0");
    const auto compute = CompileShader(ComputeShaderSource(variant), "cs_5_0");
    ASSERT_EQ(vertex.result, S_OK)
        << "stage=VS variant=" << variant << ' ' << vertex.diagnostic_text();
    ASSERT_EQ(pixel.result, S_OK)
        << "stage=PS variant=" << variant << ' ' << pixel.diagnostic_text();
    ASSERT_EQ(geometry.result, S_OK)
        << "stage=GS variant=" << variant << ' ' << geometry.diagnostic_text();
    ASSERT_EQ(hull.result, S_OK)
        << "stage=HS variant=" << variant << ' ' << hull.diagnostic_text();
    ASSERT_EQ(domain.result, S_OK)
        << "stage=DS variant=" << variant << ' ' << domain.diagnostic_text();
    ASSERT_EQ(compute.result, S_OK)
        << "stage=CS variant=" << variant << ' ' << compute.diagnostic_text();

    ASSERT_EQ(context_.device()->CreateVertexShader(
                  vertex.bytecode->GetBufferPointer(),
                  vertex.bytecode->GetBufferSize(), nullptr,
                  shaders.vertex[variant].put()),
              S_OK)
        << "variant=" << variant;
    ASSERT_EQ(context_.device()->CreatePixelShader(
                  pixel.bytecode->GetBufferPointer(),
                  pixel.bytecode->GetBufferSize(), nullptr,
                  shaders.pixel[variant].put()),
              S_OK)
        << "variant=" << variant;
    ASSERT_EQ(context_.device()->CreateGeometryShader(
                  geometry.bytecode->GetBufferPointer(),
                  geometry.bytecode->GetBufferSize(), nullptr,
                  shaders.geometry[variant].put()),
              S_OK)
        << "variant=" << variant;
    ASSERT_EQ(context_.device()->CreateHullShader(
                  hull.bytecode->GetBufferPointer(),
                  hull.bytecode->GetBufferSize(), nullptr,
                  shaders.hull[variant].put()),
              S_OK)
        << "variant=" << variant;
    ASSERT_EQ(context_.device()->CreateDomainShader(
                  domain.bytecode->GetBufferPointer(),
                  domain.bytecode->GetBufferSize(), nullptr,
                  shaders.domain[variant].put()),
              S_OK)
        << "variant=" << variant;
    ASSERT_EQ(context_.device()->CreateComputeShader(
                  compute.bytecode->GetBufferPointer(),
                  compute.bytecode->GetBufferSize(), nullptr,
                  shaders.compute[variant].put()),
              S_OK)
        << "variant=" << variant;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kShaderObjectBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    std::array<UINT, kShaderStageCount> variants = {};
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index)
      variants[stage_index] = (logical >> (stage_index * 2u)) & 3u;

    context_.context()->VSSetShader(shaders.vertex[variants[0]].get(), nullptr,
                                    0);
    context_.context()->PSSetShader(shaders.pixel[variants[1]].get(), nullptr,
                                    0);
    context_.context()->GSSetShader(shaders.geometry[variants[2]].get(),
                                    nullptr, 0);
    context_.context()->HSSetShader(shaders.hull[variants[3]].get(), nullptr,
                                    0);
    context_.context()->DSSetShader(shaders.domain[variants[4]].get(), nullptr,
                                    0);
    context_.context()->CSSetShader(shaders.compute[variants[5]].get(), nullptr,
                                    0);

    bool failed = false;
    UINT failing_stage = kShaderStageCount;
    const char *failing_phase = "none";
    const void *expected_address = nullptr;
    const void *actual_address = nullptr;
    UINT actual_class_instance_count = 0;
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      const ShaderStage stage = static_cast<ShaderStage>(stage_index);
      IUnknown *actual = nullptr;
      UINT class_instance_count = 256;
      GetStageShader(context_.context(), stage, &actual, &class_instance_count);
      IUnknown *expected = shaders.at(stage, variants[stage_index]);
      if (!failed && actual != expected) {
        failed = true;
        failing_stage = stage_index;
        failing_phase = "shader_get";
        expected_address = expected;
        actual_address = actual;
        actual_class_instance_count = class_instance_count;
      } else if (!failed && class_instance_count != 0) {
        failed = true;
        failing_stage = stage_index;
        failing_phase = "class_instance_count";
        expected_address = expected;
        actual_address = actual;
        actual_class_instance_count = class_instance_count;
      }
      if (actual)
        actual->Release();
    }

    context_.context()->VSSetShader(nullptr, nullptr, 0);
    context_.context()->PSSetShader(nullptr, nullptr, 0);
    context_.context()->GSSetShader(nullptr, nullptr, 0);
    context_.context()->HSSetShader(nullptr, nullptr, 0);
    context_.context()->DSSetShader(nullptr, nullptr, 0);
    context_.context()->CSSetShader(nullptr, nullptr, 0);
    for (UINT stage_index = 0; stage_index < kShaderStageCount; ++stage_index) {
      IUnknown *actual = nullptr;
      UINT class_instance_count = 256;
      GetStageShader(context_.context(), static_cast<ShaderStage>(stage_index),
                     &actual, &class_instance_count);
      if (!failed && actual) {
        failed = true;
        failing_stage = stage_index;
        failing_phase = "unbind_shader";
        actual_address = actual;
        actual_class_instance_count = class_instance_count;
      } else if (!failed && class_instance_count != 0) {
        failed = true;
        failing_stage = stage_index;
        failing_phase = "unbind_class_instance_count";
        actual_class_instance_count = class_instance_count;
      }
      if (actual)
        actual->Release();
    }

    if (!failed)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kShaderObjectBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kShaderObjectBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=5_0 "
           "queue=Immediate capability=D3DCompile,CreateShader,SetShader,"
           "GetShader,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kShaderObjectBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical << " variants=(" << variants[0]
        << ',' << variants[1] << ',' << variants[2] << ',' << variants[3] << ','
        << variants[4] << ',' << variants[5]
        << ") selected_cases=" << selected_cases.size() << '\n'
        << "Observed: stage="
        << ShaderStageName(static_cast<ShaderStage>(failing_stage))
        << " phase=" << failing_phase << " expected_shader=" << expected_address
        << " actual_shader=" << actual_address
        << " class_instance_count=" << actual_class_instance_count << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  context_.context()->VSSetShader(nullptr, nullptr, 0);
  context_.context()->PSSetShader(nullptr, nullptr, 0);
  context_.context()->GSSetShader(nullptr, nullptr, 0);
  context_.context()->HSSetShader(nullptr, nullptr, 0);
  context_.context()->DSSetShader(nullptr, nullptr, 0);
  context_.context()->CSSetShader(nullptr, nullptr, 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
