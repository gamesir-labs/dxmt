#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <cstdint>
#include <vector>

// Public D3D11 counter execution coverage. The case is serial because native
// performance counters may use a global non-exclusive counter bank. Queue
// completion is observed through a test-local D3D11 fence and Win32 event.

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

class D3D11CounterExecutionContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device5), reinterpret_cast<void **>(device5_.put())),
        S_OK);
    ASSERT_EQ(context_.context()->QueryInterface(
                  __uuidof(ID3D11DeviceContext4),
                  reinterpret_cast<void **>(context4_.put())),
              S_OK);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device5> device5_;
  ComPtr<ID3D11DeviceContext4> context4_;
};

DXMT_SERIAL_TEST_F(D3D11CounterExecutionContractSpec,
                   EnforcesLifecycleAndReturnsCompletedData) {
  D3D11_COUNTER_INFO info = {};
  context_.device()->CheckCounterInfo(&info);
  if (static_cast<std::uint32_t>(info.LastDeviceDependentCounter) == 0) {
    EXPECT_EQ(info.NumSimultaneousCounters, 0u);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    return;
  }

  const D3D11_COUNTER_DESC desc = {info.LastDeviceDependentCounter, 0};
  ComPtr<ID3D11Counter> counter;
  ASSERT_EQ(context_.device()->CreateCounter(&desc, counter.put()), S_OK);
  ASSERT_NE(counter.get(), nullptr);
  const UINT data_size = counter->GetDataSize();
  ASSERT_GT(data_size, 0u);
  std::vector<std::uint8_t> data(data_size, 0xcd);

  EXPECT_TRUE(FAILED(context_.context()->GetData(
      counter.get(), data.data(), data_size, D3D11_ASYNC_GETDATA_DONOTFLUSH)));
  context_.context()->Begin(counter.get());
  EXPECT_TRUE(FAILED(context_.context()->GetData(
      counter.get(), data.data(), data_size, D3D11_ASYNC_GETDATA_DONOTFLUSH)));
  EXPECT_EQ(context_.context()->GetData(counter.get(), data.data(),
                                        data_size - 1,
                                        D3D11_ASYNC_GETDATA_DONOTFLUSH),
            E_INVALIDARG);

  context_.context()->End(counter.get());

  ComPtr<ID3D11Fence> completion;
  ASSERT_EQ(device5_->CreateFence(0, D3D11_FENCE_FLAG_NONE,
                                  __uuidof(ID3D11Fence),
                                  reinterpret_cast<void **>(completion.put())),
            S_OK);
  ScopedEvent event;
  ASSERT_NE(event.handle, nullptr);
  ASSERT_EQ(completion->SetEventOnCompletion(1, event.handle), S_OK);
  ASSERT_EQ(context4_->Signal(completion.get(), 1), S_OK);
  ASSERT_EQ(WaitForSingleObject(event.handle, 5000), WAIT_OBJECT_0);

  EXPECT_EQ(context_.context()->GetData(counter.get(), data.data(), data_size,
                                        D3D11_ASYNC_GETDATA_DONOTFLUSH),
            S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
