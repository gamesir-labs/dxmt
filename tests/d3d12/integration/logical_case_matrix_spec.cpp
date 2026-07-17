#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;

constexpr std::uint32_t kArithmeticCaseCount = 16384;

const dxmt::test::LogicalCaseFamilyRegistration kArithmeticCases(
    "D3D12LogicalCaseMatrixSpec."
    "Executes16384UintArithmeticCasesInOneDispatch",
    "D3D12.Shader.Arithmetic.Uint32.", kArithmeticCaseCount, 5,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Direct", "ComputeShader,RootSRV,RootUAV"},
     dxmt::test::kMultiSubmissionTestCost,
     "selected logical IDs in an upload buffer and a poison-initialized UAV",
     "dispatch one invocation per selected ID, apply the indexed uint32 "
     "operation, issue a UAV barrier, and read back",
     "every selected output equals the bit-exact CPU uint32 evaluator and "
     "every unselected output remains poison",
     "logical ID, selection state, operation selector, shift, "
     "expected/actual, and exact replay argument"});

const dxmt::test::TestCostRegistration kArithmeticCost(
    "D3D12LogicalCaseMatrixSpec."
    "Executes16384UintArithmeticCasesInOneDispatch",
    dxmt::test::kMultiSubmissionTestCost);

std::uint32_t RotateLeft(std::uint32_t value, std::uint32_t shift) {
  if (shift == 0)
    return value;
  return (value << shift) | (value >> (32u - shift));
}

std::uint32_t EvaluateArithmeticCase(std::uint32_t case_index) {
  const std::uint32_t shift = case_index & 31u;
  const std::uint32_t selector = (case_index >> 5u) & 7u;
  const std::uint32_t x =
      case_index * 0x9e3779b9u + static_cast<std::uint32_t>(0x243f6a88u);
  const std::uint32_t y =
      (case_index ^ 0xa5a5a5a5u) * 0x85ebca6bu + 0xc2b2ae35u;
  const std::uint32_t rotated = RotateLeft(x, shift);

  switch (selector) {
  case 0:
    return x + y;
  case 1:
    return x - y;
  case 2:
    return x * (y | 1u);
  case 3:
    return x ^ rotated;
  case 4:
    return (x & y) | (~x & rotated);
  case 5:
    return (x < y ? x : y) + (x > rotated ? x : rotated);
  case 6:
    return (x >> shift) ^ (y << shift);
  default:
    return (x % ((case_index & 255u) + 1u)) ^ rotated;
  }
}

class D3D12LogicalCaseMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12LogicalCaseMatrixSpec,
       Executes16384UintArithmeticCasesInOneDispatch) {
  std::vector<std::uint32_t> expected(kArithmeticCaseCount);
  std::vector<std::uint32_t> poison(kArithmeticCaseCount);
  std::vector<bool> selected(kArithmeticCaseCount, false);
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kArithmeticCaseCount);
  for (std::uint32_t index = 0; index < kArithmeticCaseCount; ++index) {
    expected[index] = EvaluateArithmeticCase(index);
    poison[index] = ~expected[index];
    if (dxmt::test::LogicalCaseSelected(kArithmeticCases.family(), index)) {
      selected[index] = true;
      selected_cases.push_back(index);
    }
  }
  ASSERT_FALSE(selected_cases.empty());

  const auto shader = CompileShader(R"(
    ByteAddressBuffer case_indices : register(t0);
    RWByteAddressBuffer output : register(u0);

    cbuffer Parameters : register(b0) {
      uint selected_count;
    };

    uint rotate_left(uint value, uint shift) {
      return shift == 0 ? value : (value << shift) | (value >> (32 - shift));
    }

    uint evaluate(uint case_index) {
      uint shift = case_index & 31u;
      uint selector = (case_index >> 5u) & 7u;
      uint x = case_index * 0x9e3779b9u + 0x243f6a88u;
      uint y = (case_index ^ 0xa5a5a5a5u) * 0x85ebca6bu + 0xc2b2ae35u;
      uint rotated = rotate_left(x, shift);

      switch (selector) {
      case 0: return x + y;
      case 1: return x - y;
      case 2: return x * (y | 1u);
      case 3: return x ^ rotated;
      case 4: return (x & y) | (~x & rotated);
      case 5: return min(x, y) + max(x, rotated);
      case 6: return (x >> shift) ^ (y << shift);
      default: return (x % ((case_index & 255u) + 1u)) ^ rotated;
      }
    }

    [numthreads(64, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      if (id.x >= selected_count)
        return;
      uint case_index = case_indices.Load(id.x * 4);
      output.Store(case_index * 4, evaluate(case_index));
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_ROOT_PARAMETER parameters[3] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].Descriptor.ShaderRegister = 0;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[2].Constants.ShaderRegister = 0;
  parameters[2].Constants.Num32BitValues = 1;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 3;
  root_desc.pParameters = parameters;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  const UINT64 output_size = expected.size() * sizeof(expected[0]);
  auto selected_upload = context_.CreateUploadBuffer(
      selected_cases.size() * sizeof(selected_cases[0]), selected_cases.data(),
      selected_cases.size() * sizeof(selected_cases[0]));
  auto poison_upload = context_.CreateUploadBuffer(
      output_size, poison.data(), output_size);
  auto output = context_.CreateBuffer(
      output_size, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(selected_upload);
  ASSERT_TRUE(poison_upload);
  ASSERT_TRUE(output);

  // The setup submission gives every unselected slot a known value that can
  // never equal its expected result. This makes exact single-CaseId replay a
  // deterministic dropped-dispatch oracle.
  context_.list()->CopyBufferRegion(output.get(), 0, poison_upload.get(), 0,
                                    output_size);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> setup_bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), output_size, &setup_bytes),
            S_OK);
  ASSERT_EQ(setup_bytes.size(), output_size);
  ASSERT_EQ(std::memcmp(setup_bytes.data(), poison.data(), output_size), 0);

  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootShaderResourceView(
      0, selected_upload->GetGPUVirtualAddress());
  context_.list()->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress());
  const UINT selected_count = static_cast<UINT>(selected_cases.size());
  context_.list()->SetComputeRoot32BitConstant(2, selected_count, 0);
  context_.list()->Dispatch((selected_count + 63u) / 64u, 1, 1);
  D3D12TestContext::UavBarrier(context_.list(), output.get());
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), output_size, &bytes), S_OK);
  ASSERT_EQ(bytes.size(), output_size);
  std::vector<std::uint32_t> actual(kArithmeticCaseCount);
  std::memcpy(actual.data(), bytes.data(), bytes.size());

  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kArithmeticCases.family().case_id_prefix);
  for (std::uint32_t logical = 0; logical < kArithmeticCaseCount; ++logical) {
    const std::uint32_t desired =
        selected[logical] ? expected[logical] : poison[logical];
    if (actual[logical] == desired)
      continue;

    const dxmt::test::GpuCaseResult result = {
        selected[logical] ? 1u : 2u, logical, desired, actual[logical]};
    const auto case_id =
        dxmt::test::LogicalCaseId(kArithmeticCases.family(), logical);
    const auto replay_case_id = dxmt::test::LogicalCaseId(
        kArithmeticCases.family(), selected_cases.front());
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: Conformance\n"
                  << "Requirements: feature_level=11_0 shader_model=5_0 "
                     "queue=Direct capability=ComputeShader,RootSRV,RootUAV\n"
                  << "ExecutionPath: Auto\n"
                  << "Parameters: logical=" << logical
                  << " selected=" << (selected[logical] ? "true" : "false")
                  << " selector=" << ((logical >> 5u) & 7u)
                  << " shift=" << (logical & 31u) << '\n'
                  << "GpuCaseResult: status=" << result.status
                  << " first_mismatch_index="
                  << result.first_mismatch_index << " expected=0x" << std::hex
                  << result.expected << " actual=0x" << result.actual
                  << std::dec << '\n'
                  << "Replay: --dxmt-case-id=" << replay_case_id;
    break;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
