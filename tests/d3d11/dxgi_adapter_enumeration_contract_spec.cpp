#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d10_1.h>
#include <dxgi1_6.h>

#include <array>
#include <cstdint>
#include <vector>

// Public DXGI adapter enumeration coverage. Every operation is read-only and
// uses a test-local factory, so the cases remain safe under parallel workers.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct AdapterInterfaceCase {
  const GUID *iid;
  HRESULT expected;
  const char *name;
};

constexpr std::array kLuidInterfaceCases = {
    AdapterInterfaceCase{&__uuidof(IUnknown), S_OK, "IUnknown"},
    AdapterInterfaceCase{&__uuidof(IDXGIObject), S_OK, "IDXGIObject"},
    AdapterInterfaceCase{&__uuidof(IDXGIAdapter), S_OK, "IDXGIAdapter"},
    AdapterInterfaceCase{&__uuidof(IDXGIAdapter1), S_OK, "IDXGIAdapter1"},
    AdapterInterfaceCase{&__uuidof(IDXGIAdapter2), S_OK, "IDXGIAdapter2"},
    AdapterInterfaceCase{&__uuidof(IDXGIAdapter3), S_OK, "IDXGIAdapter3"},
    AdapterInterfaceCase{&__uuidof(IDXGIAdapter4), S_OK, "IDXGIAdapter4"},
    AdapterInterfaceCase{&__uuidof(ID3D11Device), E_NOINTERFACE,
                         "UnsupportedID3D11Device"},
};

struct PreferenceInterfaceCase {
  DXGI_GPU_PREFERENCE preference;
  const GUID *iid;
  const char *name;
};

constexpr std::array kPreferenceInterfaceCases = {
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_UNSPECIFIED,
                            &__uuidof(IDXGIAdapter), "UnspecifiedAdapter"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_UNSPECIFIED,
                            &__uuidof(IDXGIAdapter1), "UnspecifiedAdapter1"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_UNSPECIFIED,
                            &__uuidof(IDXGIAdapter2), "UnspecifiedAdapter2"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_UNSPECIFIED,
                            &__uuidof(IDXGIAdapter3), "UnspecifiedAdapter3"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_UNSPECIFIED,
                            &__uuidof(IDXGIAdapter4), "UnspecifiedAdapter4"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_MINIMUM_POWER,
                            &__uuidof(IDXGIAdapter), "MinimumPowerAdapter"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_MINIMUM_POWER,
                            &__uuidof(IDXGIAdapter1), "MinimumPowerAdapter1"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_MINIMUM_POWER,
                            &__uuidof(IDXGIAdapter2), "MinimumPowerAdapter2"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_MINIMUM_POWER,
                            &__uuidof(IDXGIAdapter3), "MinimumPowerAdapter3"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_MINIMUM_POWER,
                            &__uuidof(IDXGIAdapter4), "MinimumPowerAdapter4"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                            &__uuidof(IDXGIAdapter), "HighPerformanceAdapter"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                            &__uuidof(IDXGIAdapter1),
                            "HighPerformanceAdapter1"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                            &__uuidof(IDXGIAdapter2),
                            "HighPerformanceAdapter2"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                            &__uuidof(IDXGIAdapter3),
                            "HighPerformanceAdapter3"},
    PreferenceInterfaceCase{DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                            &__uuidof(IDXGIAdapter4),
                            "HighPerformanceAdapter4"},
};

struct InterfaceSupportCase {
  const GUID *iid;
  HRESULT expected;
  const char *name;
};

constexpr std::array kInterfaceSupportCases = {
    InterfaceSupportCase{&__uuidof(IDXGIDevice), S_OK, "IDXGIDevice"},
    InterfaceSupportCase{&__uuidof(ID3D10Device), S_OK, "ID3D10Device"},
    InterfaceSupportCase{&__uuidof(ID3D10Device1), S_OK, "ID3D10Device1"},
    InterfaceSupportCase{&__uuidof(ID3D11Device), DXGI_ERROR_UNSUPPORTED,
                         "ID3D11DeviceUnsupported"},
};

const dxmt::test::LogicalCaseFamilyRegistration kLuidInterfaceRegistration(
    "D3D11DxgiAdapterEnumerationContractSpec."
    "FindsAdapterByLuidForEveryPublicAdapterInterface",
    "D3D11.DXGI.AdapterEnumeration.LuidInterface.", kLuidInterfaceCases.size(),
    1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIFactory4,EnumAdapterByLuid,IDXGIAdapter,IDXGIAdapter1,"
      "IDXGIAdapter2,IDXGIAdapter3,IDXGIAdapter4,QueryInterface"},
     dxmt::test::kResourceTestCost,
     "one test-local factory and adapter per selected logical case",
     "look up the selected adapter LUID using each public adapter IID and one "
     "unsupported device IID",
     "supported interfaces return an equivalent fresh adapter object; the "
     "unsupported interface returns E_NOINTERFACE and clears output",
     "logical ID, interface, HRESULT, returned pointer, descriptor, identity, "
     "and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kPreferenceRegistration(
    "D3D11DxgiAdapterEnumerationContractSpec."
    "EnumeratesGpuPreferencesAcrossAdapterInterfaces",
    "D3D11.DXGI.AdapterEnumeration.PreferenceInterface.",
    kPreferenceInterfaceCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIFactory6,EnumAdapterByGpuPreference,IDXGIFactory4,"
      "EnumAdapterByLuid,IDXGIAdapter,IDXGIAdapter1,IDXGIAdapter2,"
      "IDXGIAdapter3,IDXGIAdapter4"},
     dxmt::test::kResourceTestCost,
     "one test-local factory and transient adapters",
     "enumerate adapter index zero for all three GPU preferences and public "
     "adapter interface versions, then look up each returned LUID",
     "every combination returns a valid adapter whose descriptor is stable "
     "through LUID lookup",
     "logical ID, preference, interface, HRESULTs, descriptors, and exact "
     "replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kInterfaceSupportRegistration(
    "D3D11DxgiAdapterEnumerationContractSpec."
    "ReportsDeviceInterfaceSupportAndVersionWrites",
    "D3D11.DXGI.AdapterEnumeration.InterfaceSupport.",
    kInterfaceSupportCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIAdapter,CheckInterfaceSupport,IDXGIDevice,ID3D10Device,"
      "ID3D10Device1,ID3D11Device"},
     dxmt::test::kResourceTestCost,
     "one test-local DXGI adapter",
     "query supported and unsupported public device interface GUIDs with and "
     "without a UMD version output",
     "supported interfaces write a version; unsupported interfaces leave the "
     "caller value unchanged",
     "logical ID, interface, HRESULTs, version value, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kLuidInterfaceCost("D3D11DxgiAdapterEnumerationContractSpec."
                       "FindsAdapterByLuidForEveryPublicAdapterInterface",
                       dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kPreferenceCost("D3D11DxgiAdapterEnumerationContractSpec."
                    "EnumeratesGpuPreferencesAcrossAdapterInterfaces",
                    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kEnumerationBoundaryCost("D3D11DxgiAdapterEnumerationContractSpec."
                             "EnumerationRejectsNullOutputsAndTerminates",
                             dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kInterfaceSupportCost("D3D11DxgiAdapterEnumerationContractSpec."
                          "ReportsDeviceInterfaceSupportAndVersionWrites",
                          dxmt::test::kResourceTestCost);

bool EqualLuid(const LUID &a, const LUID &b) {
  return a.HighPart == b.HighPart && a.LowPart == b.LowPart;
}

void ExpectSameAdapterDescription(const DXGI_ADAPTER_DESC &actual,
                                  const DXGI_ADAPTER_DESC &expected) {
  EXPECT_STREQ(actual.Description, expected.Description);
  EXPECT_EQ(actual.VendorId, expected.VendorId);
  EXPECT_EQ(actual.DeviceId, expected.DeviceId);
  EXPECT_EQ(actual.SubSysId, expected.SubSysId);
  EXPECT_EQ(actual.Revision, expected.Revision);
  EXPECT_EQ(actual.DedicatedVideoMemory, expected.DedicatedVideoMemory);
  EXPECT_EQ(actual.DedicatedSystemMemory, expected.DedicatedSystemMemory);
  EXPECT_EQ(actual.SharedSystemMemory, expected.SharedSystemMemory);
  EXPECT_TRUE(EqualLuid(actual.AdapterLuid, expected.AdapterLuid));
}

class D3D11DxgiAdapterEnumerationContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.factory()->QueryInterface(
                  __uuidof(IDXGIFactory4),
                  reinterpret_cast<void **>(factory4_.put())),
              S_OK);
    ASSERT_EQ(context_.factory()->QueryInterface(
                  __uuidof(IDXGIFactory6),
                  reinterpret_cast<void **>(factory6_.put())),
              S_OK);
  }

  D3D11TestContext context_;
  ComPtr<IDXGIFactory4> factory4_;
  ComPtr<IDXGIFactory6> factory6_;
};

TEST_F(D3D11DxgiAdapterEnumerationContractSpec,
       FindsAdapterByLuidForEveryPublicAdapterInterface) {
  DXGI_ADAPTER_DESC source_desc = {};
  ASSERT_EQ(context_.adapter()->GetDesc(&source_desc), S_OK);
  ComPtr<IUnknown> source_identity;
  ASSERT_EQ(
      context_.adapter()->QueryInterface(
          __uuidof(IUnknown), reinterpret_cast<void **>(source_identity.put())),
      S_OK);

  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kLuidInterfaceCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kLuidInterfaceRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const auto &test_case = kLuidInterfaceCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kLuidInterfaceRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message() << "LogicalCaseId: " << case_id
                                      << " interface=" << test_case.name
                                      << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<IUnknown> returned;
    const HRESULT hr = factory4_->EnumAdapterByLuid(
        source_desc.AdapterLuid, *test_case.iid,
        reinterpret_cast<void **>(returned.put()));
    EXPECT_EQ(hr, test_case.expected);

    if (test_case.expected == E_NOINTERFACE) {
      EXPECT_EQ(returned.get(), nullptr);
      continue;
    }

    ASSERT_EQ(hr, S_OK);
    ASSERT_TRUE(returned);
    ComPtr<IDXGIAdapter> adapter;
    ASSERT_EQ(
        returned->QueryInterface(__uuidof(IDXGIAdapter),
                                 reinterpret_cast<void **>(adapter.put())),
        S_OK);
    DXGI_ADAPTER_DESC desc = {};
    ASSERT_EQ(adapter->GetDesc(&desc), S_OK);
    ExpectSameAdapterDescription(desc, source_desc);

    ComPtr<IUnknown> returned_identity;
    ASSERT_EQ(returned->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(returned_identity.put())),
              S_OK);
    EXPECT_NE(returned_identity.get(), source_identity.get());
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kLuidInterfaceRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiAdapterEnumerationContractSpec,
       EnumeratesGpuPreferencesAcrossAdapterInterfaces) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kPreferenceInterfaceCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kPreferenceRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const auto &test_case = kPreferenceInterfaceCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kPreferenceRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message() << "LogicalCaseId: " << case_id
                                      << " preference=" << test_case.preference
                                      << " interface=" << test_case.name
                                      << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<IUnknown> preferred;
    ASSERT_EQ(factory6_->EnumAdapterByGpuPreference(
                  0, test_case.preference, *test_case.iid,
                  reinterpret_cast<void **>(preferred.put())),
              S_OK);
    ASSERT_TRUE(preferred);

    ComPtr<IDXGIAdapter> adapter;
    ASSERT_EQ(
        preferred->QueryInterface(__uuidof(IDXGIAdapter),
                                  reinterpret_cast<void **>(adapter.put())),
        S_OK);
    DXGI_ADAPTER_DESC preferred_desc = {};
    ASSERT_EQ(adapter->GetDesc(&preferred_desc), S_OK);
    EXPECT_NE(preferred_desc.Description[0], L'\0');

    ComPtr<IDXGIAdapter> by_luid;
    ASSERT_EQ(factory4_->EnumAdapterByLuid(
                  preferred_desc.AdapterLuid, __uuidof(IDXGIAdapter),
                  reinterpret_cast<void **>(by_luid.put())),
              S_OK);
    DXGI_ADAPTER_DESC luid_desc = {};
    ASSERT_EQ(by_luid->GetDesc(&luid_desc), S_OK);
    ExpectSameAdapterDescription(luid_desc, preferred_desc);
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kPreferenceRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiAdapterEnumerationContractSpec,
       EnumerationRejectsNullOutputsAndTerminates) {
  EXPECT_EQ(context_.factory()->EnumAdapters(0, nullptr),
            DXGI_ERROR_INVALID_CALL);
  EXPECT_EQ(context_.factory()->EnumAdapters1(0, nullptr),
            DXGI_ERROR_INVALID_CALL);

  IDXGIAdapter *legacy =
      reinterpret_cast<IDXGIAdapter *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.factory()->EnumAdapters(UINT_MAX, &legacy),
            DXGI_ERROR_NOT_FOUND);
  EXPECT_EQ(legacy, nullptr);

  IDXGIAdapter1 *versioned =
      reinterpret_cast<IDXGIAdapter1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.factory()->EnumAdapters1(UINT_MAX, &versioned),
            DXGI_ERROR_NOT_FOUND);
  EXPECT_EQ(versioned, nullptr);

  for (const DXGI_GPU_PREFERENCE preference :
       {DXGI_GPU_PREFERENCE_UNSPECIFIED, DXGI_GPU_PREFERENCE_MINIMUM_POWER,
        DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE}) {
    void *output = nullptr;
    EXPECT_EQ(factory6_->EnumAdapterByGpuPreference(
                  UINT_MAX, preference, __uuidof(IDXGIAdapter), &output),
              DXGI_ERROR_NOT_FOUND);
  }

  std::vector<LUID> luids;
  HRESULT terminal = S_OK;
  for (UINT index = 0; index < 64; ++index) {
    ComPtr<IDXGIAdapter1> adapter;
    terminal = context_.factory()->EnumAdapters1(index, adapter.put());
    if (terminal == DXGI_ERROR_NOT_FOUND) {
      EXPECT_EQ(adapter.get(), nullptr);
      break;
    }
    ASSERT_EQ(terminal, S_OK);
    ASSERT_TRUE(adapter);
    DXGI_ADAPTER_DESC1 desc = {};
    ASSERT_EQ(adapter->GetDesc1(&desc), S_OK);
    EXPECT_NE(desc.Description[0], L'\0');
    for (const LUID &seen : luids)
      EXPECT_FALSE(EqualLuid(seen, desc.AdapterLuid));
    luids.push_back(desc.AdapterLuid);
  }
  EXPECT_EQ(terminal, DXGI_ERROR_NOT_FOUND);
  EXPECT_FALSE(luids.empty());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiAdapterEnumerationContractSpec,
       ReportsDeviceInterfaceSupportAndVersionWrites) {
  constexpr LONGLONG kSentinel = 0x13579bdf2468ace0ll;
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kInterfaceSupportCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kInterfaceSupportRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const auto &test_case = kInterfaceSupportCases[logical];
    const auto case_id = dxmt::test::LogicalCaseId(
        kInterfaceSupportRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message() << "LogicalCaseId: " << case_id
                                      << " interface=" << test_case.name
                                      << " Replay: --dxmt-case-id=" << case_id);

    EXPECT_EQ(
        context_.adapter()->CheckInterfaceSupport(*test_case.iid, nullptr),
        test_case.expected);
    LARGE_INTEGER version = {};
    version.QuadPart = kSentinel;
    EXPECT_EQ(
        context_.adapter()->CheckInterfaceSupport(*test_case.iid, &version),
        test_case.expected);
    if (test_case.expected == S_OK)
      EXPECT_NE(version.QuadPart, kSentinel);
    else
      EXPECT_EQ(version.QuadPart, kSentinel);
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kInterfaceSupportRegistration.family().case_id_prefix);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
