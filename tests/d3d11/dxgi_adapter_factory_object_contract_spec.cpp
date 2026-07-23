#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_6.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Public DXGI factory and adapter object coverage reached through the D3D11
// creation chain. The objects and private-data entries are test-local, and no
// output, window, or mutable process-global DXGI state is used.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr GUID kObjectPrivateDataKey = {
    0x35c85a19,
    0x5d2f,
    0x4e93,
    {0x81, 0x8f, 0xb1, 0xcd, 0x8f, 0xa1, 0x4b, 0x72}};

enum class ObjectKind {
  Factory,
  Adapter,
};

struct ObjectInterfaceCase {
  ObjectKind kind;
  const GUID *iid;
  const char *name;
};

constexpr std::array kObjectInterfaceCases = {
    ObjectInterfaceCase{ObjectKind::Factory, &__uuidof(IUnknown),
                        "FactoryIUnknown"},
    ObjectInterfaceCase{ObjectKind::Factory, &__uuidof(IDXGIObject),
                        "FactoryObject"},
    ObjectInterfaceCase{ObjectKind::Factory, &__uuidof(IDXGIFactory),
                        "Factory"},
    ObjectInterfaceCase{ObjectKind::Factory, &__uuidof(IDXGIFactory1),
                        "Factory1"},
    ObjectInterfaceCase{ObjectKind::Factory, &__uuidof(IDXGIFactory2),
                        "Factory2"},
    ObjectInterfaceCase{ObjectKind::Factory, &__uuidof(IDXGIFactory3),
                        "Factory3"},
    ObjectInterfaceCase{ObjectKind::Factory, &__uuidof(IDXGIFactory4),
                        "Factory4"},
    ObjectInterfaceCase{ObjectKind::Factory, &__uuidof(IDXGIFactory5),
                        "Factory5"},
    ObjectInterfaceCase{ObjectKind::Factory, &__uuidof(IDXGIFactory6),
                        "Factory6"},
    ObjectInterfaceCase{ObjectKind::Adapter, &__uuidof(IUnknown),
                        "AdapterIUnknown"},
    ObjectInterfaceCase{ObjectKind::Adapter, &__uuidof(IDXGIObject),
                        "AdapterObject"},
    ObjectInterfaceCase{ObjectKind::Adapter, &__uuidof(IDXGIAdapter),
                        "Adapter"},
    ObjectInterfaceCase{ObjectKind::Adapter, &__uuidof(IDXGIAdapter1),
                        "Adapter1"},
    ObjectInterfaceCase{ObjectKind::Adapter, &__uuidof(IDXGIAdapter2),
                        "Adapter2"},
    ObjectInterfaceCase{ObjectKind::Adapter, &__uuidof(IDXGIAdapter3),
                        "Adapter3"},
    ObjectInterfaceCase{ObjectKind::Adapter, &__uuidof(IDXGIAdapter4),
                        "Adapter4"},
};

const dxmt::test::LogicalCaseFamilyRegistration kObjectInterfaceRegistration(
    "D3D11DxgiAdapterFactoryObjectContractSpec."
    "ExposesOneIdentityAcrossInterfaceVersions",
    "D3D11.DXGI.AdapterFactory.Interface.", kObjectInterfaceCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateDXGIFactory1,IDXGIFactory,IDXGIFactory1,IDXGIFactory2,"
      "IDXGIFactory3,IDXGIFactory4,IDXGIFactory5,IDXGIFactory6,"
      "IDXGIAdapter,IDXGIAdapter1,IDXGIAdapter2,IDXGIAdapter3,"
      "IDXGIAdapter4,IDXGIObject,QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local DXGI factory and its selected D3D11 adapter",
     "query every public factory interface through IDXGIFactory6 and every "
     "public adapter interface through IDXGIAdapter4",
     "each interface queries back to its base type and preserves the source "
     "object's canonical IUnknown identity",
     "logical ID, selected-case count, object kind, interface name, HRESULTs, "
     "identities, base pointers, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kObjectInterfaceCost("D3D11DxgiAdapterFactoryObjectContractSpec."
                         "ExposesOneIdentityAcrossInterfaceVersions",
                         dxmt::test::kResourceTestCost);

class D3D11DxgiAdapterFactoryObjectContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.factory(), nullptr);
    ASSERT_NE(context_.adapter(), nullptr);
    ASSERT_NE(context_.device(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DxgiAdapterFactoryObjectContractSpec,
       ExposesOneIdentityAcrossInterfaceVersions) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kObjectInterfaceCases.size();
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kObjectInterfaceRegistration.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kObjectInterfaceRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const ObjectInterfaceCase &test_case = kObjectInterfaceCases[logical];
    IUnknown *source = test_case.kind == ObjectKind::Factory
                           ? static_cast<IUnknown *>(context_.factory())
                           : static_cast<IUnknown *>(context_.adapter());
    ComPtr<IUnknown> canonical_identity;
    ASSERT_EQ(source->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(canonical_identity.put())),
              S_OK);

    ComPtr<IUnknown> versioned;
    const HRESULT interface_result = source->QueryInterface(
        *test_case.iid, reinterpret_cast<void **>(versioned.put()));
    ComPtr<IUnknown> versioned_identity;
    ComPtr<IUnknown> returned_base;
    HRESULT identity_result = E_FAIL;
    HRESULT base_result = E_FAIL;
    if (interface_result == S_OK && versioned) {
      identity_result = versioned->QueryInterface(
          __uuidof(IUnknown),
          reinterpret_cast<void **>(versioned_identity.put()));
      const GUID &base_iid = test_case.kind == ObjectKind::Factory
                                 ? __uuidof(IDXGIFactory)
                                 : __uuidof(IDXGIAdapter);
      base_result = versioned->QueryInterface(
          base_iid, reinterpret_cast<void **>(returned_base.put()));
    }

    ComPtr<IUnknown> returned_base_identity;
    HRESULT base_identity_result = E_FAIL;
    if (base_result == S_OK && returned_base) {
      base_identity_result = returned_base->QueryInterface(
          __uuidof(IUnknown),
          reinterpret_cast<void **>(returned_base_identity.put()));
    }
    const bool valid =
        interface_result == S_OK && versioned && identity_result == S_OK &&
        versioned_identity.get() == canonical_identity.get() &&
        base_result == S_OK && returned_base && base_identity_result == S_OK &&
        returned_base_identity.get() == canonical_identity.get();
    if (valid)
      continue;

    const auto case_id = dxmt::test::LogicalCaseId(
        kObjectInterfaceRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " interface=" << test_case.name
                  << " kind=" << static_cast<UINT>(test_case.kind)
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: interface_hresult=" << S_OK
                  << " identity_hresult=" << S_OK << " base_hresult=" << S_OK
                  << " identity=" << canonical_identity.get() << '\n'
                  << "Observed: interface_hresult=" << interface_result
                  << " identity_hresult=" << identity_result
                  << " base_hresult=" << base_result
                  << " base_identity_hresult=" << base_identity_result
                  << " versioned=" << versioned.get()
                  << " versioned_identity=" << versioned_identity.get()
                  << " returned_base=" << returned_base.get()
                  << " returned_base_identity=" << returned_base_identity.get()
                  << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiAdapterFactoryObjectContractSpec,
       KeepsAdapterDescriptionsConsistentAcrossVersions) {
  ComPtr<IDXGIAdapter1> adapter1;
  ComPtr<IDXGIAdapter2> adapter2;
  ComPtr<IDXGIAdapter4> adapter4;
  ASSERT_EQ(
      context_.adapter()->QueryInterface(
          __uuidof(IDXGIAdapter1), reinterpret_cast<void **>(adapter1.put())),
      S_OK);
  ASSERT_EQ(
      context_.adapter()->QueryInterface(
          __uuidof(IDXGIAdapter2), reinterpret_cast<void **>(adapter2.put())),
      S_OK);
  ASSERT_EQ(
      context_.adapter()->QueryInterface(
          __uuidof(IDXGIAdapter4), reinterpret_cast<void **>(adapter4.put())),
      S_OK);

  DXGI_ADAPTER_DESC desc = {};
  DXGI_ADAPTER_DESC1 desc1 = {};
  DXGI_ADAPTER_DESC2 desc2 = {};
  DXGI_ADAPTER_DESC3 desc3 = {};
  ASSERT_EQ(context_.adapter()->GetDesc(&desc), S_OK);
  ASSERT_EQ(adapter1->GetDesc1(&desc1), S_OK);
  ASSERT_EQ(adapter2->GetDesc2(&desc2), S_OK);
  ASSERT_EQ(adapter4->GetDesc3(&desc3), S_OK);

  const auto expect_common = [&](const auto &versioned_desc) {
    EXPECT_EQ(std::wstring(versioned_desc.Description),
              std::wstring(desc.Description));
    EXPECT_EQ(versioned_desc.VendorId, desc.VendorId);
    EXPECT_EQ(versioned_desc.DeviceId, desc.DeviceId);
    EXPECT_EQ(versioned_desc.SubSysId, desc.SubSysId);
    EXPECT_EQ(versioned_desc.Revision, desc.Revision);
    EXPECT_EQ(versioned_desc.DedicatedVideoMemory, desc.DedicatedVideoMemory);
    EXPECT_EQ(versioned_desc.DedicatedSystemMemory, desc.DedicatedSystemMemory);
    EXPECT_EQ(versioned_desc.SharedSystemMemory, desc.SharedSystemMemory);
    EXPECT_EQ(versioned_desc.AdapterLuid.HighPart, desc.AdapterLuid.HighPart);
    EXPECT_EQ(versioned_desc.AdapterLuid.LowPart, desc.AdapterLuid.LowPart);
  };
  expect_common(desc1);
  expect_common(desc2);
  expect_common(desc3);
  EXPECT_EQ(desc2.GraphicsPreemptionGranularity,
            desc3.GraphicsPreemptionGranularity);
  EXPECT_EQ(desc2.ComputePreemptionGranularity,
            desc3.ComputePreemptionGranularity);
  EXPECT_NE(desc.Description[0], L'\0');
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiAdapterFactoryObjectContractSpec,
       AdapterParentIsTheCreatingFactory) {
  ComPtr<IDXGIFactory1> parent;
  ASSERT_EQ(
      context_.adapter()->GetParent(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void **>(parent.put())),
      S_OK);
  ComPtr<IUnknown> expected_identity;
  ComPtr<IUnknown> parent_identity;
  ASSERT_EQ(context_.factory()->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(expected_identity.put())),
            S_OK);
  ASSERT_EQ(
      parent->QueryInterface(__uuidof(IUnknown),
                             reinterpret_cast<void **>(parent_identity.put())),
      S_OK);
  EXPECT_EQ(parent_identity.get(), expected_identity.get());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiAdapterFactoryObjectContractSpec,
       RejectsUnsupportedInterfacesAndNullOutputs) {
  for (IUnknown *object : {static_cast<IUnknown *>(context_.factory()),
                           static_cast<IUnknown *>(context_.adapter())}) {
    void *output = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
    EXPECT_EQ(object->QueryInterface(__uuidof(ID3D11Buffer), &output),
              E_NOINTERFACE);
    EXPECT_EQ(output, nullptr);
    EXPECT_EQ(object->QueryInterface(__uuidof(IUnknown), nullptr), E_POINTER);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DxgiAdapterFactoryObjectContractSpec,
       KeepsFactoryAndAdapterPrivateDataIndependent) {
  constexpr std::array<std::uint8_t, 4> kFactoryValue = {3, 1, 4, 1};
  constexpr std::array<std::uint8_t, 6> kAdapterValue = {2, 7, 1, 8, 2, 8};
  ASSERT_EQ(context_.factory()->SetPrivateData(kObjectPrivateDataKey,
                                               kFactoryValue.size(),
                                               kFactoryValue.data()),
            S_OK);
  ASSERT_EQ(context_.adapter()->SetPrivateData(kObjectPrivateDataKey,
                                               kAdapterValue.size(),
                                               kAdapterValue.data()),
            S_OK);

  std::array<std::uint8_t, kFactoryValue.size()> factory_value = {};
  UINT factory_size = factory_value.size();
  ASSERT_EQ(context_.factory()->GetPrivateData(
                kObjectPrivateDataKey, &factory_size, factory_value.data()),
            S_OK);
  EXPECT_EQ(factory_size, kFactoryValue.size());
  EXPECT_EQ(factory_value, kFactoryValue);

  std::array<std::uint8_t, kAdapterValue.size()> adapter_value = {};
  UINT adapter_size = adapter_value.size();
  ASSERT_EQ(context_.adapter()->GetPrivateData(
                kObjectPrivateDataKey, &adapter_size, adapter_value.data()),
            S_OK);
  EXPECT_EQ(adapter_size, kAdapterValue.size());
  EXPECT_EQ(adapter_value, kAdapterValue);

  EXPECT_EQ(
      context_.factory()->SetPrivateData(kObjectPrivateDataKey, 0, nullptr),
      S_OK);
  factory_size = factory_value.size();
  EXPECT_EQ(context_.factory()->GetPrivateData(
                kObjectPrivateDataKey, &factory_size, factory_value.data()),
            DXGI_ERROR_NOT_FOUND);
  adapter_size = adapter_value.size();
  EXPECT_EQ(context_.adapter()->GetPrivateData(
                kObjectPrivateDataKey, &adapter_size, adapter_value.data()),
            S_OK);
  EXPECT_EQ(adapter_value, kAdapterValue);
  EXPECT_EQ(
      context_.adapter()->SetPrivateData(kObjectPrivateDataKey, 0, nullptr),
      S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
