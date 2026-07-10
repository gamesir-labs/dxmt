#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <iomanip>

namespace {

template <typename T> void release_object(T*& object) {
  if (object) {
    object->Release();
    object = nullptr;
  }
}

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();

  return ::testing::AssertionFailure()
      << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

class D3D12DeviceSpec : public ::testing::Test {
protected:
  void SetUp() override {
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   __uuidof(ID3D12Device),
                                   reinterpret_cast<void**>(&device_));

    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_NE(device_, nullptr);
  }

  void TearDown() override {
    release_object(device_);
  }

  ID3D12Device* device_ = nullptr;
};

} // namespace

TEST(D3D12DeviceCreationSpec, RejectsFeatureLevel93) {
  ID3D12Device* device = nullptr;

  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_9_3,
                                 __uuidof(ID3D12Device),
                                 reinterpret_cast<void**>(&device));

  EXPECT_EQ(hr, E_INVALIDARG);
  EXPECT_EQ(device, nullptr);

  release_object(device);
}

TEST_F(D3D12DeviceSpec, ReportsAtLeastOneNode) {
  EXPECT_GE(device_->GetNodeCount(), 1u);
}

TEST_F(D3D12DeviceSpec, CreatesDirectCommandQueue) {
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  desc.NodeMask = 0;

  ID3D12CommandQueue* queue = nullptr;

  HRESULT hr = device_->CreateCommandQueue(
      &desc, __uuidof(ID3D12CommandQueue),
      reinterpret_cast<void**>(&queue));

  EXPECT_TRUE(HResultSucceeded(hr));
  EXPECT_NE(queue, nullptr);

  release_object(queue);
}

TEST_F(D3D12DeviceSpec, CreatesDirectCommandAllocator) {
  ID3D12CommandAllocator* allocator = nullptr;

  HRESULT hr = device_->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void**>(&allocator));

  EXPECT_TRUE(HResultSucceeded(hr));
  EXPECT_NE(allocator, nullptr);

  release_object(allocator);
}

TEST_F(D3D12DeviceSpec, CreatesFenceWithInitialValue) {
  ID3D12Fence* fence = nullptr;

  HRESULT hr = device_->CreateFence(7, D3D12_FENCE_FLAG_NONE,
                                    __uuidof(ID3D12Fence),
                                    reinterpret_cast<void**>(&fence));

  ASSERT_TRUE(HResultSucceeded(hr));
  ASSERT_NE(fence, nullptr);

  EXPECT_EQ(fence->GetCompletedValue(), 7ull);

  release_object(fence);
}
