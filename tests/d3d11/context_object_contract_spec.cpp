#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 device-context COM coverage. Immediate and deferred contexts
// belong to a test-local device, and every mutable private-data entry uses a
// test-only GUID, so the cases are safe under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr GUID kContextPrivateDataKey = {
    0x02c76d21,
    0xd241,
    0x4c92,
    {0xb9, 0x4c, 0x55, 0x58, 0x8b, 0x94, 0xa7, 0x31}};

struct ContextInterfaceCase {
  bool deferred;
  const GUID *iid;
  const char *name;
};

constexpr std::array kContextInterfaceCases = {
    ContextInterfaceCase{false, &__uuidof(IUnknown), "ImmediateIUnknown"},
    ContextInterfaceCase{false, &__uuidof(ID3D11DeviceChild),
                         "ImmediateDeviceChild"},
    ContextInterfaceCase{false, &__uuidof(ID3D11DeviceContext),
                         "ImmediateContext"},
    ContextInterfaceCase{false, &__uuidof(ID3D11DeviceContext1),
                         "ImmediateContext1"},
    ContextInterfaceCase{false, &__uuidof(ID3D11DeviceContext2),
                         "ImmediateContext2"},
    ContextInterfaceCase{false, &__uuidof(ID3D11DeviceContext3),
                         "ImmediateContext3"},
    ContextInterfaceCase{false, &__uuidof(ID3D11DeviceContext4),
                         "ImmediateContext4"},
    ContextInterfaceCase{true, &__uuidof(IUnknown), "DeferredIUnknown"},
    ContextInterfaceCase{true, &__uuidof(ID3D11DeviceChild),
                         "DeferredDeviceChild"},
    ContextInterfaceCase{true, &__uuidof(ID3D11DeviceContext),
                         "DeferredContext"},
    ContextInterfaceCase{true, &__uuidof(ID3D11DeviceContext1),
                         "DeferredContext1"},
    ContextInterfaceCase{true, &__uuidof(ID3D11DeviceContext2),
                         "DeferredContext2"},
    ContextInterfaceCase{true, &__uuidof(ID3D11DeviceContext3),
                         "DeferredContext3"},
    ContextInterfaceCase{true, &__uuidof(ID3D11DeviceContext4),
                         "DeferredContext4"},
};

const dxmt::test::LogicalCaseFamilyRegistration kContextInterfaceRegistration(
    "D3D11ContextObjectContractSpec.ExposesOneIdentityAcrossContextVersions",
    "D3D11.Context.Object.Interface.", kContextInterfaceCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IUnknown,ID3D11DeviceChild,ID3D11DeviceContext,"
      "ID3D11DeviceContext1,ID3D11DeviceContext2,ID3D11DeviceContext3,"
      "ID3D11DeviceContext4,QueryInterface,ComObjectIdentity,GetDevice,"
      "GetType,GetContextFlags"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, its immediate context, and one deferred "
     "context",
     "query every public device-context interface from IUnknown and "
     "ID3D11DeviceChild through ID3D11DeviceContext4 on immediate and "
     "deferred contexts",
     "every interface preserves the context's canonical COM identity, owner, "
     "context type, and zero creation flags",
     "logical ID, selected-case count, context kind, interface name, "
     "HRESULTs, identities, owner, type, flags, and exact replay argument"});

const dxmt::test::TestCostRegistration kContextInterfaceCost(
    "D3D11ContextObjectContractSpec.ExposesOneIdentityAcrossContextVersions",
    dxmt::test::kResourceTestCost);

struct HardwareProtectionCase {
  bool deferred;
  const char *name;
};

constexpr std::array kHardwareProtectionCases = {
    HardwareProtectionCase{false, "Immediate"},
    HardwareProtectionCase{true, "Deferred"},
};

const dxmt::test::LogicalCaseFamilyRegistration kHardwareProtectionRegistration(
    "D3D11ContextObjectContractSpec.RoundTripsHardwareProtectionState",
    "D3D11.Context.HardwareProtection.State.", kHardwareProtectionCases.size(),
    1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_3", "None", "Device",
      "ID3D11DeviceContext3,SetHardwareProtectionState,"
      "GetHardwareProtectionState"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, its immediate context, and one deferred "
     "context",
     "read the default hardware-protection state, enable it on one "
     "context, verify the other context is unchanged, then disable it",
     "immediate and deferred contexts each default to disabled, retain "
     "their own enabled state, and return to disabled independently",
     "logical ID, selected-case count, context kind, each observed state, "
     "failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration kHardwareProtectionCost(
    "D3D11ContextObjectContractSpec.RoundTripsHardwareProtectionState",
    dxmt::test::kResourceTestCost);

class D3D11ContextObjectContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
    ASSERT_EQ(context_.device()->CreateDeferredContext(0, deferred_.put()),
              S_OK);
    ASSERT_NE(deferred_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11DeviceContext> deferred_;
};

TEST_F(D3D11ContextObjectContractSpec,
       ExposesOneIdentityAcrossContextVersions) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kContextInterfaceCases.size();
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kContextInterfaceRegistration.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kContextInterfaceRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const ContextInterfaceCase &test_case = kContextInterfaceCases[logical];
    ID3D11DeviceContext *base =
        test_case.deferred ? deferred_.get() : context_.context();
    ComPtr<IUnknown> canonical_identity;
    ASSERT_EQ(base->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(canonical_identity.put())),
              S_OK);

    ComPtr<IUnknown> versioned;
    const HRESULT interface_result = base->QueryInterface(
        *test_case.iid, reinterpret_cast<void **>(versioned.put()));
    ComPtr<IUnknown> versioned_identity;
    ComPtr<ID3D11DeviceContext> returned_base;
    ComPtr<ID3D11DeviceChild> child;
    ComPtr<ID3D11Device> owner;
    HRESULT identity_result = E_FAIL;
    HRESULT base_result = E_FAIL;
    HRESULT child_result = E_FAIL;
    if (interface_result == S_OK && versioned) {
      identity_result = versioned->QueryInterface(
          __uuidof(IUnknown),
          reinterpret_cast<void **>(versioned_identity.put()));
      base_result = versioned->QueryInterface(
          __uuidof(ID3D11DeviceContext),
          reinterpret_cast<void **>(returned_base.put()));
      child_result = versioned->QueryInterface(
          __uuidof(ID3D11DeviceChild), reinterpret_cast<void **>(child.put()));
      if (child_result == S_OK && child)
        child->GetDevice(owner.put());
    }

    const D3D11_DEVICE_CONTEXT_TYPE expected_type =
        test_case.deferred ? D3D11_DEVICE_CONTEXT_DEFERRED
                           : D3D11_DEVICE_CONTEXT_IMMEDIATE;
    const bool valid =
        interface_result == S_OK && versioned && identity_result == S_OK &&
        versioned_identity.get() == canonical_identity.get() &&
        base_result == S_OK && returned_base.get() == base &&
        child_result == S_OK && child && owner.get() == context_.device() &&
        returned_base->GetType() == expected_type &&
        returned_base->GetContextFlags() == 0;
    if (valid)
      continue;

    const auto case_id = dxmt::test::LogicalCaseId(
        kContextInterfaceRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " interface=" << test_case.name
                  << " deferred=" << test_case.deferred
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: interface_hresult=" << S_OK
                  << " identity_hresult=" << S_OK << " base_hresult=" << S_OK
                  << " child_hresult=" << S_OK
                  << " identity=" << canonical_identity.get()
                  << " base=" << base << " owner=" << context_.device()
                  << " type=" << expected_type << " flags=0\n"
                  << "Observed: interface_hresult=" << interface_result
                  << " identity_hresult=" << identity_result
                  << " base_hresult=" << base_result
                  << " child_hresult=" << child_result
                  << " versioned=" << versioned.get()
                  << " versioned_identity=" << versioned_identity.get()
                  << " returned_base=" << returned_base.get()
                  << " child=" << child.get() << " owner=" << owner.get()
                  << " type="
                  << (returned_base ? returned_base->GetType()
                                    : D3D11_DEVICE_CONTEXT_TYPE(-1))
                  << " flags="
                  << (returned_base ? returned_base->GetContextFlags() : ~0u)
                  << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ContextObjectContractSpec,
       RejectsUnsupportedInterfacesAndNullOutputs) {
  for (ID3D11DeviceContext *context : {context_.context(), deferred_.get()}) {
    void *output = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
    EXPECT_EQ(context->QueryInterface(__uuidof(ID3D11Buffer), &output),
              E_NOINTERFACE);
    EXPECT_EQ(output, nullptr);
    EXPECT_EQ(context->QueryInterface(__uuidof(IUnknown), nullptr), E_POINTER);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ContextObjectContractSpec,
       KeepsImmediateAndDeferredPrivateDataIndependent) {
  constexpr std::array<std::uint8_t, 5> kImmediateValue = {1, 3, 5, 7, 9};
  constexpr std::array<std::uint8_t, 7> kDeferredValue = {2,  4,  6, 8,
                                                          10, 12, 14};
  ASSERT_EQ(context_.context()->SetPrivateData(kContextPrivateDataKey,
                                               kImmediateValue.size(),
                                               kImmediateValue.data()),
            S_OK);
  ASSERT_EQ(deferred_->SetPrivateData(kContextPrivateDataKey,
                                      kDeferredValue.size(),
                                      kDeferredValue.data()),
            S_OK);

  std::array<std::uint8_t, kImmediateValue.size()> immediate = {};
  UINT immediate_size = immediate.size();
  ASSERT_EQ(context_.context()->GetPrivateData(
                kContextPrivateDataKey, &immediate_size, immediate.data()),
            S_OK);
  EXPECT_EQ(immediate_size, kImmediateValue.size());
  EXPECT_EQ(immediate, kImmediateValue);

  std::array<std::uint8_t, kDeferredValue.size()> deferred = {};
  UINT deferred_size = deferred.size();
  ASSERT_EQ(deferred_->GetPrivateData(kContextPrivateDataKey, &deferred_size,
                                      deferred.data()),
            S_OK);
  EXPECT_EQ(deferred_size, kDeferredValue.size());
  EXPECT_EQ(deferred, kDeferredValue);

  EXPECT_EQ(
      context_.context()->SetPrivateData(kContextPrivateDataKey, 0, nullptr),
      S_OK);
  immediate_size = immediate.size();
  EXPECT_EQ(context_.context()->GetPrivateData(
                kContextPrivateDataKey, &immediate_size, immediate.data()),
            DXGI_ERROR_NOT_FOUND);
  deferred_size = deferred.size();
  EXPECT_EQ(deferred_->GetPrivateData(kContextPrivateDataKey, &deferred_size,
                                      deferred.data()),
            S_OK);
  EXPECT_EQ(deferred, kDeferredValue);
  EXPECT_EQ(deferred_->SetPrivateData(kContextPrivateDataKey, 0, nullptr),
            S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ContextObjectContractSpec, RoundTripsHardwareProtectionState) {
  ComPtr<ID3D11DeviceContext3> immediate3;
  ComPtr<ID3D11DeviceContext3> deferred3;
  ASSERT_EQ(context_.context()->QueryInterface(
                __uuidof(ID3D11DeviceContext3),
                reinterpret_cast<void **>(immediate3.put())),
            S_OK);
  ASSERT_EQ(
      deferred_->QueryInterface(__uuidof(ID3D11DeviceContext3),
                                reinterpret_cast<void **>(deferred3.put())),
      S_OK);

  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kHardwareProtectionCases.size();
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(
            kHardwareProtectionRegistration.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kHardwareProtectionRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const HardwareProtectionCase &test_case = kHardwareProtectionCases[logical];
    ID3D11DeviceContext3 *selected =
        test_case.deferred ? deferred3.get() : immediate3.get();
    ID3D11DeviceContext3 *other =
        test_case.deferred ? immediate3.get() : deferred3.get();

    immediate3->SetHardwareProtectionState(FALSE);
    deferred3->SetHardwareProtectionState(FALSE);
    WINBOOL initial = TRUE;
    WINBOOL enabled = FALSE;
    WINBOOL other_state = TRUE;
    WINBOOL disabled = TRUE;
    selected->GetHardwareProtectionState(&initial);
    selected->SetHardwareProtectionState(TRUE);
    selected->GetHardwareProtectionState(&enabled);
    other->GetHardwareProtectionState(&other_state);
    selected->SetHardwareProtectionState(FALSE);
    selected->GetHardwareProtectionState(&disabled);

    const bool valid = initial == FALSE && enabled == TRUE &&
                       other_state == FALSE && disabled == FALSE;
    if (valid)
      continue;

    const auto case_id = dxmt::test::LogicalCaseId(
        kHardwareProtectionRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " context=" << test_case.name
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: initial=0 enabled=1 other=0 disabled=0\n"
                  << "Observed: initial=" << initial << " enabled=" << enabled
                  << " other=" << other_state << " disabled=" << disabled
                  << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
