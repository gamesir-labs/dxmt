#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct QueueDirection {
  D3D12_COMMAND_LIST_TYPE producer;
  D3D12_COMMAND_LIST_TYPE consumer;
};

const char *QueueTypeName(D3D12_COMMAND_LIST_TYPE type) {
  switch (type) {
  case D3D12_COMMAND_LIST_TYPE_DIRECT:
    return "Direct";
  case D3D12_COMMAND_LIST_TYPE_COMPUTE:
    return "Compute";
  case D3D12_COMMAND_LIST_TYPE_COPY:
    return "Copy";
  default:
    return "Unsupported";
  }
}

class QueueWaitSpec : public ::testing::TestWithParam<QueueDirection> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12CommandQueue> CreateQueue(D3D12_COMMAND_LIST_TYPE type) {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    ComPtr<ID3D12CommandQueue> queue;
    EXPECT_EQ(context_.device()->CreateCommandQueue(
                  &desc, __uuidof(ID3D12CommandQueue),
                  reinterpret_cast<void **>(queue.put())),
              S_OK);
    return queue;
  }

  ComPtr<ID3D12GraphicsCommandList>
  CreateList(D3D12_COMMAND_LIST_TYPE type,
             ComPtr<ID3D12CommandAllocator> *allocator) {
    EXPECT_EQ(context_.device()->CreateCommandAllocator(
                  type, __uuidof(ID3D12CommandAllocator),
                  reinterpret_cast<void **>(allocator->put())),
              S_OK);
    ComPtr<ID3D12GraphicsCommandList> list;
    EXPECT_EQ(context_.device()->CreateCommandList(
                  0, type, allocator->get(), nullptr,
                  __uuidof(ID3D12GraphicsCommandList),
                  reinterpret_cast<void **>(list.put())),
              S_OK);
    return list;
  }

  ComPtr<ID3D12RootSignature> CreateProducerRootSignature() {
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameter.Descriptor.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    return context_.CreateRootSignature(desc);
  }

  ComPtr<ID3D12RootSignature> CreateConsumerRootSignature() {
    std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = ranges.size();
    parameter.DescriptorTable.pDescriptorRanges = ranges.data();
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    return context_.CreateRootSignature(desc);
  }

  D3D12TestContext context_;
};

// A future queue wait intentionally leaves the consumer queue blocked while
// the producer queue is submitted. Keep this timing-sensitive cross-queue
// probe out of the parallel worker wave so unrelated GPU work cannot turn the
// deliberate stall into a device-removal timeout.
const dxmt::test::SerialTestRegistration kFutureQueueWaitSerial(
    "QueueDirections/QueueWaitSpec."
    "WaitSubmittedBeforeFutureSignalOrdersConsumer/*");
DXMT_GROUP_SERIAL_TESTS(
    "QueueDirections/QueueWaitSpec."
    "WaitSubmittedBeforeFutureSignalOrdersConsumer/*",
    "d3d12-queue-wait");
DXMT_SERIAL_TEST_DOMAIN(
    "QueueDirections/QueueWaitSpec."
    "WaitSubmittedBeforeFutureSignalOrdersConsumer/*",
    "queue-wait");

TEST_P(QueueWaitSpec, WaitSubmittedBeforeFutureSignalOrdersConsumer) {
  constexpr UINT kElementCount = 16;
  constexpr UINT kPayloadBase = 0x4a100000u;
  constexpr UINT kPayloadStride = 0x101u;
  constexpr UINT kExactOnceIndex = kElementCount;
  constexpr UINT64 kPayloadSize = kElementCount * sizeof(UINT);
  constexpr UINT64 kOutputSize = (kElementCount + 1) * sizeof(UINT);

  std::array<UINT, kElementCount> expected = {};
  for (UINT index = 0; index < kElementCount; ++index)
    expected[index] = kPayloadBase + index * kPayloadStride;
  const std::array<UINT, kElementCount + 1> zero_output = {};

  const auto producer_shader = CompileShader(R"(
    RWByteAddressBuffer output : register(u0);

    [numthreads(16, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      output.Store(id.x * 4, 0x4a100000u + id.x * 0x101u);
    }
  )",
                                             "cs_5_0");
  const auto consumer_shader = CompileShader(R"(
    Buffer<uint> input : register(t0);
    RWBuffer<uint> output : register(u0);

    [numthreads(16, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      uint original;
      InterlockedAdd(output[id.x], input[id.x], original);
      if (id.x == 0)
        InterlockedAdd(output[16], 1u, original);
    }
  )",
                                             "cs_5_0");
  ASSERT_EQ(producer_shader.result, S_OK)
      << producer_shader.diagnostic_text();
  ASSERT_EQ(consumer_shader.result, S_OK)
      << consumer_shader.diagnostic_text();

  auto producer_root = CreateProducerRootSignature();
  auto consumer_root = CreateConsumerRootSignature();
  ASSERT_TRUE(producer_root);
  ASSERT_TRUE(consumer_root);
  const D3D12_SHADER_BYTECODE producer_bytecode = {
      producer_shader.bytecode->GetBufferPointer(),
      producer_shader.bytecode->GetBufferSize()};
  const D3D12_SHADER_BYTECODE consumer_bytecode = {
      consumer_shader.bytecode->GetBufferPointer(),
      consumer_shader.bytecode->GetBufferSize()};
  auto producer_pipeline =
      context_.CreateComputePipeline(producer_root.get(), producer_bytecode);
  auto consumer_pipeline =
      context_.CreateComputePipeline(consumer_root.get(), consumer_bytecode);
  ASSERT_TRUE(producer_pipeline);
  ASSERT_TRUE(consumer_pipeline);

  auto payload_upload = context_.CreateUploadBuffer(
      kPayloadSize, expected.data(), sizeof(expected));
  auto zero_upload = context_.CreateUploadBuffer(
      kOutputSize, zero_output.data(), sizeof(zero_output));
  auto payload = context_.CreateBuffer(
      kPayloadSize, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COMMON);
  auto output = context_.CreateBuffer(
      kOutputSize, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context_.CreateBuffer(
      kOutputSize, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto consumer_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(payload_upload);
  ASSERT_TRUE(zero_upload);
  ASSERT_TRUE(payload);
  ASSERT_TRUE(output);
  ASSERT_TRUE(readback);
  ASSERT_TRUE(consumer_heap);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = kElementCount;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kElementCount + 1;
  context_.device()->CreateShaderResourceView(
      payload.get(), &srv, context_.CpuDescriptorHandle(consumer_heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(consumer_heap.get(), 1));

  context_.list()->CopyBufferRegion(output.get(), 0, zero_upload.get(), 0,
                                    kOutputSize);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

  const auto direction = GetParam();
  auto producer_queue = CreateQueue(direction.producer);
  auto consumer_queue = CreateQueue(direction.consumer);
  ASSERT_TRUE(producer_queue);
  ASSERT_TRUE(consumer_queue);

  ComPtr<ID3D12CommandAllocator> producer_allocator;
  ComPtr<ID3D12CommandAllocator> consumer_allocator;
  auto producer_list = CreateList(direction.producer, &producer_allocator);
  auto consumer_list = CreateList(direction.consumer, &consumer_allocator);
  ASSERT_TRUE(producer_list);
  ASSERT_TRUE(consumer_list);

  if (direction.producer == D3D12_COMMAND_LIST_TYPE_COPY) {
    D3D12TestContext::Transition(
        producer_list.get(), payload.get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);
    producer_list->CopyBufferRegion(payload.get(), 0, payload_upload.get(), 0,
                                    kPayloadSize);
    D3D12TestContext::Transition(
        producer_list.get(), payload.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COMMON);
  } else {
    D3D12TestContext::Transition(
        producer_list.get(), payload.get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    producer_list->SetComputeRootSignature(producer_root.get());
    producer_list->SetPipelineState(producer_pipeline.get());
    producer_list->SetComputeRootUnorderedAccessView(
        0, payload->GetGPUVirtualAddress());
    producer_list->Dispatch(1, 1, 1);
    D3D12TestContext::UavBarrier(producer_list.get(), payload.get());
    D3D12TestContext::Transition(
        producer_list.get(), payload.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COMMON);
  }

  D3D12TestContext::Transition(
      consumer_list.get(), payload.get(), D3D12_RESOURCE_STATE_COMMON,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap *consumer_heaps[] = {consumer_heap.get()};
  consumer_list->SetDescriptorHeaps(1, consumer_heaps);
  consumer_list->SetComputeRootSignature(consumer_root.get());
  consumer_list->SetPipelineState(consumer_pipeline.get());
  consumer_list->SetComputeRootDescriptorTable(
      0, consumer_heap->GetGPUDescriptorHandleForHeapStart());
  consumer_list->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(consumer_list.get(), output.get());
  D3D12TestContext::Transition(
      consumer_list.get(), output.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  consumer_list->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                  kOutputSize);
  ASSERT_EQ(producer_list->Close(), S_OK);
  ASSERT_EQ(consumer_list->Close(), S_OK);

  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(fence.put())),
            S_OK);
  ASSERT_TRUE(fence);

  // The consumer wait is deliberately enqueued before the producer has even
  // submitted its payload. Signal(2) is also queued behind the consumer work,
  // so completion value 2 proves the future wait was released and the
  // consumer submission retired.
  ASSERT_EQ(consumer_queue->Wait(fence.get(), 1), S_OK);
  ID3D12CommandList *consumer_lists[] = {consumer_list.get()};
  consumer_queue->ExecuteCommandLists(1, consumer_lists);
  ASSERT_EQ(consumer_queue->Signal(fence.get(), 2), S_OK);
  EXPECT_EQ(fence->GetCompletedValue(), 0u);

  ID3D12CommandList *producer_lists[] = {producer_list.get()};
  producer_queue->ExecuteCommandLists(1, producer_lists);
  ASSERT_EQ(producer_queue->Signal(fence.get(), 1), S_OK);
  ASSERT_EQ(context_.WaitForFence(fence.get(), 2), S_OK);
  EXPECT_EQ(fence->GetCompletedValue(), 2u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(kOutputSize)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  const auto *actual = static_cast<const UINT *>(mapping);
  for (UINT index = 0; index < kElementCount; ++index)
    EXPECT_EQ(actual[index], expected[index]) << "payload element " << index;
  EXPECT_EQ(actual[kExactOnceIndex], 1u)
      << "consumer dispatch must execute exactly once";
  const D3D12_RANGE no_write = {0, 0};
  readback->Unmap(0, &no_write);
}

INSTANTIATE_TEST_SUITE_P(
    QueueDirections, QueueWaitSpec,
    ::testing::Values(
        QueueDirection{D3D12_COMMAND_LIST_TYPE_COPY,
                       D3D12_COMMAND_LIST_TYPE_DIRECT},
        QueueDirection{D3D12_COMMAND_LIST_TYPE_COMPUTE,
                       D3D12_COMMAND_LIST_TYPE_DIRECT},
        QueueDirection{D3D12_COMMAND_LIST_TYPE_DIRECT,
                       D3D12_COMMAND_LIST_TYPE_COMPUTE}),
    [](const ::testing::TestParamInfo<QueueDirection> &info) {
      return std::string(QueueTypeName(info.param.producer)) + "To" +
             QueueTypeName(info.param.consumer);
    });

} // namespace
