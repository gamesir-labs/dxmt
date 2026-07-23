#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

// Public D3D11 immediate-context retrieval coverage across every device
// interface version implemented by DXMT. All returned context interfaces must
// resolve to the same immediate-context COM object.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::array<const char *, 4> kDeviceVersionNames = {
    "ID3D11Device",
    "ID3D11Device1",
    "ID3D11Device2",
    "ID3D11Device3",
};

constexpr std::uint32_t kDeviceVersionCount = kDeviceVersionNames.size();

const dxmt::test::LogicalCaseFamilyRegistration kImmediateContextCases(
    "D3D11ImmediateContextVersionContractSpec."
    "ReturnsOneContextAcrossDeviceVersions",
    "D3D11.ImmediateContext.Get.Version.", kDeviceVersionCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "GetImmediateContext,GetImmediateContext1,GetImmediateContext2,"
      "GetImmediateContext3,ID3D11DeviceContextGetType,"
      "ID3D11DeviceContextGetContextFlags,ID3D11DeviceChildGetDevice,"
      "QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and two live references to its immediate "
     "context per selected logical case",
     "retrieve the immediate context through ID3D11Device, Device1, Device2, "
     "and Device3, then query the returned version back to the base context",
     "every getter returns the same immediate-context COM identity with zero "
     "context flags and the creating device",
     "logical ID, selected-case count, device version, retrieval and base-QI "
     "HRESULTs, context type and flags, canonical and returned identities, "
     "owner address, failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kImmediateContextCost("D3D11ImmediateContextVersionContractSpec."
                          "ReturnsOneContextAcrossDeviceVersions",
                          dxmt::test::kResourceTestCost);

HRESULT GetImmediateContextForVersion(ID3D11Device *device,
                                      std::uint32_t version,
                                      ID3D11DeviceContext **base_context,
                                      IUnknown **versioned_identity) {
  if (!device || !base_context || !versioned_identity ||
      version >= kDeviceVersionCount)
    return E_INVALIDARG;
  *base_context = nullptr;
  *versioned_identity = nullptr;

  if (version == 0) {
    device->GetImmediateContext(base_context);
    if (!*base_context)
      return E_FAIL;
    return (*base_context)
        ->QueryInterface(__uuidof(IUnknown),
                         reinterpret_cast<void **>(versioned_identity));
  }

  if (version == 1) {
    ComPtr<ID3D11Device1> device1;
    HRESULT hr = device->QueryInterface(
        __uuidof(ID3D11Device1), reinterpret_cast<void **>(device1.put()));
    if (FAILED(hr))
      return hr;
    ComPtr<ID3D11DeviceContext1> context1;
    device1->GetImmediateContext1(context1.put());
    if (!context1)
      return E_FAIL;
    hr = context1->QueryInterface(
        __uuidof(IUnknown), reinterpret_cast<void **>(versioned_identity));
    if (FAILED(hr))
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
    device2->GetImmediateContext2(context2.put());
    if (!context2)
      return E_FAIL;
    hr = context2->QueryInterface(
        __uuidof(IUnknown), reinterpret_cast<void **>(versioned_identity));
    if (FAILED(hr))
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
  device3->GetImmediateContext3(context3.put());
  if (!context3)
    return E_FAIL;
  hr = context3->QueryInterface(__uuidof(IUnknown),
                                reinterpret_cast<void **>(versioned_identity));
  if (FAILED(hr))
    return hr;
  return context3->QueryInterface(__uuidof(ID3D11DeviceContext),
                                  reinterpret_cast<void **>(base_context));
}

class D3D11ImmediateContextVersionContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
    ASSERT_EQ(context_.context()->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(canonical_identity_.put())),
              S_OK);
    ASSERT_NE(canonical_identity_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<IUnknown> canonical_identity_;
};

TEST_F(D3D11ImmediateContextVersionContractSpec,
       ReturnsOneContextAcrossDeviceVersions) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kDeviceVersionCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kImmediateContextCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kImmediateContextCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    ComPtr<ID3D11DeviceContext> returned_context;
    ComPtr<IUnknown> versioned_identity;
    const HRESULT retrieve_result = GetImmediateContextForVersion(
        context_.device(), logical, returned_context.put(),
        versioned_identity.put());
    ComPtr<IUnknown> returned_identity;
    ComPtr<ID3D11Device> owner;
    D3D11_DEVICE_CONTEXT_TYPE context_type =
        static_cast<D3D11_DEVICE_CONTEXT_TYPE>(
            std::numeric_limits<UINT>::max());
    UINT context_flags = std::numeric_limits<UINT>::max();
    HRESULT identity_result = E_FAIL;
    if (retrieve_result == S_OK && returned_context) {
      identity_result = returned_context->QueryInterface(
          __uuidof(IUnknown),
          reinterpret_cast<void **>(returned_identity.put()));
      returned_context->GetDevice(owner.put());
      context_type = returned_context->GetType();
      context_flags = returned_context->GetContextFlags();
    }

    const bool valid =
        retrieve_result == S_OK && returned_context && versioned_identity &&
        identity_result == S_OK && returned_identity &&
        versioned_identity.get() == canonical_identity_.get() &&
        returned_identity.get() == canonical_identity_.get() &&
        owner.get() == context_.device() &&
        context_type == D3D11_DEVICE_CONTEXT_IMMEDIATE && context_flags == 0;
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kImmediateContextCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kImmediateContextCases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " device_version=" << kDeviceVersionNames[logical]
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: retrieve_hresult=" << S_OK
                  << " identity_hresult=" << S_OK
                  << " context_type=" << D3D11_DEVICE_CONTEXT_IMMEDIATE
                  << " context_flags=0 identity=" << canonical_identity_.get()
                  << " owner=" << context_.device() << '\n'
                  << "Observed: retrieve_hresult=" << retrieve_result
                  << " identity_hresult=" << identity_result
                  << " context_type=" << static_cast<UINT>(context_type)
                  << " context_flags=" << context_flags
                  << " returned_context=" << returned_context.get()
                  << " versioned_identity=" << versioned_identity.get()
                  << " returned_identity=" << returned_identity.get()
                  << " owner=" << owner.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
