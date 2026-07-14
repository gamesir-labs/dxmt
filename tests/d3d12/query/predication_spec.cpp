#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ClearBufferComputeShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class PredicationSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  void ExpectPredicatedDispatch(UINT64 predicate_value,
                                D3D12_PREDICATION_OP operation,
                                bool executes) {
    constexpr std::uint32_t sentinel = 0xdeadbeef;
    constexpr std::uint32_t dispatch_value = 0x13579bdf;
    std::array<std::uint32_t, 64> initial;
    initial.fill(sentinel);
    auto initial_upload = context_.CreateUploadBuffer(
        sizeof(initial), initial.data(), sizeof(initial));
    auto output = context_.CreateBuffer(
        sizeof(initial), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto predicate_upload = context_.CreateUploadBuffer(
        sizeof(predicate_value), &predicate_value, sizeof(predicate_value));
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    ASSERT_TRUE(initial_upload);
    ASSERT_TRUE(output);
    ASSERT_TRUE(predicate_upload);
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
    auto pipeline = context_.CreateComputePipeline(
        root_signature.get(), ClearBufferComputeShader());
    ASSERT_TRUE(root_signature);
    ASSERT_TRUE(pipeline);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = initial.size();
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        heap->GetCPUDescriptorHandleForHeapStart());
    context_.list()->CopyBufferRegion(output.get(), 0, initial_upload.get(), 0,
                                      sizeof(initial));
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootSignature(root_signature.get());
    context_.list()->SetComputeRoot32BitConstant(0, dispatch_value, 0);
    context_.list()->SetComputeRootDescriptorTable(
        1, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetPredication(predicate_upload.get(), 0, operation);
    context_.list()->Dispatch(1, 1, 1);
    context_.list()->SetPredication(nullptr, 0,
                                    D3D12_PREDICATION_OP_EQUAL_ZERO);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SUCCEEDED(
        context_.ReadbackBuffer(output.get(), sizeof(initial), &bytes)));
    ASSERT_EQ(bytes.size(), sizeof(initial));
    const std::uint32_t expected = executes ? dispatch_value : sentinel;
    for (std::size_t index = 0; index < initial.size(); ++index) {
      std::uint32_t actual = 0;
      std::memcpy(&actual, bytes.data() + index * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected) << "element " << index;
    }
  }

  D3D12TestContext context_;
};

TEST_F(PredicationSpec, EqualZeroExecutes) {
  ExpectPredicatedDispatch(0, D3D12_PREDICATION_OP_EQUAL_ZERO, true);
}

TEST_F(PredicationSpec, EqualZeroSkips) {
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_EQUAL_ZERO, false);
}

TEST_F(PredicationSpec, NotEqualZeroExecutes) {
  ExpectPredicatedDispatch(1, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, true);
}

TEST_F(PredicationSpec, NotEqualZeroSkips) {
  ExpectPredicatedDispatch(0, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO, false);
}

} // namespace
