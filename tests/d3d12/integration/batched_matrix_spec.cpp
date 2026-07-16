#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class D3D12BatchedMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12BatchedMatrixSpec, Copies4096ShuffledRegionsInOneSubmission) {
  constexpr std::uint32_t kCaseCount = 4096;
  std::vector<std::uint32_t> source_values(kCaseCount);
  std::vector<std::uint32_t> expected(kCaseCount);
  for (std::uint32_t index = 0; index < kCaseCount; ++index)
    source_values[index] = 0x80000000u ^ (index * 0x9e3779b9u);

  auto source = context_.CreateUploadBuffer(
      source_values.size() * sizeof(source_values[0]), source_values.data(),
      source_values.size() * sizeof(source_values[0]));
  auto output = context_.CreateBuffer(
      expected.size() * sizeof(expected[0]), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(output);

  for (std::uint32_t logical = 0; logical < kCaseCount; ++logical) {
    // An odd multiplier is a permutation modulo this power-of-two case count.
    const std::uint32_t destination = (logical * 4051u) & (kCaseCount - 1);
    expected[destination] = source_values[logical];
    context_.list()->CopyBufferRegion(
        output.get(), std::uint64_t(destination) * sizeof(std::uint32_t),
        source.get(), std::uint64_t(logical) * sizeof(std::uint32_t),
        sizeof(std::uint32_t));
  }
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(
                output.get(), expected.size() * sizeof(expected[0]), &bytes),
            S_OK);
  ASSERT_EQ(bytes.size(), expected.size() * sizeof(expected[0]));
  std::vector<std::uint32_t> actual(kCaseCount);
  std::memcpy(actual.data(), bytes.data(), bytes.size());
  EXPECT_EQ(actual, expected);
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
