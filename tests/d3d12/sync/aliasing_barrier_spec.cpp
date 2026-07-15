#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class AliasingBarrierSpec : public ::testing::Test {
protected:
  enum class SubmissionMode {
    SameList,
    SeparateLists,
    SeparateExecutes,
  };

  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  ComPtr<ID3D12Heap> CreateHeap(UINT64 size) {
    D3D12_HEAP_DESC desc = {};
    desc.SizeInBytes = size;
    desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ComPtr<ID3D12Heap> heap;
    EXPECT_TRUE(SUCCEEDED(
        context_.device()->CreateHeap(&desc, IID_PPV_ARGS(heap.put()))));
    return heap;
  }

  ComPtr<ID3D12Resource> CreatePlacedBuffer(ID3D12Heap *heap, UINT64 size) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    EXPECT_TRUE(SUCCEEDED(context_.device()->CreatePlacedResource(
        heap, 0, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(resource.put()))));
    return resource;
  }

  void ExpectOverlappingResourcesOrdered(SubmissionMode mode) {
    constexpr UINT64 resource_size = 4096;
    constexpr std::array<std::uint32_t, 8> before_data = {
        0x10203040, 0x50607080, 0x90a0b0c0, 0xd0e0f000,
        0x0f1e2d3c, 0x4b5a6978, 0x8796a5b4, 0xc3d2e1f0,
    };
    constexpr std::array<std::uint32_t, 8> expected = {
        0xfedcba98, 0x76543210, 0x89abcdef, 0x01234567,
        0x55aa55aa, 0xaa55aa55, 0xdeadbeef, 0xc001d00d,
    };

    auto heap = CreateHeap(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    auto before = CreatePlacedBuffer(heap.get(), resource_size);
    auto after = CreatePlacedBuffer(heap.get(), resource_size);
    auto before_upload = context_.CreateUploadBuffer(
        sizeof(before_data), before_data.data(), sizeof(before_data));
    auto after_upload = context_.CreateUploadBuffer(
        sizeof(expected), expected.data(), sizeof(expected));
    auto readback = context_.CreateBuffer(
        sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(heap);
    ASSERT_TRUE(before);
    ASSERT_TRUE(after);
    ASSERT_TRUE(before_upload);
    ASSERT_TRUE(after_upload);
    ASSERT_TRUE(readback);

    auto record_before = [&](ID3D12GraphicsCommandList *list) {
      list->CopyBufferRegion(before.get(), 0, before_upload.get(), 0,
                             sizeof(before_data));
    };
    auto record_after = [&](ID3D12GraphicsCommandList *list) {
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
      barrier.Aliasing.pResourceBefore = before.get();
      barrier.Aliasing.pResourceAfter = after.get();
      list->ResourceBarrier(1, &barrier);
      list->CopyBufferRegion(after.get(), 0, after_upload.get(), 0,
                             sizeof(expected));
      D3D12TestContext::Transition(list, after.get(),
                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyBufferRegion(readback.get(), 0, after.get(), 0,
                             sizeof(expected));
    };

    if (mode == SubmissionMode::SameList) {
      record_before(context_.list());
      record_after(context_.list());
      ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
    } else {
      std::array<ComPtr<ID3D12CommandAllocator>, 2> allocators;
      std::array<ComPtr<ID3D12GraphicsCommandList>, 2> lists;
      for (UINT index = 0; index < lists.size(); ++index) {
        ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(allocators[index].put()))));
        ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[index].get(), nullptr,
            IID_PPV_ARGS(lists[index].put()))));
      }
      record_before(lists[0].get());
      record_after(lists[1].get());
      ASSERT_TRUE(SUCCEEDED(lists[0]->Close()));
      ASSERT_TRUE(SUCCEEDED(lists[1]->Close()));
      ID3D12CommandList *commands[] = {lists[0].get(), lists[1].get()};
      if (mode == SubmissionMode::SeparateLists) {
        context_.queue()->ExecuteCommandLists(ARRAYSIZE(commands), commands);
      } else {
        context_.queue()->ExecuteCommandLists(1, &commands[0]);
        context_.queue()->ExecuteCommandLists(1, &commands[1]);
      }
      ASSERT_TRUE(SUCCEEDED(context_.SignalAndWait()));
    }

    void *mapping = nullptr;
    const D3D12_RANGE read_range = {0, sizeof(expected)};
    ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
    std::array<std::uint32_t, expected.size()> actual = {};
    std::memcpy(actual.data(), mapping, sizeof(actual));
    EXPECT_EQ(actual, expected);
    const D3D12_RANGE no_write = {0, 0};
    readback->Unmap(0, &no_write);
  }

  D3D12TestContext context_;
};

TEST_F(AliasingBarrierSpec, OrdersOverlappingResourcesInSameCommandList) {
  ExpectOverlappingResourcesOrdered(SubmissionMode::SameList);
}

TEST_F(AliasingBarrierSpec, OrdersOverlappingResourcesAcrossCommandLists) {
  ExpectOverlappingResourcesOrdered(SubmissionMode::SeparateLists);
}

TEST_F(AliasingBarrierSpec, OrdersOverlappingResourcesAcrossExecuteCalls) {
  ExpectOverlappingResourcesOrdered(SubmissionMode::SeparateExecutes);
}

} // namespace
