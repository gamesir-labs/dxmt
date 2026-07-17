#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

constexpr std::uint32_t kCopyCaseCount = 4096;
const dxmt::test::LogicalCaseFamilyRegistration kShuffledCopyCases(
    "D3D12BatchedMatrixSpec.Copies4096ShuffledRegionsInOneSubmission",
    "D3D12.Copy.Buffer.ShuffledRegion.", kCopyCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Direct", "CopyBufferRegion"},
     dxmt::test::kMultiSubmissionTestCost,
     "4096-word upload source and poison-initialized default output",
     "copy one or more source dwords to permuted destinations, transition, "
     "and read back",
     "every selected destination equals its deterministic source dword",
     "logical/source/destination offsets, first mismatch, expected/actual, "
     "and exact replay argument"});
const dxmt::test::TestCostRegistration kShuffledCopyCost(
    "D3D12BatchedMatrixSpec.Copies4096ShuffledRegionsInOneSubmission",
    dxmt::test::kMultiSubmissionTestCost);

class D3D12BatchedMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12BatchedMatrixSpec, Copies4096ShuffledRegionsInOneSubmission) {
  std::vector<std::uint32_t> source_values(kCopyCaseCount);
  for (std::uint32_t index = 0; index < kCopyCaseCount; ++index)
    source_values[index] = 0x80000000u ^ (index * 0x9e3779b9u);
  std::vector<std::uint32_t> poison(kCopyCaseCount);
  for (std::uint32_t logical = 0; logical < kCopyCaseCount; ++logical) {
    const std::uint32_t destination =
        (logical * 4051u) & (kCopyCaseCount - 1);
    poison[destination] = ~source_values[logical];
  }

  auto source = context_.CreateUploadBuffer(
      source_values.size() * sizeof(source_values[0]), source_values.data(),
      source_values.size() * sizeof(source_values[0]));
  auto poison_upload = context_.CreateUploadBuffer(
      poison.size() * sizeof(poison[0]), poison.data(),
      poison.size() * sizeof(poison[0]));
  auto output = context_.CreateBuffer(
      source_values.size() * sizeof(source_values[0]), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(poison_upload);
  ASSERT_TRUE(output);

  // Single-case replay writes only one destination, so establish and verify a
  // deterministic non-matching value for every slot in an independent setup
  // submission. A dropped tested copy can never pass because of uninitialized
  // default-heap contents.
  const UINT64 output_size = poison.size() * sizeof(poison[0]);
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
      D3D12_RESOURCE_STATE_COPY_DEST);

  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kCopyCaseCount);
  for (std::uint32_t logical = 0; logical < kCopyCaseCount; ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kShuffledCopyCases.family(), logical))
      continue;
    selected_cases.push_back(logical);
    // An odd multiplier is a permutation modulo this power-of-two case count.
    const std::uint32_t destination =
        (logical * 4051u) & (kCopyCaseCount - 1);
    context_.list()->CopyBufferRegion(
        output.get(), std::uint64_t(destination) * sizeof(std::uint32_t),
        source.get(), std::uint64_t(logical) * sizeof(std::uint32_t),
        sizeof(std::uint32_t));
  }
  ASSERT_FALSE(selected_cases.empty());
  RecordProperty("logical_cases_executed",
                 static_cast<int>(selected_cases.size()));
  RecordProperty("logical_case_prefix",
                 kShuffledCopyCases.family().case_id_prefix);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(
                output.get(),
                source_values.size() * sizeof(source_values[0]), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), source_values.size() * sizeof(source_values[0]));
  std::vector<std::uint32_t> actual(kCopyCaseCount);
  std::memcpy(actual.data(), bytes.data(), bytes.size());

  for (const std::uint32_t logical : selected_cases) {
    const std::uint32_t destination =
        (logical * 4051u) & (kCopyCaseCount - 1);
    const std::uint32_t expected = source_values[logical];
    if (actual[destination] == expected)
      continue;

    const dxmt::test::GpuCaseResult result = {
        1, destination, expected, actual[destination]};
    const auto case_id =
        dxmt::test::LogicalCaseId(kShuffledCopyCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kShuffledCopyCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 queue=Direct "
                     "capability=CopyBufferRegion\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kShuffledCopyCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " source_offset=" << logical * sizeof(std::uint32_t)
                  << " destination=" << destination
                  << " destination_offset="
                  << destination * sizeof(std::uint32_t) << '\n'
                  << "GpuCaseResult: status=" << result.status
                  << " first_mismatch_index="
                  << result.first_mismatch_index << " expected=0x" << std::hex
                  << result.expected << " actual=0x" << result.actual
                  << std::dec << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }
}

TEST_F(D3D12BatchedMatrixSpec, Dispatches256ParameterCasesInOneSubmission) {
  constexpr std::uint32_t kDispatchCount = 256;
  constexpr std::uint32_t kMaximumWords = kDispatchCount * 8;
  const auto shader = CompileShader(R"(
    RWByteAddressBuffer output : register(u0);
    cbuffer Parameters : register(b0) {
      uint base;
      uint count;
      uint salt;
    };
    [numthreads(8, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      if (id.x < count) {
        const uint index = base + id.x;
        output.Store(index * 4, (index * 0x45d9f3bu) ^ salt ^ count);
      }
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 3;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  auto output = context_.CreateBuffer(
      kMaximumWords * sizeof(std::uint32_t), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kMaximumWords;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());

  std::vector<std::uint32_t> expected;
  expected.reserve(kMaximumWords);
  for (std::uint32_t dispatch = 0; dispatch < kDispatchCount; ++dispatch) {
    const std::uint32_t count = 1 + ((dispatch * 5u) & 7u);
    const std::uint32_t base = static_cast<std::uint32_t>(expected.size());
    const std::uint32_t salt = 0xa5a50000u ^ (dispatch * 0x1021u);
    const std::uint32_t constants[] = {base, count, salt};
    context_.list()->SetComputeRoot32BitConstants(1, 3, constants, 0);
    context_.list()->Dispatch(1, 1, 1);
    for (std::uint32_t lane = 0; lane < count; ++lane) {
      const std::uint32_t index = base + lane;
      expected.push_back((index * 0x45d9f3bu) ^ salt ^ count);
    }
  }
  ASSERT_LE(expected.size(), static_cast<std::size_t>(kMaximumWords));
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(
                output.get(), expected.size() * sizeof(expected[0]), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), expected.size() * sizeof(expected[0]));
  std::vector<std::uint32_t> actual(expected.size());
  std::memcpy(actual.data(), bytes.data(), bytes.size());
  EXPECT_EQ(actual, expected);
}

} // namespace
