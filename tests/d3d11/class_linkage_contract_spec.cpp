#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 class-linkage object coverage. Dynamic class-instance behavior
// is intentionally outside this test because DXMT does not implement it; the
// stable creation, ownership, and COM identity contracts remain observable.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct InterfaceCase {
  const GUID *iid;
  const char *name;
};

constexpr std::array kInterfaceCases = {
    InterfaceCase{&__uuidof(IUnknown), "IUnknown"},
    InterfaceCase{&__uuidof(ID3D11DeviceChild), "ID3D11DeviceChild"},
    InterfaceCase{&__uuidof(ID3D11ClassLinkage), "ID3D11ClassLinkage"},
};

constexpr GUID kUnknownInterface = {
    0x2c866476,
    0x1f63,
    0x4ed4,
    {0x9c, 0xf0, 0x7d, 0xf7, 0x9e, 0x25, 0x33, 0x31}};

constexpr std::uint32_t kInterfaceCaseCount = kInterfaceCases.size();

const dxmt::test::LogicalCaseFamilyRegistration kClassLinkageInterfaceCases(
    "D3D11ClassLinkageContractSpec.ExposesDeviceChildComIdentity",
    "D3D11.ClassLinkage.Interface.", kInterfaceCaseCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device,CreateClassLinkage,ID3D11DeviceChildGetDevice,"
      "QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live class-linkage object per "
     "selected logical case",
     "create a class-linkage object and query IUnknown, ID3D11DeviceChild, "
     "and ID3D11ClassLinkage through the public COM API",
     "every supported interface resolves to the same canonical IUnknown "
     "identity and the object returns its creating device",
     "logical ID, selected-case count, interface name and IID, HRESULT, "
     "object, canonical identity and owner addresses, failure phase, and "
     "exact replay argument"});

const dxmt::test::TestCostRegistration kClassLinkageInterfaceCost(
    "D3D11ClassLinkageContractSpec.ExposesDeviceChildComIdentity",
    dxmt::test::kResourceTestCost);

class D3D11ClassLinkageContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ClassLinkageContractSpec, ExposesDeviceChildComIdentity) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kInterfaceCaseCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kClassLinkageInterfaceCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kClassLinkageInterfaceCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const InterfaceCase &test_case = kInterfaceCases[logical];
    ComPtr<ID3D11ClassLinkage> linkage;
    const HRESULT create_result =
        context_.device()->CreateClassLinkage(linkage.put());
    ComPtr<IUnknown> canonical_identity;
    ComPtr<IUnknown> queried_interface;
    ComPtr<IUnknown> queried_identity;
    ComPtr<ID3D11Device> owner;
    HRESULT interface_result = E_FAIL;
    HRESULT identity_result = E_FAIL;
    if (create_result == S_OK && linkage) {
      linkage->GetDevice(owner.put());
      linkage->QueryInterface(
          __uuidof(IUnknown),
          reinterpret_cast<void **>(canonical_identity.put()));
      interface_result = linkage->QueryInterface(
          *test_case.iid, reinterpret_cast<void **>(queried_interface.put()));
      if (interface_result == S_OK && queried_interface) {
        identity_result = queried_interface->QueryInterface(
            __uuidof(IUnknown),
            reinterpret_cast<void **>(queried_identity.put()));
      }
    }

    const bool valid = create_result == S_OK && linkage &&
                       owner.get() == context_.device() && canonical_identity &&
                       interface_result == S_OK && queried_interface &&
                       identity_result == S_OK &&
                       queried_identity.get() == canonical_identity.get();
    if (valid)
      continue;

    const auto case_id = dxmt::test::LogicalCaseId(
        kClassLinkageInterfaceCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kClassLinkageInterfaceCases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " interface=" << test_case.name
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: create_hresult=" << S_OK
                  << " interface_hresult=" << S_OK
                  << " identity_hresult=" << S_OK
                  << " owner=" << context_.device() << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " interface_hresult=" << interface_result
                  << " identity_hresult=" << identity_result
                  << " linkage=" << linkage.get()
                  << " interface=" << queried_interface.get()
                  << " canonical_identity=" << canonical_identity.get()
                  << " queried_identity=" << queried_identity.get()
                  << " owner=" << owner.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11ClassLinkageContractSpec, QueryInterfaceValidatesAndClearsOutput) {
  ComPtr<ID3D11ClassLinkage> linkage;
  ASSERT_EQ(context_.device()->CreateClassLinkage(linkage.put()), S_OK);
  ASSERT_NE(linkage.get(), nullptr);

  EXPECT_EQ(linkage->QueryInterface(__uuidof(IUnknown), nullptr), E_POINTER);

  void *output = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(linkage->QueryInterface(kUnknownInterface, &output), E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
