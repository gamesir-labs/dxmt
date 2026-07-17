#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

// A literal zero-record compiled segment is not representable: replay's
// structural invariant rejects record_count == 0. These tests exercise the
// public API's observable equivalent, a real native/fallback record whose
// requested work count is zero, between records that produce visible work.
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

} // namespace
