#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

using dxmt::test::ComPtr;
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

} // namespace
