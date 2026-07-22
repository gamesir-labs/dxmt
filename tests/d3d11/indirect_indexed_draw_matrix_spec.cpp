#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <vector>

// Batched public-D3D11 DrawIndexedInstancedIndirect coverage. Each argument
// record selects a distinct index-buffer entry, base vertex, and first
// instance. The selected vertex and instance values are captured through
// stream output without relying on DXMT implementation details.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kIndirectIndexedDrawCaseCount = 4096;
constexpr std::uint32_t kComponentsPerRecord = 4;
constexpr std::uint32_t kRecordStride =
    kComponentsPerRecord * sizeof(std::uint32_t);
constexpr UINT kArgumentsPerRecord = 5;
constexpr UINT kPerCaseArgumentsOffset =
    kArgumentsPerRecord * sizeof(std::uint32_t);

const dxmt::test::LogicalCaseFamilyRegistration kIndirectIndexedDrawCases(
    "D3D11IndirectIndexedDrawMatrixSpec."
    "Captures4096IndexedInstancesFromOffsetIndirectArguments",
    "D3D11.DrawIndexedInstancedIndirect.Instance.",
    kIndirectIndexedDrawCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate",
      "DrawIndexedInstancedIndirect,DrawIndirectArgsBuffer,IndexBuffer,"
      "StreamOutput,StagingMap"},
     dxmt::test::kGpuBatchTestCost,
     "a default-usage DRAWINDIRECT_ARGS buffer with a zero prefix guard and "
     "one five-field record per logical case, immutable vertex, instance, "
     "and index buffers, and a poison-initialized stream-output target",
     "draw each selected indexed instance from its own nonzero-offset "
     "argument record into an explicit stream-output slot",
     "every selected record contains the vertex chosen by StartIndexLocation "
     "plus BaseVertexLocation and the instance chosen by "
     "StartInstanceLocation; every unselected record remains poison",
     "logical ID, selection state, argument and stream-output byte offsets, "
     "argument fields, component, expected/actual value, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration kIndirectIndexedDrawCost(
    "D3D11IndirectIndexedDrawMatrixSpec."
    "Captures4096IndexedInstancesFromOffsetIndirectArguments",
    dxmt::test::kGpuBatchTestCost);

using IndirectIndexedDrawRecord =
    std::array<std::uint32_t, kComponentsPerRecord>;

IndirectIndexedDrawRecord ExpectedRecord(std::uint32_t logical) {
  const std::uint32_t mixed = logical * 747796405u + 2891336453u;
  return {logical, logical ^ 0x6c8e9cf5u, mixed, mixed ^ logical ^ 0x9e3779b9u};
}

IndirectIndexedDrawRecord PoisonRecord(std::uint32_t logical) {
  const auto expected = ExpectedRecord(logical);
  return {expected[0] ^ 0x13579bdfu, expected[1] ^ 0x2468ace0u,
          expected[2] ^ 0xdeadbeefu, expected[3] ^ 0xc001d00du};
}

UINT PerCaseArgumentsByteOffset(std::uint32_t logical) {
  return kPerCaseArgumentsOffset +
         logical * kArgumentsPerRecord * sizeof(std::uint32_t);
}

class D3D11IndirectIndexedDrawMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11IndirectIndexedDrawMatrixSpec,
       Captures4096IndexedInstancesFromOffsetIndirectArguments) {
  std::vector<IndirectIndexedDrawRecord> expected(
      kIndirectIndexedDrawCaseCount);
  std::vector<IndirectIndexedDrawRecord> initial(kIndirectIndexedDrawCaseCount);
  std::vector<bool> selected(kIndirectIndexedDrawCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kIndirectIndexedDrawCaseCount);
  for (std::uint32_t logical = 0; logical < kIndirectIndexedDrawCaseCount;
       ++logical) {
    expected[logical] = ExpectedRecord(logical);
    initial[logical] = PoisonRecord(logical);
    if (dxmt::test::LogicalCaseSelected(kIndirectIndexedDrawCases.family(),
                                        logical)) {
      selected[logical] = true;
      selected_cases.push_back(logical);
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto vertex = CompileShader(R"(
    struct Input {
      uint vertex_logical : TEXCOORD0;
      uint instance_logical : TEXCOORD1;
    };
    struct Output {
      float4 position : SV_Position;
      uint4 data : TEXCOORD0;
    };

    Output main(Input input) {
      uint mixed = input.vertex_logical * 747796405u + 2891336453u;
      Output output;
      output.position = float4(0.0, 0.0, 0.0, 1.0);
      output.data = uint4(input.vertex_logical,
                          input.instance_logical ^ 0x6c8e9cf5u, mixed,
                          mixed ^ input.instance_logical ^ 0x9e3779b9u);
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

  const D3D11_INPUT_ELEMENT_DESC input_elements[] = {
      {"TEXCOORD", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA,
       0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32_UINT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA,
       1},
  };
  ComPtr<ID3D11InputLayout> input_layout;
  ASSERT_EQ(context_.device()->CreateInputLayout(
                input_elements, std::size(input_elements),
                vertex.bytecode->GetBufferPointer(),
                vertex.bytecode->GetBufferSize(), input_layout.put()),
            S_OK);

  std::vector<std::uint32_t> logical_ids(kIndirectIndexedDrawCaseCount);
  for (std::uint32_t logical = 0; logical < kIndirectIndexedDrawCaseCount;
       ++logical)
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

  std::vector<std::uint32_t> indices(kIndirectIndexedDrawCaseCount, 0u);
  D3D11_BUFFER_DESC index_desc = {};
  index_desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(indices[0]));
  index_desc.Usage = D3D11_USAGE_IMMUTABLE;
  index_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  D3D11_SUBRESOURCE_DATA index_data = {};
  index_data.pSysMem = indices.data();
  ComPtr<ID3D11Buffer> index_buffer;
  ASSERT_EQ(context_.device()->CreateBuffer(&index_desc, &index_data,
                                            index_buffer.put()),
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
  output_desc.ByteWidth = kIndirectIndexedDrawCaseCount * kRecordStride;
  output_desc.Usage = D3D11_USAGE_DEFAULT;
  output_desc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
  D3D11_SUBRESOURCE_DATA output_data = {};
  output_data.pSysMem = initial.data();
  ComPtr<ID3D11Buffer> output;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&output_desc, &output_data, output.put()),
      S_OK);

  std::vector<std::uint32_t> arguments(
      kArgumentsPerRecord * (1u + kIndirectIndexedDrawCaseCount), 0u);
  for (std::uint32_t logical = 0; logical < kIndirectIndexedDrawCaseCount;
       ++logical) {
    const std::size_t base =
        kArgumentsPerRecord * static_cast<std::size_t>(1u + logical);
    arguments[base] = 1u;
    arguments[base + 1u] = 1u;
    arguments[base + 2u] = logical;
    arguments[base + 3u] = logical;
    arguments[base + 4u] = logical;
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
  const UINT input_strides[] = {sizeof(std::uint32_t), sizeof(std::uint32_t)};
  const UINT input_offsets[] = {0, 0};
  ID3D11Buffer *input_buffers[] = {input.get(), input.get()};
  context_.context()->IASetVertexBuffers(0, 2, input_buffers, input_strides,
                                         input_offsets);
  context_.context()->IASetIndexBuffer(index_buffer.get(), DXGI_FORMAT_R32_UINT,
                                       0);
  context_.context()->VSSetShader(vertex_shader.get(), nullptr, 0);
  context_.context()->GSSetShader(stream_output_shader.get(), nullptr, 0);

  ID3D11Buffer *output_target = output.get();
  for (const std::uint32_t logical : selected_cases) {
    const UINT output_offset = logical * kRecordStride;
    context_.context()->SOSetTargets(1, &output_target, &output_offset);
    context_.context()->DrawIndexedInstancedIndirect(
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
                 kIndirectIndexedDrawCases.family().case_id_prefix);
  bool found_mismatch = false;
  for (std::uint32_t logical = 0; logical < kIndirectIndexedDrawCaseCount;
       ++logical) {
    const auto &desired =
        selected[logical] ? expected[logical] : initial[logical];
    for (std::uint32_t component = 0; component < kComponentsPerRecord;
         ++component) {
      const std::uint32_t actual_value =
          actual[logical * kComponentsPerRecord + component];
      if (actual_value == desired[component])
        continue;

      const auto case_id = dxmt::test::LogicalCaseId(
          kIndirectIndexedDrawCases.family(), logical);
      const auto replay_case_id =
          selected[logical]
              ? case_id
              : dxmt::test::LogicalCaseId(kIndirectIndexedDrawCases.family(),
                                          selected_cases.front());
      ADD_FAILURE()
          << "LogicalCaseId: " << case_id << '\n'
          << "Class: "
          << dxmt::test::TestClassName(
                 kIndirectIndexedDrawCases.family().traits.test_class)
          << '\n'
          << "Requirements: feature_level=11_0 shader_model=5_0 "
             "queue=Immediate capability=DrawIndexedInstancedIndirect,"
             "DrawIndirectArgsBuffer,IndexBuffer,StreamOutput,StagingMap\n"
          << "ExecutionPath: "
          << dxmt::test::ExecutionPathName(
                 kIndirectIndexedDrawCases.family().traits.execution_path)
          << '\n'
          << "Parameters: logical=" << logical
          << " selected=" << (selected[logical] ? "true" : "false")
          << " arguments_byte_offset=" << PerCaseArgumentsByteOffset(logical)
          << " stream_output_byte_offset=" << logical * kRecordStride
          << " index_count=1 instance_count=1 start_index=" << logical
          << " base_vertex=" << logical << " start_instance=" << logical
          << " component=" << component << " observed_logical_edges=("
          << actual[0] << ',' << actual[kComponentsPerRecord] << ','
          << actual[2u * kComponentsPerRecord] << ','
          << actual[(kIndirectIndexedDrawCaseCount - 1u) * kComponentsPerRecord]
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
