#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class CommandAllocatorLifecycleSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12TestContext context_;
};

TEST_F(CommandAllocatorLifecycleSpec, ResetAfterCreationSucceeds) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(allocator.put()))));

  EXPECT_TRUE(SUCCEEDED(allocator->Reset()));
}

TEST_F(CommandAllocatorLifecycleSpec,
       CreationRejectsInvalidInputsAndRecovers) {
  const GUID unsupported = {0x2d810dee,
                            0xa9d1,
                            0x4d0d,
                            {0x9f, 0xe4, 0x88, 0xa8, 0x70, 0x27, 0xd9, 0xb3}};
  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(context_.device()->CreateCommandAllocator(
                static_cast<D3D12_COMMAND_LIST_TYPE>(UINT_MAX),
                __uuidof(ID3D12CommandAllocator), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);

  output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, unsupported, &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator), nullptr),
            E_POINTER);

  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(allocator.put())),
            S_OK);
  ASSERT_TRUE(allocator);
  EXPECT_EQ(allocator->Reset(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(CommandAllocatorLifecycleSpec, ResetWhileRecordingFails) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(allocator.put()))));
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(list.put()))));

  EXPECT_EQ(allocator->Reset(), E_FAIL);
  ASSERT_EQ(list->Close(), S_OK);
  EXPECT_EQ(allocator->Reset(), S_OK);
}

TEST_F(CommandAllocatorLifecycleSpec, ResetAfterListCloseSucceeds) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(allocator.put()))));
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(list.put()))));
  ASSERT_TRUE(SUCCEEDED(list->Close()));

  EXPECT_TRUE(SUCCEEDED(allocator->Reset()));
}

TEST_F(CommandAllocatorLifecycleSpec,
       ReusesAllocatorAcrossDifferentListsSequentially) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_EQ(context_.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(allocator.put())),
            S_OK);

  ComPtr<ID3D12GraphicsCommandList> first_list;
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                IID_PPV_ARGS(first_list.put())),
            S_OK);
  ASSERT_EQ(first_list->Close(), S_OK);
  ASSERT_EQ(allocator->Reset(), S_OK);

  ComPtr<ID3D12GraphicsCommandList> second_list;
  ASSERT_EQ(context_.device()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
                IID_PPV_ARGS(second_list.put())),
            S_OK);
  EXPECT_EQ(second_list->Close(), S_OK);
  EXPECT_EQ(allocator->Reset(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
