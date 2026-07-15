#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class CommandListLifecycleSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12TestContext context_;
};

TEST_F(CommandListLifecycleSpec, CreateCommandListStartsRecording) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(allocator.put()))));
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(list.put()))));

  EXPECT_TRUE(SUCCEEDED(list->Close()));
}

TEST_F(CommandListLifecycleSpec, CreateCommandList1StartsClosed) {
  ComPtr<ID3D12Device4> device4;
  ASSERT_TRUE(SUCCEEDED(context_.device()->QueryInterface(
      __uuidof(ID3D12Device4), reinterpret_cast<void **>(device4.put()))));
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_TRUE(SUCCEEDED(device4->CreateCommandList1(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(list.put()))));

  EXPECT_EQ(list->Close(), E_FAIL);
}

TEST_F(CommandListLifecycleSpec, ResetClosedListSucceeds) {
  ASSERT_TRUE(SUCCEEDED(context_.list()->Close()));

  EXPECT_TRUE(SUCCEEDED(context_.list()->Reset(context_.allocator(), nullptr)));
}

TEST_F(CommandListLifecycleSpec, ResetRecordingListFails) {
  EXPECT_EQ(context_.list()->Reset(context_.allocator(), nullptr), E_FAIL);
}

TEST_F(CommandListLifecycleSpec, ResetWithNullAllocatorFails) {
  ASSERT_TRUE(SUCCEEDED(context_.list()->Close()));

  EXPECT_TRUE(FAILED(context_.list()->Reset(nullptr, nullptr)));
}

TEST_F(CommandListLifecycleSpec, ResetWithAllocatorUsedByRecordingListFails) {
  ASSERT_TRUE(SUCCEEDED(context_.list()->Close()));
  ComPtr<ID3D12CommandAllocator> occupied_allocator;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(occupied_allocator.put()))));
  ComPtr<ID3D12GraphicsCommandList> recording_list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, occupied_allocator.get(), nullptr,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(recording_list.put()))));

  EXPECT_TRUE(
      FAILED(context_.list()->Reset(occupied_allocator.get(), nullptr)));
}

TEST_F(CommandListLifecycleSpec, FailedCloseCannotBeReset) {
  D3D12_RESOURCE_BARRIER invalid = {};
  invalid.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  invalid.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  invalid.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  context_.list()->ResourceBarrier(1, &invalid);

  EXPECT_EQ(context_.list()->Close(), E_INVALIDARG);
  EXPECT_EQ(context_.list()->Reset(context_.allocator(), nullptr), E_FAIL);
}

} // namespace
