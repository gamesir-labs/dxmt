#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct CommandListPair {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
};

class CommandTypeLegalitySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  CommandListPair CreateList(D3D12_COMMAND_LIST_TYPE type) {
    CommandListPair pair;
    EXPECT_EQ(context_.device()->CreateCommandAllocator(
                  type, __uuidof(ID3D12CommandAllocator),
                  reinterpret_cast<void **>(pair.allocator.put())),
              S_OK);
    if (!pair.allocator)
      return pair;
    EXPECT_EQ(context_.device()->CreateCommandList(
                  0, type, pair.allocator.get(), nullptr,
                  __uuidof(ID3D12GraphicsCommandList),
                  reinterpret_cast<void **>(pair.list.put())),
              S_OK);
    return pair;
  }

  D3D12TestContext context_;
};


TEST_F(CommandTypeLegalitySpec, ReportsCreationTypeForEveryListKind) {
  for (const auto type : {D3D12_COMMAND_LIST_TYPE_DIRECT,
                          D3D12_COMMAND_LIST_TYPE_BUNDLE,
                          D3D12_COMMAND_LIST_TYPE_COMPUTE,
                          D3D12_COMMAND_LIST_TYPE_COPY}) {
    SCOPED_TRACE(static_cast<UINT>(type));
    auto pair = CreateList(type);
    ASSERT_TRUE(pair.list);
    EXPECT_EQ(pair.list->GetType(), type);
    EXPECT_EQ(pair.list->Close(), S_OK);
  }
}


TEST_F(CommandTypeLegalitySpec, AcceptsCopiesOnEveryCopyCapableList) {
  constexpr std::array<UINT, 4> values = {1, 2, 3, 4};
  auto source = context_.CreateUploadBuffer(
      sizeof(values), values.data(), sizeof(values));
  auto destination = context_.CreateBuffer(
      sizeof(values), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  for (const auto type : {D3D12_COMMAND_LIST_TYPE_DIRECT,
                          D3D12_COMMAND_LIST_TYPE_COMPUTE,
                          D3D12_COMMAND_LIST_TYPE_COPY}) {
    SCOPED_TRACE(static_cast<UINT>(type));
    auto pair = CreateList(type);
    ASSERT_TRUE(pair.list);
    pair.list->CopyBufferRegion(destination.get(), 0, source.get(), 0,
                                sizeof(values));
    EXPECT_EQ(pair.list->Close(), S_OK);
  }
}

TEST_F(CommandTypeLegalitySpec, AcceptsBarriersOnEveryBarrierCapableList) {
  auto resource = context_.CreateBuffer(
      256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(resource);
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource.get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

  for (const auto type : {D3D12_COMMAND_LIST_TYPE_DIRECT,
                          D3D12_COMMAND_LIST_TYPE_COMPUTE,
                          D3D12_COMMAND_LIST_TYPE_COPY}) {
    SCOPED_TRACE(static_cast<UINT>(type));
    auto pair = CreateList(type);
    ASSERT_TRUE(pair.list);
    pair.list->ResourceBarrier(1, &barrier);
    EXPECT_EQ(pair.list->Close(), S_OK);
  }
}




} // namespace
