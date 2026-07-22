#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <vector>

// Batched public-D3D11 stream-output coverage. The default run captures all
// logical cases with one draw and one staging readback. Exact CaseId replay
// writes only selected records at explicit SO offsets and verifies that every
// unselected record remains poison.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kStreamOutputCaseCount = 4096;
constexpr std::uint32_t kComponentsPerRecord = 4;
constexpr std::uint32_t kRecordStride =
    kComponentsPerRecord * sizeof(std::uint32_t);

const dxmt::test::LogicalCaseFamilyRegistration kStreamOutputCases(
    "D3D11StreamOutputMatrixSpec."
    "Captures4096VertexShaderRecordsWithExplicitOffsets",
    "D3D11.StreamOutput.VertexCapture.", kStreamOutputCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate",
      "CreateGeometryShaderWithStreamOutput,SOSetTargets,StagingMap"},
     dxmt::test::kGpuBatchTestCost,
     "an immutable vertex buffer containing stable logical IDs, a vertex "
     "shader emitting one uint4 TEXCOORD record per ID, and a poison-"
     "initialized default buffer bound for stream output",
     "create the stream-output shader through the public D3D11 API, bind the "
     "SO target, capture every selected vertex at its record offset, then copy "
     "the whole target to one staging buffer",
     "all four components of every selected record match the CPU evaluator "
     "and every unselected record remains poison",
     "logical ID, selection state, record byte offset, component, expected/"
     "actual value, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kStreamOutputCost("D3D11StreamOutputMatrixSpec."
                      "Captures4096VertexShaderRecordsWithExplicitOffsets",
                      dxmt::test::kGpuBatchTestCost);

using StreamOutputRecord = std::array<std::uint32_t, kComponentsPerRecord>;

std::uint32_t RotateLeft(std::uint32_t value, std::uint32_t shift) {
  shift &= 31u;
  return (value << shift) | (value >> ((32u - shift) & 31u));
}

StreamOutputRecord ExpectedRecord(std::uint32_t logical) {
  return {logical, logical * 1664525u + 1013904223u, logical ^ 0xa5a5a5a5u,
          RotateLeft(logical * 0x9e3779b9u + 0x7f4a7c15u, logical & 15u)};
}

StreamOutputRecord PoisonRecord(std::uint32_t logical) {
  const auto expected = ExpectedRecord(logical);
  return {expected[0] ^ 0xd15ea5e0u, expected[1] ^ 0x8badf00du,
          expected[2] ^ 0xc001d00du, expected[3] ^ 0x5ca1ab1eu};
}

class D3D11StreamOutputMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11StreamOutputMatrixSpec,
       Captures4096VertexShaderRecordsWithExplicitOffsets) {
  std::vector<StreamOutputRecord> expected(kStreamOutputCaseCount);
  std::vector<StreamOutputRecord> initial(kStreamOutputCaseCount);
  std::vector<bool> selected(kStreamOutputCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kStreamOutputCaseCount);
  for (std::uint32_t logical = 0; logical < kStreamOutputCaseCount; ++logical) {
    expected[logical] = ExpectedRecord(logical);
    initial[logical] = PoisonRecord(logical);
    if (dxmt::test::LogicalCaseSelected(kStreamOutputCases.family(), logical)) {
      selected[logical] = true;
      selected_cases.push_back(logical);
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto vertex = CompileShader(R"(
    struct Input {
      uint logical : TEXCOORD0;
    };
    struct Output {
      float4 position : SV_Position;
      uint4 data : TEXCOORD0;
    };

    uint rotate_left(uint value, uint shift) {
      shift &= 31u;
      return (value << shift) | (value >> ((32u - shift) & 31u));
    }

    Output main(Input input) {
      uint logical = input.logical;
      Output output;
      output.position = float4(0.0, 0.0, 0.0, 1.0);
      output.data = uint4(
          logical,
          logical * 1664525u + 1013904223u,
          logical ^ 0xa5a5a5a5u,
          rotate_left(logical * 0x9e3779b9u + 0x7f4a7c15u,
                      logical & 15u));
      return output;
    }
  )",
                                    "vs_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();

  ComPtr<ID3D11VertexShader> vertex_shader;
  ASSERT_EQ(context_.device()->CreateVertexShader(
                vertex.bytecode->GetBufferPointer(),
                vertex.bytecode->GetBufferSize(), nullptr, vertex_shader.put()),
            S_OK);

  const D3D11_INPUT_ELEMENT_DESC input_element = {
      "TEXCOORD", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA,
      0};
  ComPtr<ID3D11InputLayout> input_layout;
  ASSERT_EQ(context_.device()->CreateInputLayout(
                &input_element, 1, vertex.bytecode->GetBufferPointer(),
                vertex.bytecode->GetBufferSize(), input_layout.put()),
            S_OK);

  std::vector<std::uint32_t> logical_ids(kStreamOutputCaseCount);
  for (std::uint32_t logical = 0; logical < kStreamOutputCaseCount; ++logical)
    logical_ids[logical] = logical;
  D3D11_BUFFER_DESC input_desc = {};
  input_desc.ByteWidth = kStreamOutputCaseCount * sizeof(std::uint32_t);
  input_desc.Usage = D3D11_USAGE_IMMUTABLE;
  input_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA input_data = {};
  input_data.pSysMem = logical_ids.data();
  ComPtr<ID3D11Buffer> input;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&input_desc, &input_data, input.put()),
      S_OK);

  const D3D11_SO_DECLARATION_ENTRY declaration = {0, "TEXCOORD",           0,
                                                  0, kComponentsPerRecord, 0};
  const UINT stride = kRecordStride;
  ComPtr<ID3D11GeometryShader> stream_output_shader;
  ASSERT_EQ(context_.device()->CreateGeometryShaderWithStreamOutput(
                vertex.bytecode->GetBufferPointer(),
                vertex.bytecode->GetBufferSize(), &declaration, 1, &stride, 1,
                D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                stream_output_shader.put()),
            S_OK);
  ASSERT_NE(stream_output_shader.get(), nullptr);

  D3D11_BUFFER_DESC output_desc = {};
  output_desc.ByteWidth = kStreamOutputCaseCount * kRecordStride;
  output_desc.Usage = D3D11_USAGE_DEFAULT;
  output_desc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
  D3D11_SUBRESOURCE_DATA output_data = {};
  output_data.pSysMem = initial.data();
  ComPtr<ID3D11Buffer> output;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&output_desc, &output_data, output.put()),
      S_OK);

  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  context_.context()->IASetInputLayout(input_layout.get());
  const UINT input_stride = sizeof(std::uint32_t);
  const UINT input_offset = 0;
  ID3D11Buffer *input_buffer = input.get();
  context_.context()->IASetVertexBuffers(0, 1, &input_buffer, &input_stride,
                                         &input_offset);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->GSSetShader(stream_output_shader.get(), nullptr, 0);

  ID3D11Buffer *output_target = output.get();
  if (selected_cases.size() == kStreamOutputCaseCount) {
    const UINT offset = 0;
    context_.context()->SOSetTargets(1, &output_target, &offset);
    context_.context()->Draw(kStreamOutputCaseCount, 0);
  } else {
    for (const std::uint32_t logical : selected_cases) {
      const UINT offset = logical * kRecordStride;
      context_.context()->SOSetTargets(1, &output_target, &offset);
      context_.context()->Draw(1, logical);
    }
  }

  context_.context()->SOSetTargets(0, nullptr, nullptr);
  context_.context()->GSSetShader(nullptr, nullptr, 0);
  context_.context()->VSSetShader(nullptr, nullptr, 0);

  D3D11_BUFFER_DESC staging_desc = output_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put()),
      S_OK);
  context_.context()->CopyResource(staging.get(), output.get());
  context_.context()->Flush();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_EQ(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
      S_OK);
  const auto *actual = static_cast<const std::uint32_t *>(mapped.pData);
  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kStreamOutputCases.family().case_id_prefix);
  bool found_mismatch = false;
  for (std::uint32_t logical = 0; logical < kStreamOutputCaseCount; ++logical) {
    const auto &desired =
        selected[logical] ? expected[logical] : initial[logical];
    for (std::uint32_t component = 0; component < kComponentsPerRecord;
         ++component) {
      const std::uint32_t actual_value =
          actual[logical * kComponentsPerRecord + component];
      if (actual_value == desired[component])
        continue;

      const auto case_id =
          dxmt::test::LogicalCaseId(kStreamOutputCases.family(), logical);
      const auto replay_case_id =
          selected[logical]
              ? case_id
              : dxmt::test::LogicalCaseId(kStreamOutputCases.family(),
                                          selected_cases.front());
      ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                    << "Class: "
                    << dxmt::test::TestClassName(
                           kStreamOutputCases.family().traits.test_class)
                    << '\n'
                    << "Requirements: feature_level=11_0 shader_model=5_0 "
                       "queue=Immediate capability="
                       "CreateGeometryShaderWithStreamOutput,SOSetTargets,"
                       "StagingMap\n"
                    << "ExecutionPath: "
                    << dxmt::test::ExecutionPathName(
                           kStreamOutputCases.family().traits.execution_path)
                    << '\n'
                    << "Parameters: logical=" << logical
                    << " selected=" << (selected[logical] ? "true" : "false")
                    << " record_byte_offset=" << logical * kRecordStride
                    << " component=" << component << '\n'
                    << "GpuCaseResult: status=" << (selected[logical] ? 1u : 2u)
                    << " first_mismatch_index=" << logical << " expected=0x"
                    << std::hex << desired[component] << " actual=0x"
                    << actual_value << std::dec << '\n'
                    << "Replay: --dxmt-case-id=" << replay_case_id;
      found_mismatch = true;
      break;
    }
    if (found_mismatch)
      break;
  }
  context_.context()->Unmap(staging.get(), 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
