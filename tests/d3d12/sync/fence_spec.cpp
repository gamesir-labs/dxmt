#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <atomic>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;

class LifetimeProbe final : public IUnknown {
public:
  explicit LifetimeProbe(std::shared_ptr<std::atomic_bool> destroyed)
      : destroyed_(std::move(destroyed)) {}

  ~LifetimeProbe() { destroyed_->store(true, std::memory_order_release); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
    if (!object)
      return E_POINTER;
    *object = nullptr;
    if (iid != __uuidof(IUnknown))
      return E_NOINTERFACE;
    *object = this;
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return references_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG references =
        references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (!references)
      delete this;
    return references;
  }

private:
  std::atomic_ulong references_{1};
  std::shared_ptr<std::atomic_bool> destroyed_;
};

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

TEST_F(FenceSpec, SetEventOnAlreadyCompletedValueSignalsImmediately) {
  auto fence = CreateFence(5);
  ASSERT_TRUE(fence);
  EXPECT_EQ(fence->GetCompletedValue(), 5ull);

  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  ASSERT_EQ(fence->SetEventOnCompletion(3, event), S_OK);
  // Value 3 is already satisfied by the initial completed value of 5.
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_OBJECT_0);

  ASSERT_EQ(fence->SetEventOnCompletion(5, event), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_OBJECT_0);
  CloseHandle(event);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(FenceSpec, SetEventOnFutureValueSignalsAfterCompletion) {
  auto fence = CreateFence();
  ASSERT_TRUE(fence);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  ASSERT_EQ(fence->SetEventOnCompletion(9, event), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);

  ASSERT_EQ(fence->Signal(9), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);
  CloseHandle(event);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

#ifdef __ID3D12Fence1_INTERFACE_DEFINED__
TEST_F(FenceSpec, VersionedInterfacePreservesInitialValueAndCreationFlags) {
  for (const auto flags : {D3D12_FENCE_FLAG_NONE, D3D12_FENCE_FLAG_SHARED}) {
    ComPtr<ID3D12Fence> fence;
    ASSERT_EQ(context_.device()->CreateFence(
                  13, flags, IID_PPV_ARGS(fence.put())),
              S_OK)
        << "flags " << static_cast<UINT>(flags);
    ASSERT_TRUE(fence);
    EXPECT_EQ(fence->GetCompletedValue(), 13u);

    ComPtr<ID3D12Fence1> fence1;
    ASSERT_EQ(fence->QueryInterface(IID_PPV_ARGS(fence1.put())), S_OK);
    ASSERT_TRUE(fence1);
    EXPECT_EQ(fence1->GetCreationFlags(), flags);

    ComPtr<IUnknown> base_identity;
    ComPtr<IUnknown> versioned_identity;
    ASSERT_EQ(fence->QueryInterface(IID_PPV_ARGS(base_identity.put())), S_OK);
    ASSERT_EQ(fence1->QueryInterface(IID_PPV_ARGS(versioned_identity.put())),
              S_OK);
    EXPECT_EQ(base_identity.get(), versioned_identity.get());
  }
}
#endif

TEST_F(FenceSpec, DestructionSignalsPendingCompletionEvent) {
  auto fence = CreateFence();
  ASSERT_TRUE(fence);
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  ASSERT_EQ(fence->SetEventOnCompletion(1, event), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);

  fence.reset();
  EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);
  CloseHandle(event);
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

TEST_F(FenceSpec, MultipleFenceFailuresAreAtomicAndRecoverable) {
  auto first = CreateFence();
  ASSERT_TRUE(first);
  auto destroyed = std::make_shared<std::atomic_bool>(false);
  auto *probe = new LifetimeProbe(destroyed);
  ASSERT_EQ(first->SetPrivateDataInterface(__uuidof(IUnknown), probe), S_OK);
  probe->Release();

  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  const UINT64 values[] = {0, 1};
  ID3D12Fence *partial[] = {first.get(), nullptr};
  EXPECT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                nullptr, values, 1, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL,
                event),
            E_INVALIDARG);
  EXPECT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                partial, nullptr, 1, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL,
                event),
            E_INVALIDARG);
  EXPECT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                nullptr, nullptr, 0, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL,
                event),
            E_INVALIDARG);
  EXPECT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                partial, values, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL,
                event),
            E_INVALIDARG);
  EXPECT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                partial, values, 1,
                static_cast<D3D12_MULTIPLE_FENCE_WAIT_FLAGS>(2), event),
            E_INVALIDARG);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);

  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  ComPtr<ID3D12Fence> foreign_fence;
  ASSERT_EQ(foreign_device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(foreign_fence.put())),
            S_OK);
  ID3D12Fence *foreign[] = {foreign_fence.get()};
  EXPECT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                foreign, values, 1, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL,
                event),
            E_INVALIDARG);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);

  ID3D12Fence *valid[] = {first.get()};
  ASSERT_EQ(device1_->SetEventOnMultipleFenceCompletion(
                valid, values, 1, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, event),
            S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);

  first.reset();
  EXPECT_TRUE(destroyed->load(std::memory_order_acquire));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  CloseHandle(event);
}

TEST_F(FenceSpec, QueueRejectsForeignFenceAndRecovers) {
  auto foreign_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(foreign_device);
  ComPtr<ID3D12Fence> foreign_fence;
  ASSERT_EQ(foreign_device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(foreign_fence.put())),
            S_OK);
  ASSERT_TRUE(foreign_fence);

  EXPECT_EQ(context_.queue()->Signal(nullptr, 1), E_INVALIDARG);
  EXPECT_EQ(context_.queue()->Wait(nullptr, 1), E_INVALIDARG);
  EXPECT_EQ(context_.queue()->Signal(foreign_fence.get(), 2), E_INVALIDARG);
  EXPECT_EQ(context_.queue()->Wait(foreign_fence.get(), 2), E_INVALIDARG);
  EXPECT_EQ(foreign_fence->GetCompletedValue(), 0u);

  auto local_fence = CreateFence();
  ASSERT_TRUE(local_fence);
  ASSERT_EQ(context_.queue()->Signal(local_fence.get(), 3), S_OK);
  EXPECT_EQ(context_.WaitForFence(local_fence.get(), 3), S_OK);
  EXPECT_GE(local_fence->GetCompletedValue(), 3u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
