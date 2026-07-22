#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <vector>

// Batched public-D3D11 DispatchIndirect coverage. One indirect dispatch
// launches a 16x16x16 group grid. Every group owns one output record, so the
// default run covers 4096 logical cases with one command and one staging
// readback. Exact CaseId replay uses an immutable selection mask and verifies
// all other records remain poison.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kIndirectAxisSize = 16;
constexpr std::uint32_t kIndirectCaseCount =
    kIndirectAxisSize * kIndirectAxisSize * kIndirectAxisSize;
constexpr std::uint32_t kComponentsPerRecord = 4;
constexpr UINT kIndirectArgumentsOffset = 4 * sizeof(std::uint32_t);

const dxmt::test::LogicalCaseFamilyRegistration kIndirectDispatchCases(
    "D3D11IndirectDispatchMatrixSpec."
    "Dispatches4096ThreeDimensionalGroupsFromOffsetArguments",
    "D3D11.DispatchIndirect.Group.", kIndirectCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Immediate",
      "DispatchIndirect,DrawIndirectArgsBuffer,TypedUAV,StagingMap"},
     dxmt::test::kGpuBatchTestCost,
     "an immutable DRAWINDIRECT_ARGS buffer containing a zero prefix guard "
     "followed by 16x16x16 group counts at byte offset 16, plus an immutable "
     "logical-case selection SRV and a poison-initialized typed UAV",
     "dispatch indirectly from the nonzero argument offset and let every "
     "selected three-dimensional SV_GroupID write its independent uint4 "
     "record",
     "every selected record matches the CPU group-coordinate evaluator and "
     "every unselected record remains poison",
     "logical ID, group coordinates, selection state, argument byte offset, "
     "component, expected/actual value, and exact replay argument"});

const dxmt::test::TestCostRegistration kIndirectDispatchCost(
    "D3D11IndirectDispatchMatrixSpec."
    "Dispatches4096ThreeDimensionalGroupsFromOffsetArguments",
    dxmt::test::kGpuBatchTestCost);

using IndirectRecord = std::array<std::uint32_t, kComponentsPerRecord>;

IndirectRecord ExpectedRecord(std::uint32_t logical) {
  const std::uint32_t x = logical % kIndirectAxisSize;
  const std::uint32_t y = (logical / kIndirectAxisSize) % kIndirectAxisSize;
  const std::uint32_t z = logical / (kIndirectAxisSize * kIndirectAxisSize);
  const std::uint32_t mixed = logical * 1664525u + 1013904223u;
  return {logical, 0xd0000000u | x | (y << 8u) | (z << 16u), mixed,
          mixed ^ 0xa5a5a5a5u};
}

IndirectRecord PoisonRecord(std::uint32_t logical) {
  const auto expected = ExpectedRecord(logical);
  return {expected[0] ^ 0x13579bdfu, expected[1] ^ 0x2468ace0u,
          expected[2] ^ 0xdeadbeefu, expected[3] ^ 0xc001d00du};
}

class D3D11IndirectDispatchMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11IndirectDispatchMatrixSpec,
       Dispatches4096ThreeDimensionalGroupsFromOffsetArguments) {
  std::vector<std::uint32_t> selection(kIndirectCaseCount, 0u);
  std::vector<IndirectRecord> expected(kIndirectCaseCount);
  std::vector<IndirectRecord> initial(kIndirectCaseCount);
  std::vector<bool> selected(kIndirectCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kIndirectCaseCount);
  for (std::uint32_t logical = 0; logical < kIndirectCaseCount; ++logical) {
    expected[logical] = ExpectedRecord(logical);
    initial[logical] = PoisonRecord(logical);
    if (dxmt::test::LogicalCaseSelected(kIndirectDispatchCases.family(),
                                        logical)) {
      selection[logical] = 1u;
      selected[logical] = true;
      selected_cases.push_back(logical);
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto compute = CompileShader(R"(
    Buffer<uint> selection : register(t0);
    RWBuffer<uint4> output : register(u0);

    [numthreads(1, 1, 1)]
    void main(uint3 group : SV_GroupID) {
      uint logical = group.x + 16u * (group.y + 16u * group.z);
      if (selection[logical] == 0u)
        return;

      uint mixed = logical * 1664525u + 1013904223u;
      output[logical] = uint4(
          logical,
          0xd0000000u | group.x | (group.y << 8u) | (group.z << 16u),
          mixed,
          mixed ^ 0xa5a5a5a5u);
    }
  )",
                                     "cs_5_0");
  ASSERT_EQ(compute.result, S_OK) << compute.diagnostic_text();

  ComPtr<ID3D11ComputeShader> compute_shader;
  ASSERT_EQ(context_.device()->CreateComputeShader(
                compute.bytecode->GetBufferPointer(),
                compute.bytecode->GetBufferSize(), nullptr,
                compute_shader.put()),
            S_OK);

  D3D11_BUFFER_DESC selection_desc = {};
  selection_desc.ByteWidth =
      static_cast<UINT>(selection.size() * sizeof(selection[0]));
  selection_desc.Usage = D3D11_USAGE_IMMUTABLE;
  selection_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA selection_data = {};
  selection_data.pSysMem = selection.data();
  ComPtr<ID3D11Buffer> selection_buffer;
  ASSERT_EQ(context_.device()->CreateBuffer(&selection_desc, &selection_data,
                                            selection_buffer.put()),
            S_OK);

  D3D11_SHADER_RESOURCE_VIEW_DESC selection_view_desc = {};
  selection_view_desc.Format = DXGI_FORMAT_R32_UINT;
  selection_view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  selection_view_desc.Buffer.NumElements = kIndirectCaseCount;
  ComPtr<ID3D11ShaderResourceView> selection_view;
  ASSERT_EQ(context_.device()->CreateShaderResourceView(selection_buffer.get(),
                                                        &selection_view_desc,
                                                        selection_view.put()),
            S_OK);

  D3D11_BUFFER_DESC output_desc = {};
  output_desc.ByteWidth =
      static_cast<UINT>(initial.size() * sizeof(initial[0]));
  output_desc.Usage = D3D11_USAGE_DEFAULT;
  output_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  D3D11_SUBRESOURCE_DATA output_data = {};
  output_data.pSysMem = initial.data();
  ComPtr<ID3D11Buffer> output;
  ASSERT_EQ(
      context_.device()->CreateBuffer(&output_desc, &output_data, output.put()),
      S_OK);

  D3D11_UNORDERED_ACCESS_VIEW_DESC output_view_desc = {};
  output_view_desc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
  output_view_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  output_view_desc.Buffer.NumElements = kIndirectCaseCount;
  ComPtr<ID3D11UnorderedAccessView> output_view;
  ASSERT_EQ(context_.device()->CreateUnorderedAccessView(
                output.get(), &output_view_desc, output_view.put()),
            S_OK);

  const std::array<std::uint32_t, 8> arguments = {
      0u, 0u, 0u, 0u, kIndirectAxisSize, kIndirectAxisSize, kIndirectAxisSize,
      0u};
  D3D11_BUFFER_DESC arguments_desc = {};
  arguments_desc.ByteWidth = sizeof(arguments);
  arguments_desc.Usage = D3D11_USAGE_IMMUTABLE;
  arguments_desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
  D3D11_SUBRESOURCE_DATA arguments_data = {};
  arguments_data.pSysMem = arguments.data();
  ComPtr<ID3D11Buffer> arguments_buffer;
  ASSERT_EQ(context_.device()->CreateBuffer(&arguments_desc, &arguments_data,
                                            arguments_buffer.put()),
            S_OK);

  ID3D11ShaderResourceView *srv = selection_view.get();
  ID3D11UnorderedAccessView *uav = output_view.get();
  context_.context()->CSSetShader(compute_shader.get(), nullptr, 0);
  context_.context()->CSSetShaderResources(0, 1, &srv);
  context_.context()->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
  context_.context()->DispatchIndirect(arguments_buffer.get(),
                                       kIndirectArgumentsOffset);

  ID3D11ShaderResourceView *null_srv = nullptr;
  ID3D11UnorderedAccessView *null_uav = nullptr;
  context_.context()->CSSetShaderResources(0, 1, &null_srv);
  context_.context()->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
  context_.context()->CSSetShader(nullptr, nullptr, 0);

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
                 kIndirectDispatchCases.family().case_id_prefix);
  bool found_mismatch = false;
  for (std::uint32_t logical = 0; logical < kIndirectCaseCount; ++logical) {
    const auto &desired =
        selected[logical] ? expected[logical] : initial[logical];
    for (std::uint32_t component = 0; component < kComponentsPerRecord;
         ++component) {
      const std::uint32_t actual_value =
          actual[logical * kComponentsPerRecord + component];
      if (actual_value == desired[component])
        continue;

      const std::uint32_t x = logical % kIndirectAxisSize;
      const std::uint32_t y = (logical / kIndirectAxisSize) % kIndirectAxisSize;
      const std::uint32_t z = logical / (kIndirectAxisSize * kIndirectAxisSize);
      const auto case_id =
          dxmt::test::LogicalCaseId(kIndirectDispatchCases.family(), logical);
      const auto replay_case_id =
          selected[logical]
              ? case_id
              : dxmt::test::LogicalCaseId(kIndirectDispatchCases.family(),
                                          selected_cases.front());
      ADD_FAILURE()
          << "LogicalCaseId: " << case_id << '\n'
          << "Class: "
          << dxmt::test::TestClassName(
                 kIndirectDispatchCases.family().traits.test_class)
          << '\n'
          << "Requirements: feature_level=11_0 shader_model=5_0 "
             "queue=Immediate capability=DispatchIndirect,"
             "DrawIndirectArgsBuffer,TypedUAV,StagingMap\n"
          << "ExecutionPath: "
          << dxmt::test::ExecutionPathName(
                 kIndirectDispatchCases.family().traits.execution_path)
          << '\n'
          << "Parameters: logical=" << logical << " group=(" << x << ',' << y
          << ',' << z << ") selected=" << (selected[logical] ? "true" : "false")
          << " arguments_byte_offset=" << kIndirectArgumentsOffset
          << " component=" << component << '\n'
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
