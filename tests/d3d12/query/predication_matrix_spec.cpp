#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan query/predication: SetPredication value × op matrix with dispatch oracle.
// Public D3D12 API only. Matches existing PredicationSpec resource setup.

namespace {

using dxmt::test::ClearBufferComputeShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct PredicationCase {
  UINT64 predicate_value;
  D3D12_PREDICATION_OP op;
  bool expect_dispatch;
  UINT64 predicate_offset; // 0 or 8
};

std::vector<PredicationCase> BuildPredicationCases() {
  std::vector<PredicationCase> cases;
  // Values known to work with the public predication path (0 / non-zero).
  const UINT64 values[] = {0ull, 1ull, 2ull, 0xffffffffull, 42ull, 100ull,
                           0x80000000ull};
  for (const UINT64 value : values) {
    cases.push_back({value, D3D12_PREDICATION_OP_EQUAL_ZERO, value == 0, 0});
    cases.push_back(
        {value, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, value != 0, 0});
    // Offset into a two-slot predicate buffer.
    cases.push_back(
        {value, D3D12_PREDICATION_OP_EQUAL_ZERO, value == 0, sizeof(UINT64)});
    cases.push_back({value, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, value != 0,
                     sizeof(UINT64)});
  }
  return cases;
}

class PredicationMatrixSpec
    : public ::testing::TestWithParam<PredicationCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(PredicationMatrixSpec, DispatchRespectsPredicate) {
  const auto &test = GetParam();
  constexpr std::uint32_t sentinel = 0xdeadbeefu;
  constexpr std::uint32_t dispatch_value = 0x13579bdfu;
  std::array<std::uint32_t, 64> initial;
  initial.fill(sentinel);
  auto initial_upload = context_.CreateUploadBuffer(
      sizeof(initial), initial.data(), sizeof(initial));
  auto output = context_.CreateBuffer(
      sizeof(initial), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  std::array<UINT64, 2> predicate_values = {0xfedcba9876543210ull,
                                            0xfedcba9876543210ull};
  predicate_values[test.predicate_offset / sizeof(UINT64)] =
      test.predicate_value;
  auto predicate = context_.CreateUploadBuffer(
      sizeof(predicate_values), predicate_values.data(),
      sizeof(predicate_values));
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(initial_upload);
  ASSERT_TRUE(output);
  ASSERT_TRUE(predicate);
  ASSERT_TRUE(heap);

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.Num32BitValues = 1;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  auto root_signature = context_.CreateRootSignature(root_desc);
  auto pipeline = context_.CreateComputePipeline(root_signature.get(),
                                                 ClearBufferComputeShader());
  ASSERT_TRUE(root_signature);
  ASSERT_TRUE(pipeline);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = static_cast<UINT>(initial.size());
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());

  context_.list()->CopyBufferRegion(output.get(), 0, initial_upload.get(), 0,
                                    sizeof(initial));
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetComputeRoot32BitConstant(0, dispatch_value, 0);
  context_.list()->SetComputeRootDescriptorTable(
      1, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetPredication(predicate.get(), test.predicate_offset,
                                  test.op);
  context_.list()->Dispatch(1, 1, 1);
  context_.list()->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(initial), &bytes),
            S_OK);
  const std::uint32_t expected =
      test.expect_dispatch ? dispatch_value : sentinel;
  for (std::size_t index = 0; index < initial.size(); ++index) {
    std::uint32_t actual = 0;
    std::memcpy(&actual, bytes.data() + index * sizeof(actual), sizeof(actual));
    EXPECT_EQ(actual, expected)
        << "element " << index << " value=" << test.predicate_value
        << " op=" << static_cast<UINT>(test.op)
        << " off=" << test.predicate_offset;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string PredName(const ::testing::TestParamInfo<PredicationCase> &info) {
  return "V" + std::to_string(info.param.predicate_value) + "Op" +
         std::to_string(static_cast<UINT>(info.param.op)) + "Off" +
         std::to_string(info.param.predicate_offset) + "E" +
         (info.param.expect_dispatch ? "1" : "0") + "I" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(OpMatrix, PredicationMatrixSpec,
                         ::testing::ValuesIn(BuildPredicationCases()),
                         PredName);

} // namespace
