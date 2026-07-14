#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class FenceSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  ComPtr<ID3D12Fence> CreateFence(UINT64 initial_value = 0) {
    ComPtr<ID3D12Fence> fence;
    EXPECT_TRUE(SUCCEEDED(context_.device()->CreateFence(
        initial_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()))));
    return fence;
  }

  D3D12TestContext context_;
};

TEST_F(FenceSpec, CpuSignalUpdatesCompletedValue) {
  auto fence = CreateFence();
  ASSERT_TRUE(fence);

  ASSERT_TRUE(SUCCEEDED(fence->Signal(7)));
  EXPECT_EQ(fence->GetCompletedValue(), 7ull);
}

TEST_F(FenceSpec, MultipleEventsForSameValue) {
  auto fence = CreateFence();
  ASSERT_TRUE(fence);
  HANDLE events[] = {CreateEventW(nullptr, FALSE, FALSE, nullptr),
                     CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  ASSERT_NE(events[0], nullptr);
  ASSERT_NE(events[1], nullptr);

  ASSERT_TRUE(SUCCEEDED(fence->SetEventOnCompletion(3, events[0])));
  ASSERT_TRUE(SUCCEEDED(fence->SetEventOnCompletion(3, events[1])));
  ASSERT_TRUE(SUCCEEDED(context_.queue()->Signal(fence.get(), 3)));
  EXPECT_EQ(WaitForMultipleObjects(2, events, TRUE, 5000), WAIT_OBJECT_0);

  CloseHandle(events[0]);
  CloseHandle(events[1]);
}

TEST_F(FenceSpec, MultipleEventsForDifferentValues) {
  auto fence = CreateFence();
  ASSERT_TRUE(fence);
  HANDLE first = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HANDLE second = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  ASSERT_TRUE(SUCCEEDED(fence->SetEventOnCompletion(2, first)));
  ASSERT_TRUE(SUCCEEDED(fence->SetEventOnCompletion(5, second)));
  ASSERT_TRUE(SUCCEEDED(context_.queue()->Signal(fence.get(), 2)));
  EXPECT_EQ(WaitForSingleObject(first, 5000), WAIT_OBJECT_0);
  EXPECT_EQ(WaitForSingleObject(second, 0), WAIT_TIMEOUT);
  ASSERT_TRUE(SUCCEEDED(context_.queue()->Signal(fence.get(), 5)));
  EXPECT_EQ(WaitForSingleObject(second, 5000), WAIT_OBJECT_0);

  CloseHandle(first);
  CloseHandle(second);
}

} // namespace
