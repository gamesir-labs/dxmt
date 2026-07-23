#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 deferred-context creation coverage across ID3D11Device through
// ID3D11Device3. Every case owns its device and context locally, so the cases
// remain safe under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kDeviceVersionCount = 4;
constexpr std::array<const char *, kDeviceVersionCount> kDeviceVersionNames = {
    "ID3D11Device",
    "ID3D11Device1",
    "ID3D11Device2",
    "ID3D11Device3",
};

const dxmt::test::LogicalCaseFamilyRegistration kDeferredCreationCases(
    "D3D11DeferredContextCreationContractSpec."
    "CreatesAcrossDeviceInterfaceVersions",
    "D3D11.DeferredContext.Create.Version.", kDeviceVersionCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateDeferredContext,CreateDeferredContext1,CreateDeferredContext2,"
      "CreateDeferredContext3,ID3D11DeviceContextGetType,"
      "ID3D11DeviceContextGetContextFlags,ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live deferred context per selected "
     "logical case",
     "create a deferred context through each public device interface version "
     "from ID3D11Device through ID3D11Device3",
     "every version returns a deferred context with zero context flags and the "
     "creating device identity",
     "logical ID, selected-case count, interface version, HRESULT, context "
     "type and flags, owner address, failure phase, and exact replay "
     "argument"});

const dxmt::test::LogicalCaseFamilyRegistration kDeferredInvalidFlagCases(
    "D3D11DeferredContextCreationContractSpec."
    "RejectsReservedFlagsAcrossDeviceInterfaceVersions",
    "D3D11.DeferredContext.InvalidFlags.Version.", kDeviceVersionCount, 1,
    {dxmt::test::TestClass::Robustness,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateDeferredContext,CreateDeferredContext1,CreateDeferredContext2,"
      "CreateDeferredContext3"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and at most one rejected deferred-context "
     "creation per selected logical case",
     "pass a nonzero reserved ContextFlags value through each public device "
     "interface version",
     "every version returns E_INVALIDARG without producing a context",
     "logical ID, selected-case count, interface version, HRESULT, output "
     "address, and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kSingleThreadedCases(
    "D3D11DeferredContextCreationContractSpec."
    "RejectsCreationOnSingleThreadedDeviceAcrossVersions",
    "D3D11.DeferredContext.SingleThreaded.Version.", kDeviceVersionCount, 1,
    {dxmt::test::TestClass::Robustness,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "D3D11CreateDevice,D3D11_CREATE_DEVICE_SINGLETHREADED,"
      "CreateDeferredContext,CreateDeferredContext1,CreateDeferredContext2,"
      "CreateDeferredContext3"},
     dxmt::test::kResourceTestCost,
     "one test-local single-threaded D3D11 device and at most one rejected "
     "deferred-context creation per selected logical case",
     "create a device with D3D11_CREATE_DEVICE_SINGLETHREADED and request a "
     "deferred context through each public device interface version",
     "every version returns DXGI_ERROR_INVALID_CALL without producing a "
     "context",
     "logical ID, selected-case count, interface version, HRESULT, output "
     "address, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kDeferredCreationCost("D3D11DeferredContextCreationContractSpec."
                          "CreatesAcrossDeviceInterfaceVersions",
                          dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kDeferredInvalidFlagCost(
    "D3D11DeferredContextCreationContractSpec."
    "RejectsReservedFlagsAcrossDeviceInterfaceVersions",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kSingleThreadedCost("D3D11DeferredContextCreationContractSpec."
                        "RejectsCreationOnSingleThreadedDeviceAcrossVersions",
                        dxmt::test::kResourceTestCost);

HRESULT CreateDeferredForVersion(ID3D11Device *device, std::uint32_t version,
                                 UINT flags,
                                 ID3D11DeviceContext **base_context) {
  if (!device || !base_context || version >= kDeviceVersionCount)
    return E_INVALIDARG;
  *base_context = nullptr;

  if (version == 0)
    return device->CreateDeferredContext(flags, base_context);

  if (version == 1) {
    ComPtr<ID3D11Device1> device1;
    HRESULT hr = device->QueryInterface(
        __uuidof(ID3D11Device1), reinterpret_cast<void **>(device1.put()));
    if (FAILED(hr))
      return hr;
    ComPtr<ID3D11DeviceContext1> context1;
    hr = device1->CreateDeferredContext1(flags, context1.put());
    if (FAILED(hr) || !context1)
      return hr;
    return context1->QueryInterface(__uuidof(ID3D11DeviceContext),
                                    reinterpret_cast<void **>(base_context));
  }

  if (version == 2) {
    ComPtr<ID3D11Device2> device2;
    HRESULT hr = device->QueryInterface(
        __uuidof(ID3D11Device2), reinterpret_cast<void **>(device2.put()));
    if (FAILED(hr))
      return hr;
    ComPtr<ID3D11DeviceContext2> context2;
    hr = device2->CreateDeferredContext2(flags, context2.put());
    if (FAILED(hr) || !context2)
      return hr;
    return context2->QueryInterface(__uuidof(ID3D11DeviceContext),
                                    reinterpret_cast<void **>(base_context));
  }

  ComPtr<ID3D11Device3> device3;
  HRESULT hr = device->QueryInterface(__uuidof(ID3D11Device3),
                                      reinterpret_cast<void **>(device3.put()));
  if (FAILED(hr))
    return hr;
  ComPtr<ID3D11DeviceContext3> context3;
  hr = device3->CreateDeferredContext3(flags, context3.put());
  if (FAILED(hr) || !context3)
    return hr;
  return context3->QueryInterface(__uuidof(ID3D11DeviceContext),
                                  reinterpret_cast<void **>(base_context));
}

class D3D11DeferredContextCreationContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.adapter(), nullptr);
    ASSERT_NE(context_.device(), nullptr);
  }

  ComPtr<ID3D11Device> CreateSingleThreadedDevice() {
    constexpr D3D_FEATURE_LEVEL requested_level = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL actual_level = D3D_FEATURE_LEVEL(0);
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate_context;
    EXPECT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                                nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED,
                                &requested_level, 1, D3D11_SDK_VERSION,
                                device.put(), &actual_level,
                                immediate_context.put()),
              S_OK);
    EXPECT_EQ(actual_level, requested_level);
    EXPECT_NE(device.get(), nullptr);
    EXPECT_NE(immediate_context.get(), nullptr);
    return device;
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DeferredContextCreationContractSpec,
       CreatesAcrossDeviceInterfaceVersions) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kDeviceVersionCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kDeferredCreationCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kDeferredCreationCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    ComPtr<ID3D11DeviceContext> deferred;
    const HRESULT create_result =
        CreateDeferredForVersion(context_.device(), logical, 0, deferred.put());
    D3D11_DEVICE_CONTEXT_TYPE context_type = D3D11_DEVICE_CONTEXT_IMMEDIATE;
    UINT context_flags = ~0u;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && deferred) {
      context_type = deferred->GetType();
      context_flags = deferred->GetContextFlags();
      deferred->GetDevice(owner.put());
    }

    const bool valid = create_result == S_OK && deferred &&
                       context_type == D3D11_DEVICE_CONTEXT_DEFERRED &&
                       context_flags == 0 && owner.get() == context_.device();
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kDeferredCreationCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kDeferredCreationCases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " interface=" << kDeviceVersionNames[logical]
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: hresult=" << S_OK << " type="
                  << static_cast<UINT>(D3D11_DEVICE_CONTEXT_DEFERRED)
                  << " flags=0 owner=" << context_.device() << '\n'
                  << "Observed: hresult=" << create_result
                  << " type=" << static_cast<UINT>(context_type)
                  << " flags=" << context_flags << " owner=" << owner.get()
                  << " context=" << deferred.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DeferredContextCreationContractSpec,
       RejectsReservedFlagsAcrossDeviceInterfaceVersions) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kDeviceVersionCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kDeferredInvalidFlagCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kDeferredInvalidFlagCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    ComPtr<ID3D11DeviceContext> deferred;
    const HRESULT create_result =
        CreateDeferredForVersion(context_.device(), logical, 1, deferred.put());
    const auto case_id =
        dxmt::test::LogicalCaseId(kDeferredInvalidFlagCases.family(), logical);
    EXPECT_EQ(create_result, E_INVALIDARG)
        << "LogicalCaseId: " << case_id
        << " interface=" << kDeviceVersionNames[logical]
        << " output=" << deferred.get() << " replay=--dxmt-case-id=" << case_id;
    EXPECT_EQ(deferred.get(), nullptr)
        << "LogicalCaseId: " << case_id
        << " interface=" << kDeviceVersionNames[logical];
  }
}

TEST_F(D3D11DeferredContextCreationContractSpec,
       RejectsCreationOnSingleThreadedDeviceAcrossVersions) {
  const ComPtr<ID3D11Device> single_threaded = CreateSingleThreadedDevice();
  ASSERT_NE(single_threaded.get(), nullptr);

  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kDeviceVersionCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kSingleThreadedCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kSingleThreadedCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    ComPtr<ID3D11DeviceContext> deferred;
    const HRESULT create_result = CreateDeferredForVersion(
        single_threaded.get(), logical, 0, deferred.put());
    const auto case_id =
        dxmt::test::LogicalCaseId(kSingleThreadedCases.family(), logical);
    EXPECT_EQ(create_result, DXGI_ERROR_INVALID_CALL)
        << "LogicalCaseId: " << case_id
        << " interface=" << kDeviceVersionNames[logical]
        << " output=" << deferred.get() << " replay=--dxmt-case-id=" << case_id;
    EXPECT_EQ(deferred.get(), nullptr)
        << "LogicalCaseId: " << case_id
        << " interface=" << kDeviceVersionNames[logical];
  }
}

} // namespace
