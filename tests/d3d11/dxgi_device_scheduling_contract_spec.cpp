#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_3.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

// Public DXGI-device coverage reached from a test-local D3D11 device. The
// mutable scheduling properties belong to that device, so the cases are safe
// under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct InterfaceCase {
  const GUID *iid;
  const char *name;
};

constexpr std::array kInterfaceCases = {
    InterfaceCase{&__uuidof(IDXGIDevice), "IDXGIDevice"},
    InterfaceCase{&__uuidof(IDXGIDevice1), "IDXGIDevice1"},
    InterfaceCase{&__uuidof(IDXGIDevice2), "IDXGIDevice2"},
    InterfaceCase{&__uuidof(IDXGIDevice3), "IDXGIDevice3"},
};

constexpr std::array<INT, 15> kThreadPriorities = {
    -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7,
};

constexpr std::array<UINT, 16> kFrameLatencies = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
};

const dxmt::test::LogicalCaseFamilyRegistration kInterfaceRegistration(
    "D3D11DxgiDeviceSchedulingContractSpec.ExposesOneComIdentityAcrossVersions",
    "D3D11.DXGI.Device.Interface.", kInterfaceCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device,IDXGIDevice,IDXGIDevice1,IDXGIDevice2,IDXGIDevice3,"
      "IDXGIDeviceGetAdapter,QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live DXGI-device interface per "
     "selected logical case",
     "query IDXGIDevice through IDXGIDevice3 from the public D3D11 device and "
     "retrieve its adapter",
     "every interface shares the D3D11 device's canonical IUnknown identity "
     "and returns the adapter used to create the device",
     "logical ID, selected-case count, interface name, HRESULTs, device and "
     "adapter identities, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kPriorityRegistration(
    "D3D11DxgiDeviceSchedulingContractSpec.RoundTripsGpuThreadPriorities",
    "D3D11.DXGI.Device.GpuThreadPriority.", kThreadPriorities.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIDeviceSetGPUThreadPriority,IDXGIDeviceGetGPUThreadPriority"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11/DXGI device and one integer scheduling property per "
     "selected logical case",
     "set and retrieve every documented GPU thread priority from -7 through "
     "+7",
     "each valid priority returns S_OK and round-trips exactly",
     "logical ID, selected-case count, requested and observed priorities, "
     "HRESULTs, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kLatencyRegistration(
    "D3D11DxgiDeviceSchedulingContractSpec.RoundTripsMaximumFrameLatencies",
    "D3D11.DXGI.Device.MaximumFrameLatency.", kFrameLatencies.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIDevice1SetMaximumFrameLatency,"
      "IDXGIDevice1GetMaximumFrameLatency"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11/DXGI device and one integer scheduling property per "
     "selected logical case",
     "set and retrieve every valid maximum frame latency from 1 through 16",
     "each valid latency returns S_OK and round-trips exactly",
     "logical ID, selected-case count, requested and observed latencies, "
     "HRESULTs, and exact replay argument"});

const dxmt::test::TestCostRegistration kInterfaceCost(
    "D3D11DxgiDeviceSchedulingContractSpec.ExposesOneComIdentityAcrossVersions",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kPriorityCost(
    "D3D11DxgiDeviceSchedulingContractSpec.RoundTripsGpuThreadPriorities",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kLatencyCost(
    "D3D11DxgiDeviceSchedulingContractSpec.RoundTripsMaximumFrameLatencies",
    dxmt::test::kResourceTestCost);

class D3D11DxgiDeviceSchedulingContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(context_.device()->QueryInterface(
                  __uuidof(IDXGIDevice1),
                  reinterpret_cast<void **>(dxgi_device1_.put())),
              S_OK);
    ASSERT_NE(dxgi_device1_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<IDXGIDevice1> dxgi_device1_;
};

TEST_F(D3D11DxgiDeviceSchedulingContractSpec,
       ExposesOneComIdentityAcrossVersions) {
  ComPtr<IUnknown> device_identity;
  ASSERT_EQ(
      context_.device()->QueryInterface(
          __uuidof(IUnknown), reinterpret_cast<void **>(device_identity.put())),
      S_OK);
  ComPtr<IUnknown> expected_adapter_identity;
  ASSERT_EQ(context_.adapter()->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(expected_adapter_identity.put())),
            S_OK);

  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kInterfaceCases.size(); ++logical) {
    if (dxmt::test::LogicalCaseSelected(kInterfaceRegistration.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kInterfaceRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const InterfaceCase &test_case = kInterfaceCases[logical];
    ComPtr<IUnknown> versioned;
    const HRESULT interface_result = context_.device()->QueryInterface(
        *test_case.iid, reinterpret_cast<void **>(versioned.put()));
    ComPtr<IUnknown> versioned_identity;
    ComPtr<IDXGIDevice> base_device;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IUnknown> adapter_identity;
    HRESULT identity_result = E_FAIL;
    HRESULT base_result = E_FAIL;
    HRESULT adapter_result = E_FAIL;
    HRESULT adapter_identity_result = E_FAIL;
    if (interface_result == S_OK && versioned) {
      identity_result = versioned->QueryInterface(
          __uuidof(IUnknown),
          reinterpret_cast<void **>(versioned_identity.put()));
      base_result = versioned->QueryInterface(
          __uuidof(IDXGIDevice), reinterpret_cast<void **>(base_device.put()));
      if (base_result == S_OK && base_device) {
        adapter_result = base_device->GetAdapter(adapter.put());
        if (adapter_result == S_OK && adapter) {
          adapter_identity_result = adapter->QueryInterface(
              __uuidof(IUnknown),
              reinterpret_cast<void **>(adapter_identity.put()));
        }
      }
    }

    const bool valid =
        interface_result == S_OK && versioned && identity_result == S_OK &&
        versioned_identity.get() == device_identity.get() &&
        base_result == S_OK && base_device && adapter_result == S_OK &&
        adapter && adapter_identity_result == S_OK && adapter_identity &&
        adapter_identity.get() == expected_adapter_identity.get();
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kInterfaceRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " interface=" << test_case.name
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: interface_hresult=" << S_OK
                  << " identity_hresult=" << S_OK << " base_hresult=" << S_OK
                  << " adapter_hresult=" << S_OK
                  << " device_identity=" << device_identity.get()
                  << " adapter_identity=" << expected_adapter_identity.get()
                  << '\n'
                  << "Observed: interface_hresult=" << interface_result
                  << " identity_hresult=" << identity_result
                  << " base_hresult=" << base_result
                  << " adapter_hresult=" << adapter_result
                  << " adapter_identity_hresult=" << adapter_identity_result
                  << " versioned=" << versioned.get()
                  << " versioned_identity=" << versioned_identity.get()
                  << " adapter=" << adapter.get()
                  << " adapter_identity=" << adapter_identity.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiDeviceSchedulingContractSpec, RoundTripsGpuThreadPriorities) {
  INT initial_priority = std::numeric_limits<INT>::max();
  ASSERT_EQ(dxgi_device1_->GetGPUThreadPriority(&initial_priority), S_OK);
  EXPECT_EQ(initial_priority, 0);

  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kThreadPriorities.size();
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kPriorityRegistration.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kPriorityRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const INT expected = kThreadPriorities[logical];
    const HRESULT set_result = dxgi_device1_->SetGPUThreadPriority(expected);
    INT actual = std::numeric_limits<INT>::max();
    const HRESULT get_result = dxgi_device1_->GetGPUThreadPriority(&actual);
    if (set_result == S_OK && get_result == S_OK && actual == expected)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kPriorityRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " requested_priority=" << expected
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: set_hresult=" << S_OK << " get_hresult=" << S_OK
                  << " observed_priority=" << expected << '\n'
                  << "Observed: set_hresult=" << set_result
                  << " get_hresult=" << get_result
                  << " observed_priority=" << actual << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiDeviceSchedulingContractSpec,
       RejectsInvalidGpuThreadPrioritiesWithoutChangingState) {
  ASSERT_EQ(dxgi_device1_->SetGPUThreadPriority(4), S_OK);
  EXPECT_EQ(dxgi_device1_->SetGPUThreadPriority(-8), E_INVALIDARG);
  EXPECT_EQ(dxgi_device1_->SetGPUThreadPriority(8), E_INVALIDARG);

  INT actual = std::numeric_limits<INT>::max();
  ASSERT_EQ(dxgi_device1_->GetGPUThreadPriority(&actual), S_OK);
  EXPECT_EQ(actual, 4);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiDeviceSchedulingContractSpec, RoundTripsMaximumFrameLatencies) {
  UINT initial_latency = std::numeric_limits<UINT>::max();
  ASSERT_EQ(dxgi_device1_->GetMaximumFrameLatency(&initial_latency), S_OK);
  EXPECT_EQ(initial_latency, 3u);

  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kFrameLatencies.size(); ++logical) {
    if (dxmt::test::LogicalCaseSelected(kLatencyRegistration.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kLatencyRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const UINT expected = kFrameLatencies[logical];
    const HRESULT set_result = dxgi_device1_->SetMaximumFrameLatency(expected);
    UINT actual = std::numeric_limits<UINT>::max();
    const HRESULT get_result = dxgi_device1_->GetMaximumFrameLatency(&actual);
    if (set_result == S_OK && get_result == S_OK && actual == expected)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kLatencyRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " requested_latency=" << expected
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: set_hresult=" << S_OK << " get_hresult=" << S_OK
                  << " observed_latency=" << expected << '\n'
                  << "Observed: set_hresult=" << set_result
                  << " get_hresult=" << get_result
                  << " observed_latency=" << actual << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiDeviceSchedulingContractSpec,
       ResetsDefaultAndRejectsInvalidMaximumFrameLatency) {
  ASSERT_EQ(dxgi_device1_->SetMaximumFrameLatency(16), S_OK);
  EXPECT_EQ(dxgi_device1_->SetMaximumFrameLatency(17), DXGI_ERROR_INVALID_CALL);

  UINT actual = std::numeric_limits<UINT>::max();
  ASSERT_EQ(dxgi_device1_->GetMaximumFrameLatency(&actual), S_OK);
  EXPECT_EQ(actual, 16u);

  ASSERT_EQ(dxgi_device1_->SetMaximumFrameLatency(0), S_OK);
  ASSERT_EQ(dxgi_device1_->GetMaximumFrameLatency(&actual), S_OK);
  EXPECT_TRUE(actual == 0u || actual == 3u);
  EXPECT_EQ(dxgi_device1_->GetMaximumFrameLatency(nullptr),
            DXGI_ERROR_INVALID_CALL);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
