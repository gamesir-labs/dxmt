#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class DescriptorTableBindingSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.Num32BitValues = 1;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    root_signature_ = context_.CreateRootSignature(desc);
    pipeline_ = context_.CreateComputePipeline(
        root_signature_.get(), dxmt::test::ClearBufferComputeShader());
    ASSERT_TRUE(root_signature_);
    ASSERT_TRUE(pipeline_);

    for (UINT index = 0; index < outputs_.size(); ++index) {
      outputs_[index] = context_.CreateBuffer(
          kElementCount * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      heaps_[index] = context_.CreateDescriptorHeap(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
      cpu_heaps_[index] = context_.CreateDescriptorHeap(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
      ASSERT_TRUE(outputs_[index]);
      ASSERT_TRUE(heaps_[index]);
      ASSERT_TRUE(cpu_heaps_[index]);
      D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
      uav.Format = DXGI_FORMAT_R32_UINT;
      uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      uav.Buffer.NumElements = kElementCount;
      context_.device()->CreateUnorderedAccessView(
          outputs_[index].get(), nullptr, &uav,
          heaps_[index]->GetCPUDescriptorHandleForHeapStart());
      context_.device()->CreateUnorderedAccessView(
          outputs_[index].get(), nullptr, &uav,
          cpu_heaps_[index]->GetCPUDescriptorHandleForHeapStart());
      ID3D12DescriptorHeap *bound[] = {heaps_[index].get()};
      context_.list()->SetDescriptorHeaps(1, bound);
      const UINT clear[4] = {};
      context_.list()->ClearUnorderedAccessViewUint(
          heaps_[index]->GetGPUDescriptorHandleForHeapStart(),
          cpu_heaps_[index]->GetCPUDescriptorHandleForHeapStart(),
          outputs_[index].get(), clear, 0, nullptr);
    }
    ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
    ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  }

  void BindTable(UINT index) {
    ID3D12DescriptorHeap *bound[] = {heaps_[index].get()};
    context_.list()->SetDescriptorHeaps(1, bound);
    context_.list()->SetComputeRootDescriptorTable(
        1, heaps_[index]->GetGPUDescriptorHandleForHeapStart());
  }

  void BindPipelineAndArguments() {
    context_.list()->SetComputeRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline_.get());
    context_.list()->SetComputeRoot32BitConstant(0, kValue, 0);
  }

  void ExpectOutput(UINT index, UINT expected) {
    D3D12TestContext::Transition(context_.list(), outputs_[index].get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SUCCEEDED(context_.ReadbackBuffer(
        outputs_[index].get(), kElementCount * sizeof(UINT), &bytes)));
    ASSERT_EQ(bytes.size(), kElementCount * sizeof(UINT));
    for (std::size_t offset = 0; offset < bytes.size();
         offset += sizeof(UINT)) {
      UINT value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(value));
      EXPECT_EQ(value, expected) << "element " << offset / sizeof(UINT);
    }
  }

  static constexpr UINT kElementCount = 64;
  static constexpr UINT kValue = 0x12345678;
  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  std::array<ComPtr<ID3D12Resource>, 2> outputs_;
  std::array<ComPtr<ID3D12DescriptorHeap>, 2> heaps_;
  std::array<ComPtr<ID3D12DescriptorHeap>, 2> cpu_heaps_;
};


TEST_F(DescriptorTableBindingSpec, RebindingAfterHeapSwitchRestoresAccess) {
  BindPipelineAndArguments();
  BindTable(0);
  BindTable(1);
  context_.list()->Dispatch(1, 1, 1);

  ExpectOutput(1, kValue);
}

TEST_F(DescriptorTableBindingSpec, SettingSameHeapAgainPreservesTables) {
  BindPipelineAndArguments();
  BindTable(0);
  ID3D12DescriptorHeap *same[] = {heaps_[0].get()};
  context_.list()->SetDescriptorHeaps(1, same);
  context_.list()->Dispatch(1, 1, 1);

  ExpectOutput(0, kValue);
}



TEST_F(DescriptorTableBindingSpec,
       SwitchingOnlySamplerHeapDoesNotCorruptCbvSrvUavHeap) {
  auto sampler_a = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  auto sampler_b = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  ASSERT_TRUE(sampler_a);
  ASSERT_TRUE(sampler_b);

  BindPipelineAndArguments();
  ID3D12DescriptorHeap *first[] = {heaps_[0].get(), sampler_a.get()};
  context_.list()->SetDescriptorHeaps(2, first);
  context_.list()->SetComputeRootDescriptorTable(
      1, heaps_[0]->GetGPUDescriptorHandleForHeapStart());

  // Replace only the sampler heap while keeping the same CBV/SRV/UAV heap.
  // After rebinding the resource table, the UAV path must still execute.
  ID3D12DescriptorHeap *second[] = {heaps_[0].get(), sampler_b.get()};
  context_.list()->SetDescriptorHeaps(2, second);
  context_.list()->SetComputeRootDescriptorTable(
      1, heaps_[0]->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);

  ExpectOutput(0, kValue);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(DescriptorTableBindingSpec,
       SwitchingOnlyCbvSrvUavHeapDoesNotCorruptSamplerHeap) {
  auto sampler = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  ASSERT_TRUE(sampler);

  BindPipelineAndArguments();
  ID3D12DescriptorHeap *first[] = {heaps_[0].get(), sampler.get()};
  context_.list()->SetDescriptorHeaps(2, first);
  context_.list()->SetComputeRootDescriptorTable(
      1, heaps_[0]->GetGPUDescriptorHandleForHeapStart());

  // Replace only the resource heap. After rebinding onto heap 1 the sampler
  // heap remains usable for subsequent sampler-table rebinds.
  ID3D12DescriptorHeap *second[] = {heaps_[1].get(), sampler.get()};
  context_.list()->SetDescriptorHeaps(2, second);
  context_.list()->SetComputeRootDescriptorTable(
      1, heaps_[1]->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);

  ExpectOutput(1, kValue);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
