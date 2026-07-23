#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_6.h>

#include <array>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

// Public DXGI output enumeration and COM-object coverage. The tests only read
// display topology and never take output ownership or change display state, so
// they remain safe under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr GUID kOutputPrivateDataKey = {
    0xe6b8d8c4,
    0x6452,
    0x46d7,
    {0xa3, 0x8e, 0x2b, 0x75, 0x9f, 0xa0, 0xc3, 0x18}};

struct OutputInterfaceCase {
  const GUID *iid;
  HRESULT expected;
  const char *name;
};

constexpr std::array kOutputInterfaceCases = {
    OutputInterfaceCase{&__uuidof(IUnknown), S_OK, "IUnknown"},
    OutputInterfaceCase{&__uuidof(IDXGIObject), S_OK, "IDXGIObject"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput), S_OK, "IDXGIOutput"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput1), S_OK, "IDXGIOutput1"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput2), S_OK, "IDXGIOutput2"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput3), S_OK, "IDXGIOutput3"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput4), S_OK, "IDXGIOutput4"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput5), S_OK, "IDXGIOutput5"},
    OutputInterfaceCase{&__uuidof(IDXGIOutput6), S_OK, "IDXGIOutput6"},
    OutputInterfaceCase{&__uuidof(ID3D11Device), E_NOINTERFACE,
                        "UnsupportedDevice"},
};

const dxmt::test::LogicalCaseFamilyRegistration kOutputInterfaceRegistration(
    "D3D11DxgiOutputContractSpec.QueriesEveryPublicOutputInterface",
    "D3D11.DXGI.Output.Interface.", kOutputInterfaceCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIAdapterEnumOutputs,IDXGIOutput,IDXGIOutput1,IDXGIOutput2,"
      "IDXGIOutput3,IDXGIOutput4,IDXGIOutput5,IDXGIOutput6,IDXGIObject,"
      "QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one fixture-local adapter and at most one read-only output object per "
     "selected logical case",
     "enumerate the first available output and query every public output "
     "interface version plus one unrelated interface",
     "systems without an attached output report DXGI_ERROR_NOT_FOUND; an "
     "available output exposes all runtime interface versions under one COM "
     "identity and rejects the unrelated interface",
     "logical ID, interface name, enumeration and QueryInterface HRESULTs, "
     "object identities, output availability, and exact replay argument"});

const dxmt::test::TestCostRegistration kOutputInterfaceCost(
    "D3D11DxgiOutputContractSpec.QueriesEveryPublicOutputInterface",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration kOutputEnumerationCost(
    "D3D11DxgiOutputContractSpec.EnumeratesStableObjectsUntilNotFound",
    dxmt::test::kResourceTestCost);

class D3D11DxgiOutputContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.InitializeAdapter(), S_OK);
    ASSERT_NE(context_.adapter(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DxgiOutputContractSpec, QueriesEveryPublicOutputInterface) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kOutputInterfaceCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kOutputInterfaceRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const OutputInterfaceCase &test_case = kOutputInterfaceCases[logical];
    const auto case_id = dxmt::test::LogicalCaseId(
        kOutputInterfaceRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message() << "LogicalCaseId: " << case_id
                                      << " interface=" << test_case.name
                                      << " Replay: --dxmt-case-id=" << case_id);

    ComPtr<IDXGIOutput> output;
    const HRESULT enumeration_result =
        context_.adapter()->EnumOutputs(0, output.put());
    ASSERT_TRUE(enumeration_result == S_OK ||
                enumeration_result == DXGI_ERROR_NOT_FOUND);
    if (enumeration_result == DXGI_ERROR_NOT_FOUND) {
      EXPECT_EQ(output.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(output);

    ComPtr<IUnknown> output_identity;
    ASSERT_EQ(output->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(output_identity.put())),
              S_OK);
    ComPtr<IUnknown> queried;
    const HRESULT interface_result = output->QueryInterface(
        *test_case.iid, reinterpret_cast<void **>(queried.put()));
    EXPECT_EQ(interface_result, test_case.expected);
    if (test_case.expected == E_NOINTERFACE) {
      EXPECT_EQ(queried.get(), nullptr);
      continue;
    }
    ASSERT_TRUE(queried);
    ComPtr<IUnknown> queried_identity;
    ASSERT_EQ(queried->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(queried_identity.put())),
              S_OK);
    EXPECT_EQ(queried_identity.get(), output_identity.get());
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kOutputInterfaceRegistration.family().case_id_prefix);
}

TEST_F(D3D11DxgiOutputContractSpec, EnumeratesStableObjectsUntilNotFound) {
  EXPECT_EQ(context_.adapter()->EnumOutputs(0, nullptr), E_INVALIDARG);

  ComPtr<IUnknown> adapter_identity;
  ASSERT_EQ(context_.adapter()->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(adapter_identity.put())),
            S_OK);
  std::set<IUnknown *> output_identities;
  std::vector<ComPtr<IDXGIOutput>> live_outputs;
  bool reached_boundary = false;
  UINT output_count = 0;
  for (UINT index = 0; index < 32; ++index) {
    ComPtr<IDXGIOutput> output;
    const HRESULT hr = context_.adapter()->EnumOutputs(index, output.put());
    if (hr == DXGI_ERROR_NOT_FOUND) {
      EXPECT_EQ(output.get(), nullptr);
      reached_boundary = true;
      break;
    }
    ASSERT_EQ(hr, S_OK) << "output_index=" << index;
    ASSERT_TRUE(output);
    ++output_count;

    DXGI_OUTPUT_DESC desc = {};
    EXPECT_EQ(output->GetDesc(nullptr), DXGI_ERROR_INVALID_CALL);
    ASSERT_EQ(output->GetDesc(&desc), S_OK);
    EXPECT_LE(desc.DesktopCoordinates.left, desc.DesktopCoordinates.right);
    EXPECT_LE(desc.DesktopCoordinates.top, desc.DesktopCoordinates.bottom);

    ComPtr<IDXGIAdapter> parent;
    ASSERT_EQ(output->GetParent(__uuidof(IDXGIAdapter),
                                reinterpret_cast<void **>(parent.put())),
              S_OK);
    ComPtr<IUnknown> parent_identity;
    ASSERT_EQ(parent->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(parent_identity.put())),
              S_OK);
    EXPECT_EQ(parent_identity.get(), adapter_identity.get());

    ComPtr<IUnknown> output_identity;
    ASSERT_EQ(output->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(output_identity.put())),
              S_OK);
    EXPECT_TRUE(output_identities.insert(output_identity.get()).second);

    constexpr std::array<std::uint8_t, 5> kPrivateValue = {2, 3, 5, 7, 11};
    ASSERT_EQ(output->SetPrivateData(kOutputPrivateDataKey,
                                     kPrivateValue.size(),
                                     kPrivateValue.data()),
              S_OK);
    std::array<std::uint8_t, kPrivateValue.size()> returned = {};
    UINT returned_size = returned.size();
    ASSERT_EQ(output->GetPrivateData(kOutputPrivateDataKey, &returned_size,
                                     returned.data()),
              S_OK);
    EXPECT_EQ(returned_size, kPrivateValue.size());
    EXPECT_EQ(returned, kPrivateValue);
    EXPECT_EQ(output->SetPrivateData(kOutputPrivateDataKey, 0, nullptr), S_OK);
    live_outputs.push_back(std::move(output));
  }

  EXPECT_TRUE(reached_boundary);
  RecordProperty("outputs_enumerated", output_count);
}

} // namespace
