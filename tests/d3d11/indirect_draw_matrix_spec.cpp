#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <vector>

// Batched public-D3D11 DrawInstancedIndirect coverage. Every logical case owns
// one nonzero-offset indirect argument record and one explicit stream-output
// slot. This isolates indirect StartInstanceLocation addressing from the
// separate multi-instance stream-output linearization problem.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kIndirectDrawCaseCount = 4096;
constexpr std::uint32_t kComponentsPerRecord = 4;
constexpr std::uint32_t kRecordStride =
    kComponentsPerRecord * sizeof(std::uint32_t);
constexpr UINT kPerCaseArgumentsOffset = 4 * sizeof(std::uint32_t);
constexpr UINT kArgumentsPerRecord = 4;

const dxmt::test::LogicalCaseFamilyRegistration kIndirectDrawCases(
    "D3D11IndirectDrawMatrixSpec."
    "Captures4096InstancesFromOffsetIndirectArguments",
    "D3D11.DrawInstancedIndirect.Instance.", kIndirectDrawCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate",
      "DrawInstancedIndirect,DrawIndirectArgsBuffer,StreamOutput,StagingMap"},
     dxmt::test::kGpuBatchTestCost,
     "a default-usage DRAWINDIRECT_ARGS buffer with a zero prefix guard, one "
     "record per logical case from byte offset 16, an immutable per-instance "
     "input buffer, and a poison-initialized stream-output target",
     "draw each selected instance from its own argument record and write it "
     "to its explicit stream-output offset",
     "every selected record contains the per-instance input chosen by "
     "StartInstanceLocation and every unselected record remains poison",
     "logical ID, selection state, argument and stream-output byte offsets, "
     "component, expected/actual value, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kIndirectDrawCost("D3D11IndirectDrawMatrixSpec."
                      "Captures4096InstancesFromOffsetIndirectArguments",
                      dxmt::test::kGpuBatchTestCost);

using IndirectDrawRecord = std::array<std::uint32_t, kComponentsPerRecord>;

IndirectDrawRecord ExpectedRecord(std::uint32_t logical) {
  const std::uint32_t mixed = logical * 22695477u + 1u;
  return {logical, logical ^ 0xa5a5a5a5u, mixed, mixed ^ 0x9e3779b9u};
}

IndirectDrawRecord PoisonRecord(std::uint32_t logical) {
  const auto expected = ExpectedRecord(logical);
  return {expected[0] ^ 0x13579bdfu, expected[1] ^ 0x2468ace0u,
          expected[2] ^ 0xdeadbeefu, expected[3] ^ 0xc001d00du};
}

UINT PerCaseArgumentsByteOffset(std::uint32_t logical) {
  return kPerCaseArgumentsOffset +
         logical * kArgumentsPerRecord * sizeof(std::uint32_t);
}

class D3D11IndirectDrawMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11IndirectDrawMatrixSpec,
       Captures4096InstancesFromOffsetIndirectArguments) {
  std::vector<IndirectDrawRecord> expected(kIndirectDrawCaseCount);
  std::vector<IndirectDrawRecord> initial(kIndirectDrawCaseCount);
  std::vector<bool> selected(kIndirectDrawCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kIndirectDrawCaseCount);
  for (std::uint32_t logical = 0; logical < kIndirectDrawCaseCount; ++logical) {
    expected[logical] = ExpectedRecord(logical);
    initial[logical] = PoisonRecord(logical);
    if (dxmt::test::LogicalCaseSelected(kIndirectDrawCases.family(), logical)) {
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

    Output main(Input input) {
      uint logical = input.logical;
      uint mixed = logical * 22695477u + 1u;
      Output output;
      output.position = float4(0.0, 0.0, 0.0, 1.0);
      output.data = uint4(logical, logical ^ 0xa5a5a5a5u, mixed,
                          mixed ^ 0x9e3779b9u);
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
      "TEXCOORD", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA,
      1};
  ComPtr<ID3D11InputLayout> input_layout;
  ASSERT_EQ(context_.device()->CreateInputLayout(
                &input_element, 1, vertex.bytecode->GetBufferPointer(),
                vertex.bytecode->GetBufferSize(), input_layout.put()),
            S_OK);

  std::vector<std::uint32_t> logical_ids(kIndirectDrawCaseCount);
  for (std::uint32_t logical = 0; logical < kIndirectDrawCaseCount; ++logical)
    logical_ids[logical] = logical;
  D3D11_BUFFER_DESC input_desc = {};
  input_desc.ByteWidth =
      static_cast<UINT>(logical_ids.size() * sizeof(logical_ids[0]));
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
  const UINT stream_output_stride = kRecordStride;
  ComPtr<ID3D11GeometryShader> stream_output_shader;
  ASSERT_EQ(context_.device()->CreateGeometryShaderWithStreamOutput(
                vertex.bytecode->GetBufferPointer(),
                vertex.bytecode->GetBufferSize(), &declaration, 1,
                &stream_output_stride, 1, D3D11_SO_NO_RASTERIZED_STREAM,
                nullptr, stream_output_shader.put()),
            S_OK);

  D3D11_BUFFER_DESC output_desc = {};
  output_desc.ByteWidth = kIndirectDrawCaseCount * kRecordStride;
  output_desc.Usage = D3D11_USAGE_DEFAULT;
  output_desc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
  D3D11_SUBRESOURCE_DATA output_data = {};
  output_data.pSysMem = initial.data();
  ComPtr<ID3D11Buffer> output;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&output_desc, &output_data, output.put()),
      S_OK);

  std::vector<std::uint32_t> arguments(
      4u + kIndirectDrawCaseCount * kArgumentsPerRecord, 0u);
  for (std::uint32_t logical = 0; logical < kIndirectDrawCaseCount; ++logical) {
    const std::size_t base = 4u + logical * kArgumentsPerRecord;
    arguments[base] = 1u;
    arguments[base + 1u] = 1u;
    arguments[base + 2u] = 0u;
    arguments[base + 3u] = logical;
  }

  D3D11_BUFFER_DESC arguments_desc = {};
  arguments_desc.ByteWidth =
      static_cast<UINT>(arguments.size() * sizeof(arguments[0]));
  arguments_desc.Usage = D3D11_USAGE_DEFAULT;
  arguments_desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
  D3D11_SUBRESOURCE_DATA arguments_data = {};
  arguments_data.pSysMem = arguments.data();
  ComPtr<ID3D11Buffer> arguments_buffer;
  ASSERT_EQ(context_.device()->CreateBuffer(&arguments_desc, &arguments_data,
                                            arguments_buffer.put()),
            S_OK);

  context_.context()->IASetInputLayout(input_layout.get());
  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  const UINT input_stride = sizeof(std::uint32_t);
  const UINT input_offset = 0;
  ID3D11Buffer *input_buffer = input.get();
  context_.context()->IASetVertexBuffers(0, 1, &input_buffer, &input_stride,
                                         &input_offset);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->GSSetShader(stream_output_shader.get(), nullptr, 0);

  ID3D11Buffer *output_target = output.get();
  for (const std::uint32_t logical : selected_cases) {
    const UINT output_offset = logical * kRecordStride;
    context_.context()->SOSetTargets(1, &output_target, &output_offset);
    context_.context()->DrawInstancedIndirect(
        arguments_buffer.get(), PerCaseArgumentsByteOffset(logical));
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
                 kIndirectDrawCases.family().case_id_prefix);
  bool found_mismatch = false;
  for (std::uint32_t logical = 0; logical < kIndirectDrawCaseCount; ++logical) {
    const auto &desired =
        selected[logical] ? expected[logical] : initial[logical];
    for (std::uint32_t component = 0; component < kComponentsPerRecord;
         ++component) {
      const std::uint32_t actual_value =
          actual[logical * kComponentsPerRecord + component];
      if (actual_value == desired[component])
        continue;

      const auto case_id =
          dxmt::test::LogicalCaseId(kIndirectDrawCases.family(), logical);
      const auto replay_case_id =
          selected[logical]
              ? case_id
              : dxmt::test::LogicalCaseId(kIndirectDrawCases.family(),
                                          selected_cases.front());
      const UINT arguments_offset = PerCaseArgumentsByteOffset(logical);
      ADD_FAILURE()
          << "LogicalCaseId: " << case_id << '\n'
          << "Class: "
          << dxmt::test::TestClassName(
                 kIndirectDrawCases.family().traits.test_class)
          << '\n'
          << "Requirements: feature_level=11_0 shader_model=5_0 "
             "queue=Immediate capability=DrawInstancedIndirect,"
             "DrawIndirectArgsBuffer,StreamOutput,StagingMap\n"
          << "ExecutionPath: "
          << dxmt::test::ExecutionPathName(
                 kIndirectDrawCases.family().traits.execution_path)
          << '\n'
          << "Parameters: logical=" << logical
          << " selected=" << (selected[logical] ? "true" : "false")
          << " arguments_byte_offset=" << arguments_offset
          << " stream_output_byte_offset=" << logical * kRecordStride
          << " component=" << component << " observed_logical_edges=("
          << actual[0] << ',' << actual[kComponentsPerRecord] << ','
          << actual[2u * kComponentsPerRecord] << ','
          << actual[(kIndirectDrawCaseCount - 1u) * kComponentsPerRecord]
          << ")\n"
          << "GpuCaseResult: status=" << (selected[logical] ? 1u : 2u)
          << " first_mismatch_index=" << logical << " expected=0x" << std::hex
          << desired[component] << " actual=0x" << actual_value << std::dec
          << '\n'
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
