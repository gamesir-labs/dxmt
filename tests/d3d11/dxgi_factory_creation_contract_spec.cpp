#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_6.h>

#include <array>
#include <cstdint>

// Public DXGI factory creation and feature-query coverage. Each case owns its
// factory and does not alter window association or other process-global state.

namespace {

using dxmt::test::ComPtr;

enum class FactoryCreationApi {
  Factory1,
  Factory2,
};

struct FactoryCreationCase {
  FactoryCreationApi api;
  const GUID *iid;
  HRESULT expected;
  const char *name;
};

constexpr std::array kFactoryCreationCases = {
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(IUnknown), S_OK,
                        "Factory1IUnknown"},
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(IDXGIObject),
                        S_OK, "Factory1Object"},
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(IDXGIFactory),
                        S_OK, "Factory1Factory"},
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(IDXGIFactory1),
                        S_OK, "Factory1Factory1"},
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(IDXGIFactory2),
                        S_OK, "Factory1Factory2"},
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(IDXGIFactory3),
                        S_OK, "Factory1Factory3"},
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(IDXGIFactory4),
                        S_OK, "Factory1Factory4"},
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(IDXGIFactory5),
                        S_OK, "Factory1Factory5"},
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(IDXGIFactory6),
                        S_OK, "Factory1Factory6"},
    FactoryCreationCase{FactoryCreationApi::Factory1, &__uuidof(ID3D11Device),
                        E_NOINTERFACE, "Factory1UnsupportedDevice"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(IUnknown), S_OK,
                        "Factory2IUnknown"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(IDXGIObject),
                        S_OK, "Factory2Object"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(IDXGIFactory),
                        S_OK, "Factory2Factory"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(IDXGIFactory1),
                        S_OK, "Factory2Factory1"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(IDXGIFactory2),
                        S_OK, "Factory2Factory2"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(IDXGIFactory3),
                        S_OK, "Factory2Factory3"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(IDXGIFactory4),
                        S_OK, "Factory2Factory4"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(IDXGIFactory5),
                        S_OK, "Factory2Factory5"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(IDXGIFactory6),
                        S_OK, "Factory2Factory6"},
    FactoryCreationCase{FactoryCreationApi::Factory2, &__uuidof(ID3D11Device),
                        E_NOINTERFACE, "Factory2UnsupportedDevice"},
};

struct FactoryFeatureCase {
  DXGI_FEATURE feature;
  UINT size;
  HRESULT expected;
  bool expect_boolean;
  const char *name;
};

constexpr std::array kFactoryFeatureCases = {
    FactoryFeatureCase{static_cast<DXGI_FEATURE>(0x12345678), sizeof(BOOL),
                       DXGI_ERROR_INVALID_CALL, false, "UnknownFeature"},
    FactoryFeatureCase{DXGI_FEATURE_PRESENT_ALLOW_TEARING, sizeof(BOOL) - 1,
                       DXGI_ERROR_INVALID_CALL, false, "TearingUndersized"},
    FactoryFeatureCase{DXGI_FEATURE_PRESENT_ALLOW_TEARING, sizeof(BOOL) + 1,
                       DXGI_ERROR_INVALID_CALL, false, "TearingOversized"},
    FactoryFeatureCase{DXGI_FEATURE_PRESENT_ALLOW_TEARING, sizeof(BOOL), S_OK,
                       true, "TearingBoolean"},
};

const dxmt::test::LogicalCaseFamilyRegistration kFactoryCreationRegistration(
    "D3D11DxgiFactoryCreationContractSpec."
    "CreatesEveryPublicFactoryInterfaceDirectly",
    "D3D11.DXGI.FactoryCreation.Interface.", kFactoryCreationCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateDXGIFactory1,CreateDXGIFactory2,IDXGIFactory,IDXGIFactory1,"
      "IDXGIFactory2,IDXGIFactory3,IDXGIFactory4,IDXGIFactory5,"
      "IDXGIFactory6,IDXGIObject,QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one independently created DXGI factory per selected logical case",
     "request every public factory interface from CreateDXGIFactory1 and "
     "CreateDXGIFactory2 plus one unsupported device interface",
     "supported requests return a factory with one canonical identity; "
     "unsupported requests return E_NOINTERFACE and clear output",
     "logical ID, creation API, interface, HRESULT, identity, base pointer, "
     "and exact replay argument"});

const dxmt::test::LogicalCaseFamilyRegistration kFactoryFeatureRegistration(
    "D3D11DxgiFactoryCreationContractSpec."
    "ValidatesFactoryFeatureQueries",
    "D3D11.DXGI.FactoryCreation.FeatureQuery.", kFactoryFeatureCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateDXGIFactory2,IDXGIFactory5,CheckFeatureSupport,"
      "DXGI_FEATURE_PRESENT_ALLOW_TEARING"},
     dxmt::test::kResourceTestCost,
     "one test-local DXGI factory",
     "query tearing support with exact, undersized, and oversized buffers and "
     "query one unknown feature",
     "only the exact known feature query succeeds and writes a canonical BOOL",
     "logical ID, feature, buffer size, HRESULT, returned BOOL, and exact "
     "replay argument"});

const dxmt::test::TestCostRegistration
    kFactoryCreationCost("D3D11DxgiFactoryCreationContractSpec."
                         "CreatesEveryPublicFactoryInterfaceDirectly",
                         dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kFactoryObjectCost("D3D11DxgiFactoryCreationContractSpec."
                       "FreshFactoryReportsStableObjectState",
                       dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kFactoryFeatureCost("D3D11DxgiFactoryCreationContractSpec."
                        "ValidatesFactoryFeatureQueries",
                        dxmt::test::kResourceTestCost);

class D3D11DxgiFactoryCreationContractSpec : public ::testing::Test {};

TEST_F(D3D11DxgiFactoryCreationContractSpec,
       CreatesEveryPublicFactoryInterfaceDirectly) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kFactoryCreationCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kFactoryCreationRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const auto &test_case = kFactoryCreationCases[logical];
    const auto case_id = dxmt::test::LogicalCaseId(
        kFactoryCreationRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id
                 << " api=" << static_cast<UINT>(test_case.api) << " interface="
                 << test_case.name << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<IUnknown> created;
    const HRESULT hr =
        test_case.api == FactoryCreationApi::Factory1
            ? CreateDXGIFactory1(*test_case.iid,
                                 reinterpret_cast<void **>(created.put()))
            : CreateDXGIFactory2(0, *test_case.iid,
                                 reinterpret_cast<void **>(created.put()));
    EXPECT_EQ(hr, test_case.expected);
    if (test_case.expected == E_NOINTERFACE) {
      EXPECT_EQ(created.get(), nullptr);
      continue;
    }

    ASSERT_EQ(hr, S_OK);
    ASSERT_TRUE(created);
    ComPtr<IUnknown> identity;
    ComPtr<IDXGIFactory> base;
    ASSERT_EQ(
        created->QueryInterface(__uuidof(IUnknown),
                                reinterpret_cast<void **>(identity.put())),
        S_OK);
    ASSERT_EQ(created->QueryInterface(__uuidof(IDXGIFactory),
                                      reinterpret_cast<void **>(base.put())),
              S_OK);
    ComPtr<IUnknown> base_identity;
    ASSERT_EQ(
        base->QueryInterface(__uuidof(IUnknown),
                             reinterpret_cast<void **>(base_identity.put())),
        S_OK);
    EXPECT_EQ(base_identity.get(), identity.get());
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kFactoryCreationRegistration.family().case_id_prefix);
}

TEST_F(D3D11DxgiFactoryCreationContractSpec,
       FreshFactoryReportsStableObjectState) {
  ComPtr<IDXGIFactory6> factory;
  ASSERT_EQ(CreateDXGIFactory2(0, __uuidof(IDXGIFactory6),
                               reinterpret_cast<void **>(factory.put())),
            S_OK);
  ASSERT_TRUE(factory);
  EXPECT_EQ(factory->GetCreationFlags(), 0u);
  EXPECT_EQ(factory->IsCurrent(), TRUE);

  void *parent = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(factory->GetParent(__uuidof(IUnknown), &parent), E_NOINTERFACE);
  EXPECT_EQ(parent, nullptr);
}

TEST_F(D3D11DxgiFactoryCreationContractSpec, ValidatesFactoryFeatureQueries) {
  ComPtr<IDXGIFactory5> factory;
  ASSERT_EQ(CreateDXGIFactory2(0, __uuidof(IDXGIFactory5),
                               reinterpret_cast<void **>(factory.put())),
            S_OK);

  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kFactoryFeatureCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kFactoryFeatureRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const auto &test_case = kFactoryFeatureCases[logical];
    const auto case_id = dxmt::test::LogicalCaseId(
        kFactoryFeatureRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id
                 << " feature=" << static_cast<UINT>(test_case.feature)
                 << " size=" << test_case.size << " case=" << test_case.name
                 << " Replay: --dxmt-case-id=" << case_id);

    BOOL data = static_cast<BOOL>(0x12345678);
    const HRESULT hr =
        factory->CheckFeatureSupport(test_case.feature, &data, test_case.size);
    EXPECT_EQ(hr, test_case.expected);
    if (test_case.expect_boolean) {
      EXPECT_TRUE(data == TRUE || data == FALSE);
    }
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kFactoryFeatureRegistration.family().case_id_prefix);
}

} // namespace
