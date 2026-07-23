#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <array>

// Public D3D11.4 fence synchronization coverage. Devices, contexts, fences,
// and events are test-local; event waits observe queue completion rather than
// relying on sleeps or shared queue ordering, so these cases are parallel-safe.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct ScopedEvent {
  ScopedEvent() : handle(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

  ScopedEvent(const ScopedEvent &) = delete;
  ScopedEvent &operator=(const ScopedEvent &) = delete;

  ~ScopedEvent() {
    if (handle)
      CloseHandle(handle);
  }

  HANDLE handle = nullptr;
};

class D3D11FenceSynchronizationContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device5), reinterpret_cast<void **>(device5_.put())),
        S_OK);

    ComPtr<ID3D11DeviceContext> immediate;
    context_.device()->GetImmediateContext(immediate.put());
    ASSERT_NE(immediate.get(), nullptr);
    ASSERT_EQ(
        immediate->QueryInterface(__uuidof(ID3D11DeviceContext4),
                                  reinterpret_cast<void **>(immediate4_.put())),
        S_OK);
    ASSERT_NE(immediate4_.get(), nullptr);
  }

  ComPtr<ID3D11Fence> CreateFence(UINT64 initial_value = 0) {
    ComPtr<ID3D11Fence> fence;
    EXPECT_EQ(device5_->CreateFence(initial_value, D3D11_FENCE_FLAG_NONE,
                                    __uuidof(ID3D11Fence),
                                    reinterpret_cast<void **>(fence.put())),
              S_OK);
    return fence;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device5> device5_;
  ComPtr<ID3D11DeviceContext4> immediate4_;
};

TEST_F(D3D11FenceSynchronizationContractSpec,
       SignalsEventForAlreadyCompletedValue) {
  ComPtr<ID3D11Fence> fence = CreateFence(5);
  ASSERT_NE(fence.get(), nullptr);
  ScopedEvent event;
  ASSERT_NE(event.handle, nullptr);

  ASSERT_EQ(fence->SetEventOnCompletion(5, event.handle), S_OK);
  EXPECT_EQ(WaitForSingleObject(event.handle, 5000), WAIT_OBJECT_0);
  EXPECT_EQ(fence->GetCompletedValue(), 5u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       ImmediateContextSignalAdvancesCompletedValue) {
  constexpr std::array<UINT64, 3> kSignalValues = {1, 7, 1024};
  ComPtr<ID3D11Fence> fence = CreateFence();
  ASSERT_NE(fence.get(), nullptr);

  for (const UINT64 value : kSignalValues) {
    ScopedEvent event;
    ASSERT_NE(event.handle, nullptr);
    ASSERT_EQ(fence->SetEventOnCompletion(value, event.handle), S_OK);
    EXPECT_EQ(WaitForSingleObject(event.handle, 0), WAIT_TIMEOUT);
    ASSERT_EQ(immediate4_->Signal(fence.get(), value), S_OK);
    ASSERT_EQ(WaitForSingleObject(event.handle, 5000), WAIT_OBJECT_0);
    EXPECT_GE(fence->GetCompletedValue(), value);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       ImmediateContextWaitOrdersFollowingSignal) {
  ComPtr<ID3D11Fence> gate = CreateFence(3);
  ComPtr<ID3D11Fence> completion = CreateFence();
  ASSERT_NE(gate.get(), nullptr);
  ASSERT_NE(completion.get(), nullptr);
  ScopedEvent event;
  ASSERT_NE(event.handle, nullptr);
  ASSERT_EQ(completion->SetEventOnCompletion(9, event.handle), S_OK);

  ASSERT_EQ(immediate4_->Wait(gate.get(), 3), S_OK);
  ASSERT_EQ(immediate4_->Signal(completion.get(), 9), S_OK);
  ASSERT_EQ(WaitForSingleObject(event.handle, 5000), WAIT_OBJECT_0);
  EXPECT_GE(completion->GetCompletedValue(), 9u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec, RejectsNullFences) {
  EXPECT_EQ(immediate4_->Signal(nullptr, 1), E_INVALIDARG);
  EXPECT_EQ(immediate4_->Wait(nullptr, 1), E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       DeferredContextRejectsSignalAndWait) {
  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_EQ(context_.device()->CreateDeferredContext(0, deferred.put()), S_OK);
  ASSERT_NE(deferred.get(), nullptr);
  ComPtr<ID3D11DeviceContext4> deferred4;
  ASSERT_EQ(
      deferred->QueryInterface(__uuidof(ID3D11DeviceContext4),
                               reinterpret_cast<void **>(deferred4.put())),
      S_OK);
  ASSERT_NE(deferred4.get(), nullptr);
  ComPtr<ID3D11Fence> fence = CreateFence();
  ASSERT_NE(fence.get(), nullptr);

  const HRESULT signal_result = deferred4->Signal(fence.get(), 1);
  const HRESULT wait_result = deferred4->Wait(fence.get(), 1);
  EXPECT_TRUE(FAILED(signal_result));
  EXPECT_TRUE(FAILED(wait_result));
  EXPECT_EQ(fence->GetCompletedValue(), 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
