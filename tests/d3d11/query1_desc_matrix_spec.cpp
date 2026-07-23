#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11.3 query-object coverage. Every logical case creates a Query1
// object with an ALL-context descriptor and verifies both versioned and legacy
// descriptor and COM-interface contracts.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct Query1Case {
  D3D11_QUERY query;
  UINT misc_flags;
  UINT data_size;
  const char *name;
};

constexpr std::array kQuery1Cases = {
    Query1Case{D3D11_QUERY_EVENT, 0, sizeof(BOOL), "Event"},
    Query1Case{D3D11_QUERY_OCCLUSION, 0, sizeof(UINT64), "Occlusion"},
    Query1Case{D3D11_QUERY_TIMESTAMP, 0, sizeof(UINT64), "Timestamp"},
    Query1Case{D3D11_QUERY_TIMESTAMP_DISJOINT, 0,
               sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT),
               "TimestampDisjoint"},
    Query1Case{D3D11_QUERY_PIPELINE_STATISTICS, 0,
               sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS),
               "PipelineStatistics"},
    Query1Case{D3D11_QUERY_OCCLUSION_PREDICATE, 0, sizeof(BOOL),
               "OcclusionPredicate"},
    Query1Case{D3D11_QUERY_OCCLUSION_PREDICATE, D3D11_QUERY_MISC_PREDICATEHINT,
               sizeof(BOOL), "OcclusionPredicateHint"},
};

constexpr std::uint32_t kQuery1CaseCount = kQuery1Cases.size();

const dxmt::test::LogicalCaseFamilyRegistration kQuery1DescCases(
    "D3D11Query1DescMatrixSpec.RoundTripsSupportedDescriptions",
    "D3D11.Query1.Description.", kQuery1CaseCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device3,CreateQuery1,ID3D11Query1GetDesc1,"
      "ID3D11QueryGetDesc,ID3D11AsynchronousGetDataSize,"
      "ID3D11DeviceChildGetDevice,QueryInterface,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live Query1 COM object per selected "
     "logical case",
     "create every supported Query1 kind with ALL context type, including "
     "both occlusion-predicate flag modes, then inspect both public "
     "descriptors and interfaces",
     "each query preserves its full Desc1, exposes the matching legacy "
     "descriptor and data size, returns its creating device, and supports "
     "Query1 and legacy Query interfaces with one COM identity",
     "logical ID, selected-case count, query type and name, flags, context "
     "type, expected and returned descriptors, data size, owner and interface "
     "addresses, HRESULT, failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kQuery1DescCost("D3D11Query1DescMatrixSpec.RoundTripsSupportedDescriptions",
                    dxmt::test::kResourceTestCost);

class D3D11Query1DescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device3), reinterpret_cast<void **>(device3_.put())),
        S_OK);
    ASSERT_NE(device3_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device3> device3_;
};

TEST_F(D3D11Query1DescMatrixSpec, RoundTripsSupportedDescriptions) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kQuery1CaseCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kQuery1DescCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kQuery1DescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const Query1Case &test_case = kQuery1Cases[logical];
    const D3D11_QUERY_DESC1 expected = {test_case.query, test_case.misc_flags,
                                        D3D11_CONTEXT_TYPE_ALL};
    ComPtr<ID3D11Query1> query;
    const HRESULT create_result =
        device3_->CreateQuery1(&expected, query.put());
    D3D11_QUERY_DESC1 actual = {};
    D3D11_QUERY_DESC legacy_desc = {};
    UINT data_size = 0;
    ComPtr<ID3D11Query1> queried_query1;
    ComPtr<ID3D11Query> legacy_query;
    ComPtr<ID3D11Device> owner;
    HRESULT query1_result = E_FAIL;
    HRESULT legacy_result = E_FAIL;
    if (create_result == S_OK && query) {
      query->GetDesc1(&actual);
      query->GetDesc(&legacy_desc);
      data_size = query->GetDataSize();
      query->GetDevice(owner.put());
      query1_result = query->QueryInterface(
          __uuidof(ID3D11Query1),
          reinterpret_cast<void **>(queried_query1.put()));
      legacy_result = query->QueryInterface(
          __uuidof(ID3D11Query), reinterpret_cast<void **>(legacy_query.put()));
    }

    const bool valid =
        create_result == S_OK && query && actual.Query == expected.Query &&
        actual.MiscFlags == expected.MiscFlags &&
        actual.ContextType == expected.ContextType &&
        legacy_desc.Query == expected.Query &&
        legacy_desc.MiscFlags == expected.MiscFlags &&
        data_size == test_case.data_size && owner.get() == context_.device() &&
        query1_result == S_OK && queried_query1.get() == query.get() &&
        legacy_result == S_OK && legacy_query.get() == query.get();
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kQuery1DescCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kQuery1DescCases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " name=" << test_case.name
                  << " query=" << static_cast<UINT>(expected.Query)
                  << " misc_flags=" << expected.MiscFlags
                  << " context_type=" << static_cast<UINT>(expected.ContextType)
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: create_hresult=" << S_OK
                  << " query1_hresult=" << S_OK << " legacy_hresult=" << S_OK
                  << " data_size=" << test_case.data_size
                  << " owner=" << context_.device() << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " query1_hresult=" << query1_result
                  << " legacy_hresult=" << legacy_result
                  << " query=" << static_cast<UINT>(actual.Query)
                  << " misc_flags=" << actual.MiscFlags
                  << " context_type=" << static_cast<UINT>(actual.ContextType)
                  << " legacy_query=" << static_cast<UINT>(legacy_desc.Query)
                  << " legacy_misc_flags=" << legacy_desc.MiscFlags
                  << " data_size=" << data_size << " owner=" << owner.get()
                  << " object=" << query.get()
                  << " queried_query1=" << queried_query1.get()
                  << " queried_legacy=" << legacy_query.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11Query1DescMatrixSpec, ValidatesDescriptionAndOutputPointer) {
  ID3D11Query1 *query =
      reinterpret_cast<ID3D11Query1 *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateQuery1(nullptr, &query), E_INVALIDARG);
  EXPECT_EQ(query, nullptr);

  const D3D11_QUERY_DESC1 desc = {D3D11_QUERY_EVENT, 0, D3D11_CONTEXT_TYPE_ALL};
  EXPECT_EQ(device3_->CreateQuery1(&desc, nullptr), S_FALSE);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
