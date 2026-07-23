#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <array>
#include <cstddef>
#include <set>

// Public D3D11.4 device-removal notification coverage. Every registration
// uses a test-local device and Win32 event, so the case is parallel-safe and
// does not depend on an actual device-removal event.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

const dxmt::test::TestCostRegistration
    kDeviceRemovedEventCost("D3D11DeviceRemovedEventContractSpec."
                            "ReturnsDistinctCookiesWithoutSpuriousSignals",
                            dxmt::test::kResourceTestCost);

struct ScopedEvent {
  ~ScopedEvent() {
    if (handle)
      CloseHandle(handle);
  }

  HANDLE handle = nullptr;
};

class D3D11DeviceRemovedEventContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device4), reinterpret_cast<void **>(device4_.put())),
        S_OK);
    ASSERT_NE(device4_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device4> device4_;
};

TEST_F(D3D11DeviceRemovedEventContractSpec,
       ReturnsDistinctCookiesWithoutSpuriousSignals) {
  std::array<ScopedEvent, 3> events;
  std::array<DWORD, 3> cookies = {};

  for (std::size_t i = 0; i < events.size(); ++i) {
    events[i].handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ASSERT_NE(events[i].handle, nullptr);
    ASSERT_EQ(
        device4_->RegisterDeviceRemovedEvent(events[i].handle, &cookies[i]),
        S_OK);
    EXPECT_NE(cookies[i], 0u);
    EXPECT_EQ(WaitForSingleObject(events[i].handle, 0), WAIT_TIMEOUT);
  }

  const std::set<DWORD> distinct_cookies(cookies.begin(), cookies.end());
  EXPECT_EQ(distinct_cookies.size(), cookies.size());

  for (std::size_t i = cookies.size(); i-- > 0;)
    device4_->UnregisterDeviceRemoved(cookies[i]);

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
