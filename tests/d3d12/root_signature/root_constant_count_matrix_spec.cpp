#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Public D3D12 root-signature + compute path: 1..64 root 32-bit constants.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class RootConstantCountMatrixSpec
    : public ::testing::TestWithParam<UINT> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(RootConstantCountMatrixSpec, CreatesRootSignatureWithExactConstantCount) {
  const UINT count = GetParam();
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.Num32BitValues = count;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = 1;
  desc.pParameters = &parameter;
  auto root = context_.CreateRootSignature(desc);
  ASSERT_TRUE(root) << "count=" << count;
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(RootConstantCountMatrixSpec, DispatchesWithRootConstantsVisibleToShader) {
  const UINT count = GetParam();
  if (count == 64) {
    GTEST_SKIP() << "the observable compute path needs one additional DWORD "
                    "for its UAV descriptor table";
  }
  // Keep GPU path cheap: only exercise counts that fit a single constant buffer
  // style root parameter and a tiny clear shader using the first constant.
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.Num32BitValues = count;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &range;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = 2;
  desc.pParameters = parameters;
  auto root = context_.CreateRootSignature(desc);
  ASSERT_TRUE(root);

  const auto shader = CompileShader(R"(
    cbuffer Constants : register(b0) { uint value; };
    RWBuffer<uint> output : register(u0);
    [numthreads(1,1,1)] void main() { output[0] = value; }
  )", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto pipeline = context_.CreateComputePipeline(
      root.get(), {shader.bytecode->GetBufferPointer(),
                   shader.bytecode->GetBufferSize()});
  auto output = context_.CreateBuffer(
      sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      heap->GetCPUDescriptorHandleForHeapStart());

  std::vector<UINT> constants(count, 0);
  constants[0] = 0xA5A50000u + count;
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRoot32BitConstants(0, count, constants.data(), 0);
  context_.list()->SetComputeRootDescriptorTable(
      1, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(UINT), &bytes), S_OK);
  UINT actual = 0;
  std::memcpy(&actual, bytes.data(), sizeof(actual));
  EXPECT_EQ(actual, constants[0]);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

INSTANTIATE_TEST_SUITE_P(ConstantCounts, RootConstantCountMatrixSpec,
                         ::testing::Values(1u, 2u, 3u, 4u, 15u, 16u, 31u,
                                           32u, 33u, 63u, 64u),
                         [](const ::testing::TestParamInfo<UINT> &info) {
                           return "Count" + std::to_string(info.param);
                         });

} // namespace
