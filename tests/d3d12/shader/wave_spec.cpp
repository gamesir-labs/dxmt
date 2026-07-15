#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/wave_operations.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::WaveOperationsComputeShader;

class ShaderWaveSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_D3D12_OPTIONS1, &options_, sizeof(options_)),
              S_OK);
  }

  ComPtr<ID3D12RootSignature> CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    return context_.CreateRootSignature(desc);
  }

  D3D12TestContext context_;
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options_ = {};
};

TEST_F(ShaderWaveSpec, AdvertisedWaveOperationsExecuteCorrectly) {
  if (!options_.WaveOps)
    GTEST_SKIP() << "WaveOps is not advertised";
  auto root_signature = CreateRootSignature();
  ASSERT_TRUE(root_signature);
  auto pipeline = context_.CreateComputePipeline(root_signature.get(),
                                                 WaveOperationsComputeShader());
  ASSERT_TRUE(pipeline);

  constexpr UINT kThreadCount = 32;
  constexpr UINT kWordsPerThread = 4;
  constexpr UINT kBufferSize = kThreadCount * kWordsPerThread * sizeof(UINT);
  auto output =
      context_.CreateBuffer(kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kBufferSize / sizeof(UINT);
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), kBufferSize, &bytes), S_OK);
  ASSERT_EQ(bytes.size(), kBufferSize);
  for (UINT thread = 0; thread < kThreadCount; ++thread) {
    UINT values[kWordsPerThread] = {};
    std::memcpy(values, bytes.data() + thread * kWordsPerThread * sizeof(UINT),
                sizeof(values));
    const UINT lane = values[0];
    const UINT lane_count = values[1];
    const UINT active_count = values[2];
    const UINT first_thread = values[3];
    SCOPED_TRACE(::testing::Message() << "thread " << thread);
    EXPECT_GE(lane_count, options_.WaveLaneCountMin);
    EXPECT_LE(lane_count, options_.WaveLaneCountMax);
    ASSERT_LT(lane, lane_count);
    ASSERT_LE(first_thread, thread);
    EXPECT_EQ(lane, thread - first_thread);
    EXPECT_EQ(active_count, std::min(lane_count, kThreadCount - first_thread));
  }
}

} // namespace
