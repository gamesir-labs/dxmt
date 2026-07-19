#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>
#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;

UINT ReadUint(const std::vector<std::uint8_t> &bytes, UINT64 offset) {
  UINT value = 0;
  if (offset + sizeof(value) <= bytes.size())
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

HRESULT MapReadbackBuffer(ID3D12Resource *readback, UINT64 size,
                          std::vector<std::uint8_t> *bytes) {
  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, static_cast<SIZE_T>(size)};
  HRESULT hr = readback->Map(0, &read_range, &mapped);
  if (FAILED(hr))
    return hr;
  bytes->resize(static_cast<std::size_t>(size));
  std::memcpy(bytes->data(), mapped, bytes->size());
  D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
  return S_OK;
}

// Precompiled SM5 DXBC from Wine's indirect-dispatch append-buffer test.
// One thread appends {4,2,1}, {4,1,1}, and {3,1,1} in order.
constexpr DWORD kAppendShader[] = {
    0x43425844, 0x954de75a, 0x8bb1b78b, 0x84ded464, 0x9d9532b7,
    0x00000001, 0x00000158, 0x00000003, 0x0000002c, 0x0000003c,
    0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
    0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853,
    0x00000104, 0x00050050, 0x00000041, 0x0100086a, 0x0400009e,
    0x0011e000, 0x00000000, 0x0000000c, 0x02000068, 0x00000001,
    0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x050000b2,
    0x00100012, 0x00000000, 0x0011e000, 0x00000000, 0x0c0000a8,
    0x0011e072, 0x00000000, 0x0010000a, 0x00000000, 0x00004001,
    0x00000000, 0x00004002, 0x00000004, 0x00000002, 0x00000001,
    0x00000000, 0x050000b2, 0x00100012, 0x00000000, 0x0011e000,
    0x00000000, 0x0c0000a8, 0x0011e072, 0x00000000, 0x0010000a,
    0x00000000, 0x00004001, 0x00000000, 0x00004002, 0x00000004,
    0x00000001, 0x00000001, 0x00000000, 0x050000b2, 0x00100012,
    0x00000000, 0x0011e000, 0x00000000, 0x0c0000a8, 0x0011e072,
    0x00000000, 0x0010000a, 0x00000000, 0x00004001, 0x00000000,
    0x00004002, 0x00000003, 0x00000001, 0x00000001, 0x00000000,
    0x0100003e,
};

// Precompiled SM5 DXBC from Wine's UAV-counter test. Four threads atomically
// decrement u0's counter and copy the claimed element into the same u1 index.
constexpr DWORD kDecrementShader[] = {
    0x43425844, 0x957ef3dd, 0x9f317559, 0x09c8f12d, 0xdbfd98c8,
    0x00000001, 0x00000100, 0x00000003, 0x0000002c, 0x0000003c,
    0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
    0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853,
    0x000000ac, 0x00050050, 0x0000002b, 0x0100086a, 0x0480009e,
    0x0011e000, 0x00000000, 0x00000004, 0x0400009e, 0x0011e000,
    0x00000001, 0x00000004, 0x02000068, 0x00000001, 0x0400009b,
    0x00000004, 0x00000001, 0x00000001, 0x050000b3, 0x00100012,
    0x00000000, 0x0011e000, 0x00000000, 0x8b0000a7, 0x80002302,
    0x00199983, 0x00100022, 0x00000000, 0x0010000a, 0x00000000,
    0x00004001, 0x00000000, 0x0011e006, 0x00000000, 0x090000a8,
    0x0011e012, 0x00000001, 0x0010000a, 0x00000000, 0x00004001,
    0x00000000, 0x0010001a, 0x00000000, 0x0100003e,
};

class D3D12UavCounterSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12RootSignature> CreateRootSignature(UINT descriptor_count) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = descriptor_count;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    return context_.CreateRootSignature(desc);
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(ID3D12RootSignature *root, const DWORD *shader,
                 std::size_t shader_size) {
    return context_.CreateComputePipeline(root, {shader, shader_size});
  }

  ComPtr<ID3D12Resource>
  CreateInitializedBuffer(const void *data, UINT64 size,
                          D3D12_RESOURCE_FLAGS flags) {
    auto upload = context_.CreateUploadBuffer(size, data, size);
    auto buffer = context_.CreateBuffer(size, D3D12_HEAP_TYPE_DEFAULT, flags,
                                        D3D12_RESOURCE_STATE_COPY_DEST);
    EXPECT_TRUE(upload);
    EXPECT_TRUE(buffer);
    if (!upload || !buffer)
      return {};
    context_.list()->CopyBufferRegion(buffer.get(), 0, upload.get(), 0, size);
    uploads_.push_back(std::move(upload));
    return buffer;
  }

  void TransitionToUav(ID3D12Resource *resource) {
    D3D12TestContext::Transition(context_.list(), resource,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }

  void Dispatch(ID3D12RootSignature *root, ID3D12PipelineState *pipeline,
                ID3D12DescriptorHeap *heap) {
    ID3D12DescriptorHeap *heaps[] = {heap};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root);
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
  }

  void ExpectConsumeCopiesAllValuesAndReachesZero(const DWORD *shader,
                                                   std::size_t shader_size,
                                                   const char *path_label);

  void RunAppendCase(UINT64 counter_offset,
                     bool counter_in_data_resource = false) {
    constexpr UINT kSentinel = 0xaaaaaaaau;
    constexpr std::array<UINT, 15> kInitialData = {
        kSentinel, kSentinel, kSentinel, kSentinel, kSentinel,
        kSentinel, kSentinel, kSentinel, kSentinel, kSentinel,
        kSentinel, kSentinel, kSentinel, kSentinel, kSentinel};
    const UINT64 counter_size = counter_offset + sizeof(UINT);
    std::vector<std::uint8_t> initial_counter(counter_size);
    constexpr UINT kOffsetZeroMarker = 0x13579bdfu;
    constexpr UINT kInitialCounter = 1;
    std::memcpy(initial_counter.data(), &kOffsetZeroMarker,
                sizeof(kOffsetZeroMarker));
    std::memcpy(initial_counter.data() + counter_offset, &kInitialCounter,
                sizeof(kInitialCounter));

    auto root = CreateRootSignature(1);
    auto pipeline =
        CreatePipeline(root.get(), kAppendShader, sizeof(kAppendShader));
    const UINT64 data_size = counter_in_data_resource
                                 ? counter_size
                                 : sizeof(kInitialData);
    std::vector<std::uint8_t> initial_data(data_size);
    std::memcpy(initial_data.data(), kInitialData.data(),
                sizeof(kInitialData));
    if (counter_in_data_resource) {
      std::memcpy(initial_data.data() + counter_offset, &kInitialCounter,
                  sizeof(kInitialCounter));
    }
    auto data = CreateInitializedBuffer(
        initial_data.data(), initial_data.size(),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> counter;
    if (!counter_in_data_resource) {
      counter = CreateInitializedBuffer(
          initial_counter.data(), initial_counter.size(),
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    ASSERT_TRUE(root);
    ASSERT_TRUE(pipeline);
    ASSERT_TRUE(data);
    ASSERT_TRUE(counter_in_data_resource || counter);
    ASSERT_TRUE(heap);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = kInitialData.size() / 3;
    uav.Buffer.StructureByteStride = 3 * sizeof(UINT);
    uav.Buffer.CounterOffsetInBytes = counter_offset;
    context_.device()->CreateUnorderedAccessView(
        data.get(), counter_in_data_resource ? data.get() : counter.get(), &uav,
        heap->GetCPUDescriptorHandleForHeapStart());
    TransitionToUav(data.get());
    if (!counter_in_data_resource)
      TransitionToUav(counter.get());
    Dispatch(root.get(), pipeline.get(), heap.get());
    D3D12TestContext::Transition(
        context_.list(), data.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (!counter_in_data_resource) {
      D3D12TestContext::Transition(
          context_.list(), counter.get(),
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    auto data_readback = context_.CreateBuffer(
        data_size, D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ComPtr<ID3D12Resource> counter_readback;
    if (!counter_in_data_resource) {
      counter_readback = context_.CreateBuffer(
          counter_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
          D3D12_RESOURCE_STATE_COPY_DEST);
    }
    ASSERT_TRUE(data_readback);
    ASSERT_TRUE(counter_in_data_resource || counter_readback);
    context_.list()->CopyBufferRegion(data_readback.get(), 0, data.get(), 0,
                                      data_size);
    if (!counter_in_data_resource) {
      context_.list()->CopyBufferRegion(
          counter_readback.get(), 0, counter.get(), 0, counter_size);
    }
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

    std::vector<std::uint8_t> data_bytes;
    ASSERT_EQ(MapReadbackBuffer(data_readback.get(), data_size, &data_bytes),
              S_OK);
    EXPECT_EQ(ReadUint(data_bytes, 0), kSentinel);
    EXPECT_EQ(ReadUint(data_bytes, sizeof(UINT)), kSentinel);
    EXPECT_EQ(ReadUint(data_bytes, 2 * sizeof(UINT)), kSentinel);
    const std::array<UINT, 9> expected_appended = {4, 2, 1, 4, 1,
                                                   1, 3, 1, 1};
    for (std::size_t i = 0; i < expected_appended.size(); ++i) {
      EXPECT_EQ(ReadUint(data_bytes, (i + 3) * sizeof(UINT)),
                expected_appended[i])
          << "component=" << i;
    }
    EXPECT_EQ(ReadUint(data_bytes, 12 * sizeof(UINT)), kSentinel);
    EXPECT_EQ(ReadUint(data_bytes, 13 * sizeof(UINT)), kSentinel);
    EXPECT_EQ(ReadUint(data_bytes, 14 * sizeof(UINT)), kSentinel);

    std::vector<std::uint8_t> counter_bytes;
    if (counter_in_data_resource) {
      counter_bytes = data_bytes;
    } else {
      ASSERT_EQ(MapReadbackBuffer(counter_readback.get(), counter_size,
                                  &counter_bytes),
                S_OK);
    }
    EXPECT_EQ(ReadUint(counter_bytes, counter_offset), 4u);
    if (counter_offset && !counter_in_data_resource) {
      EXPECT_EQ(ReadUint(counter_bytes, 0), kOffsetZeroMarker);
    }
  }

  D3D12TestContext context_;
  std::vector<ComPtr<ID3D12Resource>> uploads_;
};

TEST_F(D3D12UavCounterSpec, AppendUsesInitialCounterAndUpdatesResource) {
  RunAppendCase(0);
}

TEST_F(D3D12UavCounterSpec, AppendHonorsNonzeroAlignedCounterOffset) {
  RunAppendCase(D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);
}

TEST_F(D3D12UavCounterSpec, AppendSupportsCounterInDataResource) {
  RunAppendCase(D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT, true);
}

TEST_F(D3D12UavCounterSpec, AppendHonorsLastValidAlignedCounterOffset) {
  // size == 2 * alignment + sizeof(UINT) makes 2 * alignment the final legal
  // CounterOffsetInBytes for this counter resource.
  RunAppendCase(2 * D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);
}

TEST_F(D3D12UavCounterSpec, AppendIsVisibleAcrossCommandListsWithUavBarrier) {
  constexpr UINT kSentinel = 0xaaaaaaaau;
  // Enough structured elements for counter=1..6 appends (two bursts of three).
  constexpr UINT kElementCount = 8;
  constexpr UINT kComponentCount = kElementCount * 3;
  std::vector<UINT> initial_data(kComponentCount, kSentinel);
  constexpr UINT kInitialCounter = 1;
  auto root = CreateRootSignature(1);
  auto pipeline =
      CreatePipeline(root.get(), kAppendShader, sizeof(kAppendShader));
  auto data = CreateInitializedBuffer(
      initial_data.data(), initial_data.size() * sizeof(UINT),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto counter = CreateInitializedBuffer(
      &kInitialCounter, sizeof(kInitialCounter),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(data);
  ASSERT_TRUE(counter);
  ASSERT_TRUE(heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kElementCount;
  uav.Buffer.StructureByteStride = 3 * sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      data.get(), counter.get(), &uav,
      heap->GetCPUDescriptorHandleForHeapStart());
  TransitionToUav(data.get());
  TransitionToUav(counter.get());

  ComPtr<ID3D12CommandAllocator> second_allocator;
  ComPtr<ID3D12GraphicsCommandList> second_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(second_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, second_allocator.get(),
                nullptr, __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(second_list.put())),
            S_OK);

  // First list: three appends from counter=1 -> counter becomes 4.
  Dispatch(root.get(), pipeline.get(), heap.get());
  ASSERT_EQ(context_.list()->Close(), S_OK);

  // Second list: resource UAV barrier then three more appends from the
  // published counter value.
  D3D12TestContext::UavBarrier(second_list.get(), data.get());
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  second_list->SetDescriptorHeaps(1, heaps);
  second_list->SetComputeRootSignature(root.get());
  second_list->SetPipelineState(pipeline.get());
  second_list->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  second_list->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      second_list.get(), data.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      second_list.get(), counter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  const UINT64 data_size = initial_data.size() * sizeof(UINT);
  auto data_readback = context_.CreateBuffer(
      data_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto counter_readback = context_.CreateBuffer(
      sizeof(kInitialCounter), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(data_readback);
  ASSERT_TRUE(counter_readback);
  second_list->CopyBufferRegion(data_readback.get(), 0, data.get(), 0,
                                data_size);
  second_list->CopyBufferRegion(counter_readback.get(), 0, counter.get(), 0,
                                sizeof(kInitialCounter));
  ASSERT_EQ(second_list->Close(), S_OK);

  ID3D12CommandList *first = context_.list();
  context_.queue()->ExecuteCommandLists(1, &first);
  ID3D12CommandList *second = second_list.get();
  context_.queue()->ExecuteCommandLists(1, &second);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);

  std::vector<std::uint8_t> data_bytes;
  ASSERT_EQ(MapReadbackBuffer(data_readback.get(), data_size, &data_bytes),
            S_OK);
  // Element 0 stays sentinel; slots 1-3 and 4-6 receive the two append bursts.
  EXPECT_EQ(ReadUint(data_bytes, 0), kSentinel);
  const std::array<UINT, 9> burst = {4, 2, 1, 4, 1, 1, 3, 1, 1};
  for (std::size_t i = 0; i < burst.size(); ++i) {
    EXPECT_EQ(ReadUint(data_bytes, (i + 3) * sizeof(UINT)), burst[i])
        << "first-burst component=" << i;
  }
  // Second burst starts at structured index 4 (counter after first list).
  for (std::size_t i = 0; i < burst.size(); ++i) {
    EXPECT_EQ(ReadUint(data_bytes, (i + 12) * sizeof(UINT)), burst[i])
        << "second-burst component=" << i;
  }
  std::vector<std::uint8_t> counter_bytes;
  ASSERT_EQ(MapReadbackBuffer(counter_readback.get(), sizeof(kInitialCounter),
                              &counter_bytes),
            S_OK);
  EXPECT_EQ(ReadUint(counter_bytes, 0), 7u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

void D3D12UavCounterSpec::ExpectConsumeCopiesAllValuesAndReachesZero(
    const DWORD *shader, std::size_t shader_size, const char *path_label) {
  SCOPED_TRACE(path_label);
  constexpr std::array<UINT, 4> kInitialData = {10, 20, 30, 40};
  constexpr UINT kInitialCounter = 4;
  constexpr std::array<UINT, 4> kInitialOutput = {};
  auto root = CreateRootSignature(2);
  auto pipeline = CreatePipeline(root.get(), shader, shader_size);
  auto data = CreateInitializedBuffer(
      kInitialData.data(), sizeof(kInitialData),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto counter = CreateInitializedBuffer(
      &kInitialCounter, sizeof(kInitialCounter),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto output = CreateInitializedBuffer(
      kInitialOutput.data(), sizeof(kInitialOutput),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(root);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(data);
  ASSERT_TRUE(counter);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC counter_uav = {};
  counter_uav.Format = DXGI_FORMAT_UNKNOWN;
  counter_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  counter_uav.Buffer.NumElements = kInitialData.size();
  counter_uav.Buffer.StructureByteStride = sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      data.get(), counter.get(), &counter_uav,
      context_.CpuDescriptorHandle(heap.get(), 0));
  D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
  output_uav.Format = DXGI_FORMAT_UNKNOWN;
  output_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  output_uav.Buffer.NumElements = kInitialOutput.size();
  output_uav.Buffer.StructureByteStride = sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &output_uav,
      context_.CpuDescriptorHandle(heap.get(), 1));
  TransitionToUav(data.get());
  TransitionToUav(counter.get());
  TransitionToUav(output.get());
  Dispatch(root.get(), pipeline.get(), heap.get());
  D3D12TestContext::Transition(
      context_.list(), counter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  auto output_readback = context_.CreateBuffer(
      sizeof(kInitialOutput), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  auto counter_readback = context_.CreateBuffer(
      sizeof(kInitialCounter), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(output_readback);
  ASSERT_TRUE(counter_readback);
  context_.list()->CopyBufferRegion(output_readback.get(), 0, output.get(), 0,
                                    sizeof(kInitialOutput));
  context_.list()->CopyBufferRegion(counter_readback.get(), 0, counter.get(), 0,
                                    sizeof(kInitialCounter));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

  std::vector<std::uint8_t> output_bytes;
  ASSERT_EQ(MapReadbackBuffer(output_readback.get(), sizeof(kInitialOutput),
                              &output_bytes),
            S_OK);
  for (std::size_t i = 0; i < kInitialData.size(); ++i) {
    EXPECT_EQ(ReadUint(output_bytes, i * sizeof(UINT)), kInitialData[i])
        << "element=" << i;
  }
  std::vector<std::uint8_t> counter_bytes;
  ASSERT_EQ(MapReadbackBuffer(counter_readback.get(), sizeof(kInitialCounter),
                              &counter_bytes),
            S_OK);
  EXPECT_EQ(ReadUint(counter_bytes, 0), 0u);
}

TEST_F(D3D12UavCounterSpec, DecrementCounterCopiesAllValuesAndReachesZero) {
  ExpectConsumeCopiesAllValuesAndReachesZero(
      kDecrementShader, sizeof(kDecrementShader), "DecrementCounterDxbc");
}

TEST_F(D3D12UavCounterSpec,
       ConsumeStructuredBufferCopiesAllValuesAndReachesZero) {
  // ConsumeStructuredBuffer.Consume() lowers to the same imm_atomic_consume
  // DXBC as DecrementCounter. Prefer a source-compiled path when the HLSL
  // frontend accepts ConsumeStructuredBuffer; otherwise execute the Wine
  // precompiled consume DXBC that matches that lowering.
  const auto source = CompileShader(R"(
    ConsumeStructuredBuffer<uint> input : register(u0);
    RWStructuredBuffer<uint> output : register(u1);
    [numthreads(4, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      uint value = input.Consume();
      output[id.x] = value;
    }
  )", "cs_5_0");
  if (source.result == S_OK && source.bytecode) {
    const auto *words = static_cast<const DWORD *>(
        source.bytecode->GetBufferPointer());
    ExpectConsumeCopiesAllValuesAndReachesZero(
        words, source.bytecode->GetBufferSize(), "ConsumeStructuredHLSL");
    return;
  }
  ExpectConsumeCopiesAllValuesAndReachesZero(
      kDecrementShader, sizeof(kDecrementShader),
      "ConsumeStructuredPrecompiledDxbc");
}

TEST_F(D3D12UavCounterSpec,
       AppendThenConsumeIsVisibleAcrossComputeAndDirectQueues) {
  // Compute queue appends three structured records; after a fence the direct
  // queue observes the updated counter and appended payload.
  constexpr UINT kSentinel = 0xaaaaaaaau;
  constexpr std::array<UINT, 15> kInitialData = {
      kSentinel, kSentinel, kSentinel, kSentinel, kSentinel,
      kSentinel, kSentinel, kSentinel, kSentinel, kSentinel,
      kSentinel, kSentinel, kSentinel, kSentinel, kSentinel};
  constexpr UINT kInitialCounter = 1;

  auto root_append = CreateRootSignature(1);
  auto append_pipeline =
      CreatePipeline(root_append.get(), kAppendShader, sizeof(kAppendShader));
  auto data = CreateInitializedBuffer(
      kInitialData.data(), sizeof(kInitialData),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto counter = CreateInitializedBuffer(
      &kInitialCounter, sizeof(kInitialCounter),
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(root_append);
  ASSERT_TRUE(append_pipeline);
  ASSERT_TRUE(data);
  ASSERT_TRUE(counter);
  ASSERT_TRUE(heap);

  // Finish CPU uploads on the direct queue so resources start in COPY_DEST.
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kInitialData.size() / 3;
  uav.Buffer.StructureByteStride = 3 * sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      data.get(), counter.get(), &uav,
      heap->GetCPUDescriptorHandleForHeapStart());

  D3D12_COMMAND_QUEUE_DESC compute_desc = {};
  compute_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  ComPtr<ID3D12CommandQueue> compute_queue;
  ASSERT_EQ(context_.device()->CreateCommandQueue(
                &compute_desc, __uuidof(ID3D12CommandQueue),
                reinterpret_cast<void **>(compute_queue.put())),
            S_OK);
  ComPtr<ID3D12CommandAllocator> compute_allocator;
  ComPtr<ID3D12GraphicsCommandList> compute_list;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                __uuidof(ID3D12CommandAllocator),
                reinterpret_cast<void **>(compute_allocator.put())),
            S_OK);
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_COMPUTE, compute_allocator.get(),
                nullptr, __uuidof(ID3D12GraphicsCommandList),
                reinterpret_cast<void **>(compute_list.put())),
            S_OK);

  D3D12TestContext::Transition(compute_list.get(), data.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  D3D12TestContext::Transition(compute_list.get(), counter.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  compute_list->SetDescriptorHeaps(1, heaps);
  compute_list->SetComputeRootSignature(root_append.get());
  compute_list->SetPipelineState(append_pipeline.get());
  compute_list->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  compute_list->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(compute_list.get(), data.get());
  ASSERT_EQ(compute_list->Close(), S_OK);

  D3D12TestContext::Transition(
      context_.list(), counter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      context_.list(), data.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto counter_readback = context_.CreateBuffer(
      sizeof(kInitialCounter), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  auto data_readback = context_.CreateBuffer(
      sizeof(kInitialData), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(counter_readback);
  ASSERT_TRUE(data_readback);
  context_.list()->CopyBufferRegion(counter_readback.get(), 0, counter.get(), 0,
                                    sizeof(kInitialCounter));
  context_.list()->CopyBufferRegion(data_readback.get(), 0, data.get(), 0,
                                    sizeof(kInitialData));
  ASSERT_EQ(context_.list()->Close(), S_OK);

  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(fence.put())),
            S_OK);
  ID3D12CommandList *compute_lists[] = {compute_list.get()};
  compute_queue->ExecuteCommandLists(1, compute_lists);
  ASSERT_EQ(compute_queue->Signal(fence.get(), 1), S_OK);
  ASSERT_EQ(context_.queue()->Wait(fence.get(), 1), S_OK);
  ID3D12CommandList *direct_lists[] = {context_.list()};
  context_.queue()->ExecuteCommandLists(1, direct_lists);
  ASSERT_EQ(context_.queue()->Signal(fence.get(), 2), S_OK);
  ASSERT_EQ(context_.WaitForFence(fence.get(), 2), S_OK);

  std::vector<std::uint8_t> counter_bytes;
  ASSERT_EQ(MapReadbackBuffer(counter_readback.get(), sizeof(kInitialCounter),
                              &counter_bytes),
            S_OK);
  EXPECT_EQ(ReadUint(counter_bytes, 0), 4u);
  std::vector<std::uint8_t> data_bytes;
  ASSERT_EQ(MapReadbackBuffer(data_readback.get(), sizeof(kInitialData),
                              &data_bytes),
            S_OK);
  const std::array<UINT, 9> burst = {4, 2, 1, 4, 1, 1, 3, 1, 1};
  for (std::size_t i = 0; i < burst.size(); ++i) {
    EXPECT_EQ(ReadUint(data_bytes, (i + 3) * sizeof(UINT)), burst[i])
        << "component=" << i;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
