#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class SplitTransitionSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  void ExpectSplitTransition(bool separate_submissions) {
    const std::array<std::uint32_t, 8> expected = {
        0x10203040, 0x50607080, 0x90a0b0c0, 0xd0e0f000,
        0x0f1e2d3c, 0x4b5a6978, 0x8796a5b4, 0xc3d2e1f0,
    };
    auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                              sizeof(expected));
    auto resource = context_.CreateBuffer(
        sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto readback = context_.CreateBuffer(
        sizeof(expected), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(resource);
    ASSERT_TRUE(readback);

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

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

    lists[0]->CopyBufferRegion(resource.get(), 0, upload.get(), 0,
                               sizeof(expected));
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    lists[0]->ResourceBarrier(1, &barrier);
    ASSERT_TRUE(SUCCEEDED(lists[0]->Close()));

    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
    lists[1]->ResourceBarrier(1, &barrier);
    lists[1]->CopyBufferRegion(readback.get(), 0, resource.get(), 0,
                               sizeof(expected));
    ASSERT_TRUE(SUCCEEDED(lists[1]->Close()));

    ID3D12CommandList *submission[] = {lists[0].get(), lists[1].get()};
    if (separate_submissions) {
      context_.queue()->ExecuteCommandLists(1, &submission[0]);
      ASSERT_TRUE(SUCCEEDED(context_.SignalAndWait()));
      context_.queue()->ExecuteCommandLists(1, &submission[1]);
    } else {
      context_.queue()->ExecuteCommandLists(ARRAYSIZE(submission), submission);
    }
    ASSERT_TRUE(SUCCEEDED(context_.SignalAndWait()));

    void *mapping = nullptr;
    const D3D12_RANGE read_range = {0, sizeof(expected)};
    ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
    EXPECT_EQ(std::memcmp(mapping, expected.data(), sizeof(expected)), 0);
    const D3D12_RANGE written_range = {0, 0};
    readback->Unmap(0, &written_range);
  }

  D3D12TestContext context_;
};

TEST_F(SplitTransitionSpec, BeginAndEndAcrossCommandLists) {
  ExpectSplitTransition(false);
}

TEST_F(SplitTransitionSpec, BeginAndEndAcrossExecuteCalls) {
  ExpectSplitTransition(true);
}

} // namespace
