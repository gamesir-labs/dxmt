#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>
#include <dxmt_d3d12_test_path.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

// The first two tests exercise public zero-work commands. The test-only path
// controls below additionally inject literal record_count == 0 compiled and
// fallback segments so the replay boundary itself is covered.
class PathEmptySegmentSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    const auto shader = CompileShader(R"(
      RWBuffer<uint> output : register(u0);

      [numthreads(1, 1, 1)]
      void main() {
        uint original;
        InterlockedAdd(output[0], 1u, original);
      }
    )",
                                      "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

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
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    pipeline_ =
        context_.CreateComputePipeline(root_signature_.get(), bytecode);
    ASSERT_TRUE(pipeline_);

    constexpr std::uint32_t kInitialValue = 0;
    auto upload = context_.CreateUploadBuffer(
        sizeof(kInitialValue), &kInitialValue, sizeof(kInitialValue));
    output_ = context_.CreateBuffer(
        sizeof(kInitialValue), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    readback_ = context_.CreateBuffer(
        sizeof(kInitialValue), D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    descriptor_heap_ = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(output_);
    ASSERT_TRUE(readback_);
    ASSERT_TRUE(descriptor_heap_);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 1;
    context_.device()->CreateUnorderedAccessView(
        output_.get(), nullptr, &uav,
        descriptor_heap_->GetCPUDescriptorHandleForHeapStart());

    context_.list()->CopyBufferRegion(output_.get(), 0, upload.get(), 0,
                                      sizeof(kInitialValue));
    D3D12TestContext::Transition(
        context_.list(), output_.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {descriptor_heap_.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline_.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, descriptor_heap_->GetGPUDescriptorHandleForHeapStart());
  }

  void DispatchRealWork() { context_.list()->Dispatch(1, 1, 1); }

  void ExpectCounterAfterFence(std::uint32_t expected) {
    D3D12TestContext::Transition(
        context_.list(), output_.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    context_.list()->CopyBufferRegion(readback_.get(), 0, output_.get(), 0,
                                      sizeof(expected));

    ComPtr<ID3D12Fence> fence;
    ASSERT_EQ(context_.device()->CreateFence(
                  0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                  reinterpret_cast<void **>(fence.put())),
              S_OK);
    ASSERT_EQ(context_.list()->Close(), S_OK);
    ID3D12CommandList *lists[] = {context_.list()};
    context_.queue()->ExecuteCommandLists(1, lists);

    constexpr UINT64 kCompletionValue = 0x51;
    ASSERT_EQ(context_.queue()->Signal(fence.get(), kCompletionValue), S_OK);
    ASSERT_EQ(context_.WaitForFence(fence.get(), kCompletionValue), S_OK);
    EXPECT_GE(fence->GetCompletedValue(), kCompletionValue);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);

    std::uint32_t *mapped = nullptr;
    const D3D12_RANGE read_range = {0, sizeof(expected)};
    ASSERT_EQ(readback_->Map(0, &read_range,
                             reinterpret_cast<void **>(&mapped)),
              S_OK);
    ASSERT_NE(mapped, nullptr);
    EXPECT_EQ(*mapped, expected);
    const D3D12_RANGE no_write = {0, 0};
    readback_->Unmap(0, &no_write);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12DescriptorHeap> descriptor_heap_;
  ComPtr<ID3D12Resource> output_;
  ComPtr<ID3D12Resource> readback_;
};

class ForcedExecutionPathSpec : public ::testing::Test {
protected:
  using ExecutionPathConfig = dxmt::d3d12::test::ExecutionPathConfig;
  using ExecutionPathMode = dxmt::d3d12::test::ExecutionPathMode;
  using ExecutionPathStats = dxmt::d3d12::test::ExecutionPathStats;

  struct RunResult {
    HRESULT execute_result = E_FAIL;
    std::array<std::uint32_t, 3> values = {};
    ExecutionPathStats stats = {};
  };

  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);

    const auto shader = CompileShader(R"(
      Buffer<uint> input : register(t0);
      RWBuffer<uint> output : register(u0);

      [numthreads(1, 1, 1)]
      void main() {
        uint previous;
        InterlockedAdd(output[1], 1u, previous);
        output[0] = input[0] * 3u + 7u;
        output[2] = previous;
      }
    )",
                                      "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 1;
    root_desc.pParameters = &parameter;
    root_signature_ = context_.CreateRootSignature(root_desc);
    ASSERT_TRUE(root_signature_);

    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    pipeline_ =
        context_.CreateComputePipeline(root_signature_.get(), bytecode);
    ASSERT_TRUE(pipeline_);

    constexpr std::uint32_t kInput = 11;
    input_ = context_.CreateUploadBuffer(sizeof(kInput), &kInput,
                                         sizeof(kInput));
    ASSERT_TRUE(input_);
  }

  void RunPath(ExecutionPathMode mode, std::uint32_t flags, RunResult *result,
               UINT dispatch_count = 1) {
    ASSERT_NE(result, nullptr);
    constexpr std::array<std::uint32_t, 3> kInitial = {
        0xdeadbeefu, 0u, 0xffffffffu};
    auto initial = context_.CreateUploadBuffer(
        sizeof(kInitial), kInitial.data(), sizeof(kInitial));
    auto output = context_.CreateBuffer(
        sizeof(kInitial), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto readback = context_.CreateBuffer(
        sizeof(kInitial), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(output);
    ASSERT_TRUE(readback);
    ASSERT_TRUE(heap);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = 1;
    context_.device()->CreateShaderResourceView(
        input_.get(), &srv, context_.CpuDescriptorHandle(heap.get(), 0));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = kInitial.size();
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        context_.CpuDescriptorHandle(heap.get(), 1));

    context_.list()->CopyBufferRegion(output.get(), 0, initial.get(), 0,
                                      sizeof(kInitial));
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);

    ExecutionPathConfig config = {};
    config.mode = mode;
    config.flags = flags;
    ASSERT_EQ(context_.list()->SetPrivateData(
                  dxmt::d3d12::test::kExecutionPathConfigGuid,
                  sizeof(config), &config),
              S_OK);
    ExecutionPathConfig observed_config = {};
    UINT config_size = sizeof(observed_config);
    ASSERT_EQ(context_.list()->GetPrivateData(
                  dxmt::d3d12::test::kExecutionPathConfigGuid, &config_size,
                  &observed_config),
              S_OK);
    EXPECT_EQ(config_size, sizeof(observed_config));
    EXPECT_EQ(observed_config.mode, config.mode);
    EXPECT_EQ(observed_config.flags, config.flags);

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline_.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    for (UINT dispatch = 0; dispatch < dispatch_count; ++dispatch) {
      context_.list()->Dispatch(1, 1, 1);
      if (dispatch + 1 != dispatch_count)
        D3D12TestContext::UavBarrier(context_.list(), output.get());
    }
    result->execute_result = context_.ExecuteAndWait();
    ASSERT_EQ(result->execute_result, S_OK);

    UINT stats_size = sizeof(result->stats);
    ASSERT_EQ(context_.list()->GetPrivateData(
                  dxmt::d3d12::test::kExecutionPathStatsGuid, &stats_size,
                  &result->stats),
              S_OK);
    ASSERT_EQ(stats_size, sizeof(result->stats));
    ASSERT_EQ(result->stats.struct_size, sizeof(result->stats));

    ASSERT_EQ(context_.ResetCommandList(), S_OK);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    context_.list()->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                      sizeof(result->values));
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

    void *mapping = nullptr;
    const D3D12_RANGE read_range = {0, sizeof(result->values)};
    ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
    ASSERT_NE(mapping, nullptr);
    std::memcpy(result->values.data(), mapping, sizeof(result->values));
    const D3D12_RANGE no_write = {0, 0};
    readback->Unmap(0, &no_write);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12Resource> input_;
};

DXMT_SERIAL_TEST_F(PathEmptySegmentSpec, EmptyNativeSegmentIsNoOp) {
  DispatchRealWork();
  D3D12TestContext::UavBarrier(context_.list(), output_.get());

  // Dispatch with a zero dimension is the public D3D12 representation of an
  // empty-work native packet. The compiler still builds a compute packet, but
  // replay must emit no encoder work and must not disturb the following packet.
  context_.list()->Dispatch(0, 1, 1);

  D3D12TestContext::UavBarrier(context_.list(), output_.get());
  DispatchRealWork();
  ExpectCounterAfterFence(2);
}

DXMT_SERIAL_TEST_F(PathEmptySegmentSpec, EmptyFallbackSegmentIsNoOp) {
  const D3D12_DISPATCH_ARGUMENTS arguments = {1, 1, 1};
  constexpr UINT kZeroCommandCount = 0;
  auto argument_buffer = context_.CreateUploadBuffer(
      sizeof(arguments), &arguments, sizeof(arguments));
  auto count_buffer = context_.CreateUploadBuffer(
      sizeof(kZeroCommandCount), &kZeroCommandCount,
      sizeof(kZeroCommandCount));
  ASSERT_TRUE(argument_buffer);
  ASSERT_TRUE(count_buffer);

  D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
  argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
  signature_desc.ByteStride = sizeof(arguments);
  signature_desc.NumArgumentDescs = 1;
  signature_desc.pArgumentDescs = &argument_desc;
  ComPtr<ID3D12CommandSignature> signature;
  ASSERT_EQ(context_.device()->CreateCommandSignature(
                &signature_desc, nullptr, __uuidof(ID3D12CommandSignature),
                reinterpret_cast<void **>(signature.put())),
            S_OK);

  DispatchRealWork();
  D3D12TestContext::UavBarrier(context_.list(), output_.get());

  // ExecuteIndirect is a fallback record. A zero count-buffer value makes the
  // record an empty-work fallback packet while retaining a real segment between
  // the surrounding native dispatches.
  context_.list()->ExecuteIndirect(signature.get(), 1, argument_buffer.get(),
                                   0, count_buffer.get(), 0);

  D3D12TestContext::UavBarrier(context_.list(), output_.get());
  DispatchRealWork();
  ExpectCounterAfterFence(2);
}

DXMT_SERIAL_TEST_F(PathEmptySegmentSpec, BarrierOnlyBoundaryPreservesState) {
  DispatchRealWork();

  // No descriptor, root-signature or pipeline state is rebound at this
  // boundary. The barrier-only fallback segment must order the UAV accesses
  // while preserving the compiled state needed by the next native dispatch.
  D3D12TestContext::UavBarrier(context_.list(), output_.get());

  DispatchRealWork();
  ExpectCounterAfterFence(2);
}

DXMT_SERIAL_TEST_F(ForcedExecutionPathSpec,
                   NativeFallbackAndAutoAreObservableAndEquivalent) {
  RunResult native;
  RunResult fallback;
  RunResult automatic;
  ASSERT_NO_FATAL_FAILURE(RunPath(ExecutionPathMode::NativeCompiled,
                                  dxmt::d3d12::test::ExecutionPathFlagNone,
                                  &native));
  ASSERT_NO_FATAL_FAILURE(RunPath(ExecutionPathMode::Fallback,
                                  dxmt::d3d12::test::ExecutionPathFlagNone,
                                  &fallback));
  ASSERT_NO_FATAL_FAILURE(RunPath(ExecutionPathMode::Auto,
                                  dxmt::d3d12::test::ExecutionPathFlagNone,
                                  &automatic));

  constexpr std::array<std::uint32_t, 3> kExpected = {40u, 1u, 0u};
  EXPECT_EQ(native.execute_result, S_OK);
  EXPECT_EQ(fallback.execute_result, S_OK);
  EXPECT_EQ(automatic.execute_result, S_OK);
  EXPECT_EQ(native.values, kExpected);
  EXPECT_EQ(fallback.values, kExpected);
  EXPECT_EQ(automatic.values, kExpected);
  EXPECT_EQ(native.values, fallback.values);
  EXPECT_EQ(native.values, automatic.values);

  EXPECT_EQ(native.stats.mode, ExecutionPathMode::NativeCompiled);
  EXPECT_EQ(native.stats.work_record_count, 1u);
  EXPECT_EQ(native.stats.compiled_work_record_count, 1u);
  EXPECT_EQ(native.stats.native_requirement_satisfied, 1u);
  EXPECT_EQ(native.stats.selected_compute_packets, 1u);
  EXPECT_EQ(native.stats.retained_compute_packets, 1u);
  EXPECT_EQ(native.stats.replayed_compute_packets, 1u);
  EXPECT_EQ(native.stats.replayed_compiled_packet_fallbacks, 0u);

  EXPECT_EQ(fallback.stats.mode, ExecutionPathMode::Fallback);
  EXPECT_GT(fallback.stats.record_count, 0u);
  EXPECT_EQ(fallback.stats.work_record_count, 1u);
  EXPECT_EQ(fallback.stats.compiled_work_record_count, 0u);
  EXPECT_EQ(fallback.stats.selected_compute_packets, 0u);
  EXPECT_EQ(fallback.stats.retained_graphics_packets, 0u);
  EXPECT_EQ(fallback.stats.retained_compute_packets, 0u);
  EXPECT_EQ(fallback.stats.has_native_root_base_buffer, 0u);
  EXPECT_EQ(fallback.stats.replayed_compute_packets, 0u);
  EXPECT_GT(fallback.stats.fallback_segments, 0u);
  EXPECT_GT(fallback.stats.replayed_fallback_ranges, 0u);
  EXPECT_EQ(fallback.stats.replayed_fallback_records,
            fallback.stats.record_count);
  EXPECT_EQ(fallback.stats.replayed_compiled_packet_fallbacks, 0u);

  EXPECT_EQ(automatic.stats.mode, ExecutionPathMode::Auto);
  EXPECT_EQ(automatic.stats.work_record_count, 1u);
  EXPECT_EQ(automatic.stats.compiled_work_record_count, 1u);
  EXPECT_EQ(automatic.stats.selected_compute_packets, 1u);
  EXPECT_EQ(automatic.stats.retained_compute_packets, 1u);
  EXPECT_EQ(automatic.stats.replayed_compute_packets, 1u);
  EXPECT_EQ(automatic.stats.replayed_compiled_packet_fallbacks, 0u);
}

DXMT_SERIAL_TEST_F(
    ForcedExecutionPathSpec,
    LiteralZeroRecordNativeAndFallbackSegmentsDoNotDropOrDuplicateWork) {
  RunResult result;
  constexpr std::uint32_t kEmptyFlags =
      dxmt::d3d12::test::ExecutionPathFlagInjectEmptyNativeSegment |
      dxmt::d3d12::test::ExecutionPathFlagInjectEmptyFallbackSegment;
  ASSERT_NO_FATAL_FAILURE(
      RunPath(ExecutionPathMode::Auto, kEmptyFlags, &result, 2));

  EXPECT_EQ(result.values,
            (std::array<std::uint32_t, 3>{40u, 2u, 1u}));
  EXPECT_EQ(result.stats.work_record_count, 2u);
  EXPECT_EQ(result.stats.compiled_work_record_count, 2u);
  EXPECT_EQ(result.stats.replayed_compute_packets, 2u);
  EXPECT_EQ(result.stats.replayed_compiled_packet_fallbacks, 0u);
  EXPECT_EQ(result.stats.empty_native_segments, 1u);
  EXPECT_EQ(result.stats.empty_fallback_segments, 1u);
  EXPECT_EQ(result.stats.replayed_empty_native_segments, 1u);
  EXPECT_EQ(result.stats.replayed_empty_fallback_segments, 1u);
}

} // namespace
