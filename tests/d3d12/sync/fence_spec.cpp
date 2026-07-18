#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <iterator>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class FenceSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(context_.Initialize()));
    ASSERT_EQ(context_.device()->QueryInterface(IID_PPV_ARGS(device1_.put())),
              S_OK);
  }

  ComPtr<ID3D12Fence> CreateFence(UINT64 initial_value = 0) {
    ComPtr<ID3D12Fence> fence;
    EXPECT_TRUE(SUCCEEDED(context_.device()->CreateFence(
        initial_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()))));
    return fence;
  }

  D3D12TestContext context_;
  ComPtr<ID3D12Device1> device1_;
};

TEST_F(FenceSpec, CreationRequiresOutputAndClearsUnsupportedFlags) {
  EXPECT_EQ(context_.device()->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), nullptr),
            E_POINTER);

  for (const auto flags : {
           D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER,
           D3D12_FENCE_FLAG_NON_MONITORED,
           static_cast<D3D12_FENCE_FLAGS>(8),
       }) {
    void *output = reinterpret_cast<void *>(std::uintptr_t{1});
    EXPECT_EQ(context_.device()->CreateFence(
                  0, flags, __uuidof(ID3D12Fence), &output),
              E_NOTIMPL)
        << "flags " << static_cast<UINT>(flags);
    EXPECT_EQ(output, nullptr);
  }
}

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

TEST_F(FenceSpec, MultipleFenceWaitAllSignalsAfterEveryTarget) {
  auto first = CreateFence();
  auto second = CreateFence();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  ID3D12Fence *fences[] = {first.get(), second.get()};
  const UINT64 values[] = {2, 3};

  ASSERT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                fences, values, std::size(fences),
                D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, event),
            S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);
  ASSERT_EQ(first->Signal(values[0]), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);
  ASSERT_EQ(second->Signal(values[1]), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);

  CloseHandle(event);
}

TEST_F(FenceSpec, MultipleFenceWaitAnySignalsAfterFirstTarget) {
  auto first = CreateFence();
  auto second = CreateFence();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  ID3D12Fence *fences[] = {first.get(), second.get()};
  const UINT64 values[] = {2, 3};

  ASSERT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                fences, values, std::size(fences),
                D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, event),
            S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);
  ASSERT_EQ(second->Signal(values[1]), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);
  EXPECT_EQ(first->GetCompletedValue(), 0u);

  CloseHandle(event);
}

TEST_F(FenceSpec, MultipleFenceAlreadySatisfiedSignalsImmediately) {
  auto first = CreateFence(2);
  auto second = CreateFence(3);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  ID3D12Fence *fences[] = {first.get(), second.get()};
  const UINT64 values[] = {2, 3};

  ASSERT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                fences, values, std::size(fences),
                D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, event),
            S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);

  CloseHandle(event);
}

TEST_F(FenceSpec, MultipleFenceWaitAllTracksDuplicateFenceTargets) {
  auto fence = CreateFence();
  ASSERT_TRUE(fence);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  ID3D12Fence *fences[] = {fence.get(), fence.get()};
  const UINT64 values[] = {2, 5};

  ASSERT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                fences, values, std::size(fences),
                D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, event),
            S_OK);
  ASSERT_EQ(fence->Signal(values[0]), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);
  ASSERT_EQ(fence->Signal(values[1]), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);

  CloseHandle(event);
}

} // namespace
