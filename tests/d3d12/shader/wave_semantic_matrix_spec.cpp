#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/wave_semantics.inc"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::D3D12TestContext;

enum class WaveSemantic {
  ActiveSum,
  ActiveMin,
  ActiveMax,
  PrefixSum,
  ReadLaneAt,
  AllTrue,
  AnyTrue,
  BallotEvenCount,
  IsFirstLane,
  FirstLaneActiveSum,
  ActiveProduct,
  PrefixProduct,
};

struct WaveSemanticCase {
  const char *name;
  WaveSemantic semantic;
  UINT word;
};

class ShaderWaveCapabilitySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(ShaderWaveCapabilitySpec,
       KeepsWaveMatchFailClosedBelowShaderModel65) {
  D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {
      D3D_SHADER_MODEL_6_5,
  };
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_SHADER_MODEL, &shader_model,
                sizeof(shader_model)),
            S_OK);
  EXPECT_LT(shader_model.HighestShaderModel, D3D_SHADER_MODEL_6_5);
}

class ShaderWaveSemanticMatrixSpec
    : public ::testing::TestWithParam<WaveSemanticCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_D3D12_OPTIONS1, &options_, sizeof(options_)),
              S_OK);
  }

  UINT Expected(WaveSemantic semantic, UINT thread, UINT lane,
                UINT active_count, UINT first_thread) const {
    const UINT last_thread = first_thread + active_count - 1;
    switch (semantic) {
    case WaveSemantic::ActiveSum:
      return active_count * (first_thread + 1 + last_thread + 1) / 2;
    case WaveSemantic::ActiveMin:
      return first_thread + 7;
    case WaveSemantic::ActiveMax:
      return last_thread + 7;
    case WaveSemantic::PrefixSum:
      return lane;
    case WaveSemantic::ReadLaneAt:
      return last_thread;
    case WaveSemantic::AllTrue:
    case WaveSemantic::AnyTrue:
      return 1;
    case WaveSemantic::BallotEvenCount: {
      UINT count = 0;
      for (UINT value = first_thread; value <= last_thread; ++value)
        count += (value & 1u) == 0u;
      return count;
    }
    case WaveSemantic::IsFirstLane:
      return lane == 0 ? 1 : 0;
    case WaveSemantic::FirstLaneActiveSum:
      return 1;
    case WaveSemantic::ActiveProduct:
    case WaveSemantic::PrefixProduct:
      return 1;
    }
    return 0;
  }

  D3D12TestContext context_;
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options_ = {};
};

TEST_P(ShaderWaveSemanticMatrixSpec, ExecutesAdvertisedWaveIntrinsic) {
  if (!options_.WaveOps)
    GTEST_SKIP() << "WaveOps is not advertised";

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {kWaveSemanticComputeShader,
                                          kWaveSemanticComputeShader_len};
  auto pipeline =
      context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  constexpr UINT kThreadCount = 32;
  constexpr UINT kWordsPerThread = 16;
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
  const auto &test = GetParam();
  for (UINT thread = 0; thread < kThreadCount; ++thread) {
    UINT values[kWordsPerThread] = {};
    std::memcpy(values, bytes.data() + thread * sizeof(values), sizeof(values));
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
    ASSERT_LT(test.word, kWordsPerThread);
    EXPECT_EQ(values[test.word], Expected(test.semantic, thread, lane,
                                          active_count, first_thread));
  }
}

std::string
WaveSemanticCaseName(const ::testing::TestParamInfo<WaveSemanticCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    StableIntrinsics, ShaderWaveSemanticMatrixSpec,
    ::testing::Values(
        WaveSemanticCase{"ActiveSum", WaveSemantic::ActiveSum, 4},
        WaveSemanticCase{"ActiveMin", WaveSemantic::ActiveMin, 5},
        WaveSemanticCase{"ActiveMax", WaveSemantic::ActiveMax, 6},
        WaveSemanticCase{"PrefixSum", WaveSemantic::PrefixSum, 7},
        WaveSemanticCase{"ReadLaneAt", WaveSemantic::ReadLaneAt, 8},
        WaveSemanticCase{"AllTrue", WaveSemantic::AllTrue, 9},
        WaveSemanticCase{"AnyTrue", WaveSemantic::AnyTrue, 10},
        WaveSemanticCase{"BallotEvenCount", WaveSemantic::BallotEvenCount, 11},
        WaveSemanticCase{"IsFirstLane", WaveSemantic::IsFirstLane, 12},
        WaveSemanticCase{"FirstLaneActiveSum", WaveSemantic::FirstLaneActiveSum,
                         13},
        WaveSemanticCase{"ActiveProduct", WaveSemantic::ActiveProduct, 14},
        WaveSemanticCase{"PrefixProduct", WaveSemantic::PrefixProduct, 15}),
    WaveSemanticCaseName);

} // namespace
