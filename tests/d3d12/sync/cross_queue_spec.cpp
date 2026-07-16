#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;

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

struct QueuePair {
  D3D12_COMMAND_LIST_TYPE producer;
  D3D12_COMMAND_LIST_TYPE consumer;
};

class CrossQueueSpec : public ::testing::TestWithParam<QueuePair> {
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

  HRESULT ExecuteAcrossQueues(const QueuePair &pair,
                              ID3D12CommandList *producer_list,
                              ID3D12CommandList *consumer_list) {
    auto producer_queue = CreateQueue(pair.producer);
    auto consumer_queue = CreateQueue(pair.consumer);
    if (!producer_queue || !consumer_queue)
      return E_FAIL;
    ComPtr<ID3D12Fence> fence;
    HRESULT hr = context_.device()->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
        reinterpret_cast<void **>(fence.put()));
    if (FAILED(hr))
      return hr;

    producer_queue->ExecuteCommandLists(1, &producer_list);
    if (FAILED(hr = producer_queue->Signal(fence.get(), 1)) ||
        FAILED(hr = consumer_queue->Wait(fence.get(), 1)))
      return hr;
    consumer_queue->ExecuteCommandLists(1, &consumer_list);
    if (FAILED(hr = consumer_queue->Signal(fence.get(), 2)))
      return hr;
    return context_.WaitForFence(fence.get(), 2);
  }

  D3D12TestContext context_;
};

TEST_P(CrossQueueSpec, FenceMakesBufferWritesVisible) {
  constexpr std::array<UINT, 16> expected = {
      0x01020304, 0x11121314, 0x21222324, 0x31323334,
      0x41424344, 0x51525354, 0x61626364, 0x71727374,
      0x81828384, 0x91929394, 0xa1a2a3a4, 0xb1b2b3b4,
      0xc1c2c3c4, 0xd1d2d3d4, 0xe1e2e3e4, 0xf1f2f3f4,
  };
  const auto pair = GetParam();
  auto upload = context_.CreateUploadBuffer(
      sizeof(expected), expected.data(), sizeof(expected));
  D3D12_RESOURCE_DESC intermediate_desc = {};
  intermediate_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  intermediate_desc.Width = sizeof(expected);
  intermediate_desc.Height = 1;
  intermediate_desc.DepthOrArraySize = 1;
  intermediate_desc.MipLevels = 1;
  intermediate_desc.SampleDesc.Count = 1;
  intermediate_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  const auto allocation = context_.device()->GetResourceAllocationInfo(
      0, 1, &intermediate_desc);
  ASSERT_GT(allocation.SizeInBytes, 0u);
  ASSERT_GT(allocation.Alignment, 0u);
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = allocation.Alignment + allocation.SizeInBytes;
  heap_desc.Alignment = allocation.Alignment;
  heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_desc.Properties.CreationNodeMask = 1;
  heap_desc.Properties.VisibleNodeMask = 1;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  ComPtr<ID3D12Heap> heap;
  ASSERT_EQ(context_.device()->CreateHeap(
                &heap_desc, __uuidof(ID3D12Heap),
                reinterpret_cast<void **>(heap.put())),
            S_OK);
  ComPtr<ID3D12Resource> intermediate;
  ASSERT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), allocation.Alignment, &intermediate_desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(intermediate.put())),
            S_OK);
  auto readback = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(intermediate);
  ASSERT_TRUE(readback);

  ComPtr<ID3D12CommandAllocator> producer_allocator;
  ComPtr<ID3D12CommandAllocator> consumer_allocator;
  auto producer_list = CreateList(pair.producer, &producer_allocator);
  auto consumer_list = CreateList(pair.consumer, &consumer_allocator);
  ASSERT_TRUE(producer_list);
  ASSERT_TRUE(consumer_list);
  producer_list->CopyBufferRegion(intermediate.get(), 0, upload.get(), 0,
                                  sizeof(expected));
  consumer_list->CopyBufferRegion(readback.get(), 0, intermediate.get(), 0,
                                  sizeof(expected));
  ASSERT_EQ(producer_list->Close(), S_OK);
  ASSERT_EQ(consumer_list->Close(), S_OK);
  ASSERT_EQ(ExecuteAcrossQueues(pair, producer_list.get(), consumer_list.get()),
            S_OK);

  void *mapped = nullptr;
  const D3D12_RANGE read_range = {0, sizeof(expected)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapped), S_OK);
  ASSERT_NE(mapped, nullptr);
  EXPECT_EQ(std::memcmp(mapped, expected.data(), sizeof(expected)), 0);
  const D3D12_RANGE written_range = {};
  readback->Unmap(0, &written_range);
}

TEST_P(CrossQueueSpec, FenceMakesTextureWritesVisible) {
  constexpr UINT kWidth = 5;
  constexpr UINT kHeight = 3;
  constexpr std::array<UINT, kWidth * kHeight> expected = {
      0xff000001, 0xff000002, 0xff000003, 0xff000004, 0xff000005,
      0xff001001, 0xff001002, 0xff001003, 0xff001004, 0xff001005,
      0xff002001, 0xff002002, 0xff002003, 0xff002004, 0xff002005,
  };
  const auto pair = GetParam();
  auto texture = context_.CreateTexture2D(
      kWidth, kHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(texture);

  const auto desc = texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT rows = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  context_.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows,
                                            &row_size, &total_size);
  ASSERT_EQ(rows, kHeight);
  ASSERT_EQ(row_size, kWidth * sizeof(UINT));
  auto upload = context_.CreateUploadBuffer(total_size);
  auto readback = context_.CreateBuffer(
      total_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(readback);
  void *mapped = nullptr;
  const D3D12_RANGE no_read = {};
  ASSERT_EQ(upload->Map(0, &no_read, &mapped), S_OK);
  std::memset(mapped, 0, static_cast<std::size_t>(total_size));
  for (UINT y = 0; y < kHeight; ++y) {
    std::memcpy(static_cast<std::uint8_t *>(mapped) +
                    footprint.Offset + y * footprint.Footprint.RowPitch,
                expected.data() + y * kWidth,
                kWidth * sizeof(UINT));
  }
  const D3D12_RANGE written = {0, static_cast<SIZE_T>(total_size)};
  upload->Unmap(0, &written);

  ComPtr<ID3D12CommandAllocator> producer_allocator;
  ComPtr<ID3D12CommandAllocator> consumer_allocator;
  auto producer_list = CreateList(pair.producer, &producer_allocator);
  auto consumer_list = CreateList(pair.consumer, &consumer_allocator);
  ASSERT_TRUE(producer_list);
  ASSERT_TRUE(consumer_list);
  D3D12_TEXTURE_COPY_LOCATION upload_location = {};
  upload_location.pResource = upload.get();
  upload_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  upload_location.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION texture_location = {};
  texture_location.pResource = texture.get();
  texture_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION readback_location = {};
  readback_location.pResource = readback.get();
  readback_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  readback_location.PlacedFootprint = footprint;
  producer_list->CopyTextureRegion(&texture_location, 0, 0, 0,
                                   &upload_location, nullptr);
  consumer_list->CopyTextureRegion(&readback_location, 0, 0, 0,
                                   &texture_location, nullptr);
  ASSERT_EQ(producer_list->Close(), S_OK);
  ASSERT_EQ(consumer_list->Close(), S_OK);
  ASSERT_EQ(ExecuteAcrossQueues(pair, producer_list.get(), consumer_list.get()),
            S_OK);

  const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(total_size)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapped), S_OK);
  for (UINT y = 0; y < kHeight; ++y) {
    EXPECT_EQ(std::memcmp(static_cast<const std::uint8_t *>(mapped) +
                              footprint.Offset +
                              y * footprint.Footprint.RowPitch,
                          expected.data() + y * kWidth,
                          kWidth * sizeof(UINT)),
              0)
        << "row " << y;
  }
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
}

class CrossQueueUavCopySpec : public CrossQueueSpec {};

TEST_P(CrossQueueUavCopySpec, FencePublishesDescriptorBackedUavWrites) {
  constexpr UINT kElementCount = 16;
  constexpr UINT kBase = 0x10203040;
  const auto shader = CompileShader(R"(
    RWByteAddressBuffer output : register(u0);

    [numthreads(16, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      output.Store(id.x * 4, 0x10203040u + id.x);
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
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline =
      context_.CreateComputePipeline(root_signature.get(), bytecode);
  auto output = context_.CreateBuffer(
      kElementCount * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context_.CreateBuffer(
      kElementCount * sizeof(UINT), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(output);
  ASSERT_TRUE(readback);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kElementCount;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      heap->GetCPUDescriptorHandleForHeapStart());

  const auto pair = GetParam();
  ComPtr<ID3D12CommandAllocator> producer_allocator;
  ComPtr<ID3D12CommandAllocator> consumer_allocator;
  auto producer_list = CreateList(pair.producer, &producer_allocator);
  auto consumer_list = CreateList(pair.consumer, &consumer_allocator);
  ASSERT_TRUE(producer_list);
  ASSERT_TRUE(consumer_list);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  producer_list->SetDescriptorHeaps(1, heaps);
  producer_list->SetPipelineState(pipeline.get());
  producer_list->SetComputeRootSignature(root_signature.get());
  producer_list->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  producer_list->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      producer_list.get(), output.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
  consumer_list->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                  kElementCount * sizeof(UINT));
  ASSERT_EQ(producer_list->Close(), S_OK);
  ASSERT_EQ(consumer_list->Close(), S_OK);
  ASSERT_EQ(ExecuteAcrossQueues(pair, producer_list.get(), consumer_list.get()),
            S_OK);

  void *mapped = nullptr;
  const D3D12_RANGE read_range = {0, kElementCount * sizeof(UINT)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapped), S_OK);
  const auto *actual = static_cast<const UINT *>(mapped);
  for (UINT i = 0; i < kElementCount; ++i)
    EXPECT_EQ(actual[i], kBase + i) << "element " << i;
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
}

class CrossQueueDescriptorSpec : public CrossQueueSpec {};

TEST_P(CrossQueueDescriptorSpec, FencePublishesUavWritesToDescriptorReads) {
  constexpr UINT kElementCount = 16;
  constexpr UINT kBase = 0x24680000;
  const auto producer_shader = CompileShader(R"(
    RWByteAddressBuffer output : register(u0);

    [numthreads(16, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      output.Store(id.x * 4, 0x24680000u + id.x);
    }
  )",
                                             "cs_5_0");
  const auto consumer_shader = CompileShader(R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);

    [numthreads(16, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      output.Store(id.x * 4, input.Load(id.x * 4) ^ 0xffffffffu);
    }
  )",
                                             "cs_5_0");
  ASSERT_EQ(producer_shader.result, S_OK)
      << producer_shader.diagnostic_text();
  ASSERT_EQ(consumer_shader.result, S_OK)
      << consumer_shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE producer_range = {};
  producer_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  producer_range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER producer_parameter = {};
  producer_parameter.ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  producer_parameter.DescriptorTable.NumDescriptorRanges = 1;
  producer_parameter.DescriptorTable.pDescriptorRanges = &producer_range;
  D3D12_ROOT_SIGNATURE_DESC producer_root_desc = {};
  producer_root_desc.NumParameters = 1;
  producer_root_desc.pParameters = &producer_parameter;
  auto producer_root = context_.CreateRootSignature(producer_root_desc);

  D3D12_DESCRIPTOR_RANGE consumer_ranges[2] = {};
  consumer_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  consumer_ranges[0].NumDescriptors = 1;
  consumer_ranges[0].OffsetInDescriptorsFromTableStart = 0;
  consumer_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  consumer_ranges[1].NumDescriptors = 1;
  consumer_ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_ROOT_PARAMETER consumer_parameter = {};
  consumer_parameter.ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  consumer_parameter.DescriptorTable.NumDescriptorRanges = 2;
  consumer_parameter.DescriptorTable.pDescriptorRanges = consumer_ranges;
  D3D12_ROOT_SIGNATURE_DESC consumer_root_desc = {};
  consumer_root_desc.NumParameters = 1;
  consumer_root_desc.pParameters = &consumer_parameter;
  auto consumer_root = context_.CreateRootSignature(consumer_root_desc);
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
  auto intermediate = context_.CreateBuffer(
      kElementCount * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto output = context_.CreateBuffer(
      kElementCount * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context_.CreateBuffer(
      kElementCount * sizeof(UINT), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  auto producer_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  auto consumer_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(producer_pipeline);
  ASSERT_TRUE(consumer_pipeline);
  ASSERT_TRUE(intermediate);
  ASSERT_TRUE(output);
  ASSERT_TRUE(readback);
  ASSERT_TRUE(producer_heap);
  ASSERT_TRUE(consumer_heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kElementCount;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = kElementCount;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      intermediate.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(producer_heap.get(), 0));
  context_.device()->CreateShaderResourceView(
      intermediate.get(), &srv,
      context_.CpuDescriptorHandle(consumer_heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(consumer_heap.get(), 1));

  const auto pair = GetParam();
  ComPtr<ID3D12CommandAllocator> producer_allocator;
  ComPtr<ID3D12CommandAllocator> consumer_allocator;
  auto producer_list = CreateList(pair.producer, &producer_allocator);
  auto consumer_list = CreateList(pair.consumer, &consumer_allocator);
  ASSERT_TRUE(producer_list);
  ASSERT_TRUE(consumer_list);
  ID3D12DescriptorHeap *producer_heaps[] = {producer_heap.get()};
  producer_list->SetDescriptorHeaps(1, producer_heaps);
  producer_list->SetPipelineState(producer_pipeline.get());
  producer_list->SetComputeRootSignature(producer_root.get());
  producer_list->SetComputeRootDescriptorTable(
      0, producer_heap->GetGPUDescriptorHandleForHeapStart());
  producer_list->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      producer_list.get(), intermediate.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

  D3D12TestContext::Transition(
      consumer_list.get(), intermediate.get(), D3D12_RESOURCE_STATE_COMMON,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap *consumer_heaps[] = {consumer_heap.get()};
  consumer_list->SetDescriptorHeaps(1, consumer_heaps);
  consumer_list->SetPipelineState(consumer_pipeline.get());
  consumer_list->SetComputeRootSignature(consumer_root.get());
  consumer_list->SetComputeRootDescriptorTable(
      0, consumer_heap->GetGPUDescriptorHandleForHeapStart());
  consumer_list->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      consumer_list.get(), output.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  consumer_list->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                  kElementCount * sizeof(UINT));
  ASSERT_EQ(producer_list->Close(), S_OK);
  ASSERT_EQ(consumer_list->Close(), S_OK);
  ASSERT_EQ(ExecuteAcrossQueues(pair, producer_list.get(), consumer_list.get()),
            S_OK);

  void *mapped = nullptr;
  const D3D12_RANGE read_range = {0, kElementCount * sizeof(UINT)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapped), S_OK);
  const auto *actual = static_cast<const UINT *>(mapped);
  for (UINT i = 0; i < kElementCount; ++i)
    EXPECT_EQ(actual[i], (kBase + i) ^ 0xffffffffu) << "element " << i;
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
}

INSTANTIATE_TEST_SUITE_P(
    QueueMatrix, CrossQueueSpec,
    ::testing::Values(
        QueuePair{D3D12_COMMAND_LIST_TYPE_COPY,
                  D3D12_COMMAND_LIST_TYPE_DIRECT},
        QueuePair{D3D12_COMMAND_LIST_TYPE_COPY,
                  D3D12_COMMAND_LIST_TYPE_COMPUTE},
        QueuePair{D3D12_COMMAND_LIST_TYPE_COMPUTE,
                  D3D12_COMMAND_LIST_TYPE_DIRECT},
        QueuePair{D3D12_COMMAND_LIST_TYPE_DIRECT,
                  D3D12_COMMAND_LIST_TYPE_COMPUTE},
        QueuePair{D3D12_COMMAND_LIST_TYPE_DIRECT,
                  D3D12_COMMAND_LIST_TYPE_COPY},
        QueuePair{D3D12_COMMAND_LIST_TYPE_COMPUTE,
                  D3D12_COMMAND_LIST_TYPE_COPY}),
    [](const ::testing::TestParamInfo<QueuePair> &info) {
      return std::string(QueueTypeName(info.param.producer)) + "To" +
             QueueTypeName(info.param.consumer);
    });

INSTANTIATE_TEST_SUITE_P(
    QueueMatrix, CrossQueueUavCopySpec,
    ::testing::Values(
        QueuePair{D3D12_COMMAND_LIST_TYPE_COMPUTE,
                  D3D12_COMMAND_LIST_TYPE_DIRECT},
        QueuePair{D3D12_COMMAND_LIST_TYPE_COMPUTE,
                  D3D12_COMMAND_LIST_TYPE_COPY},
        QueuePair{D3D12_COMMAND_LIST_TYPE_DIRECT,
                  D3D12_COMMAND_LIST_TYPE_COMPUTE},
        QueuePair{D3D12_COMMAND_LIST_TYPE_DIRECT,
                  D3D12_COMMAND_LIST_TYPE_COPY}),
    [](const ::testing::TestParamInfo<QueuePair> &info) {
      return std::string(QueueTypeName(info.param.producer)) + "To" +
             QueueTypeName(info.param.consumer);
    });

INSTANTIATE_TEST_SUITE_P(
    QueueMatrix, CrossQueueDescriptorSpec,
    ::testing::Values(
        QueuePair{D3D12_COMMAND_LIST_TYPE_COMPUTE,
                  D3D12_COMMAND_LIST_TYPE_DIRECT},
        QueuePair{D3D12_COMMAND_LIST_TYPE_DIRECT,
                  D3D12_COMMAND_LIST_TYPE_COMPUTE}),
    [](const ::testing::TestParamInfo<QueuePair> &info) {
      return std::string(QueueTypeName(info.param.producer)) + "To" +
             QueueTypeName(info.param.consumer);
    });

} // namespace
