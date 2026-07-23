#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <dxgi1_4.h>

#include <array>
#include <cstdint>
#include <limits>

// Public IDXGIAdapter3 video-memory query coverage. The tests intentionally do
// not set process reservations or register global notifications, so they only
// read adapter state and remain safe under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

struct MemoryQueryCase {
  UINT node;
  DXGI_MEMORY_SEGMENT_GROUP group;
  HRESULT expected;
  const char *name;
};

const std::array kMemoryQueryCases = {
    MemoryQueryCase{0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, S_OK, "Local"},
    MemoryQueryCase{0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, S_OK, "NonLocal"},
    MemoryQueryCase{std::numeric_limits<UINT>::max(),
                    DXGI_MEMORY_SEGMENT_GROUP_LOCAL, E_INVALIDARG,
                    "InvalidNode"},
    MemoryQueryCase{0, static_cast<DXGI_MEMORY_SEGMENT_GROUP>(2), E_INVALIDARG,
                    "InvalidSegment"},
};

const dxmt::test::LogicalCaseFamilyRegistration kMemoryQueryRegistration(
    "D3D11DxgiAdapterMemoryContractSpec.QueriesSegmentsAndValidatesArguments",
    "D3D11.DXGI.Adapter.MemoryQuery.", kMemoryQueryCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "IDXGIAdapter3,QueryVideoMemoryInfo,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,"
      "DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL"},
     dxmt::test::kResourceTestCost,
     "one fixture-local IDXGIAdapter3 reference and no memory reservation",
     "query local and non-local memory on node zero, then pass an out-of-range "
     "node and segment",
     "documented segment queries succeed with internally consistent values; "
     "invalid arguments return E_INVALIDARG",
     "logical ID, node, segment, HRESULT, memory counters, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration kMemoryQueryCost(
    "D3D11DxgiAdapterMemoryContractSpec.QueriesSegmentsAndValidatesArguments",
    dxmt::test::kResourceTestCost);

class D3D11DxgiAdapterMemoryContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.InitializeAdapter(), S_OK);
    ASSERT_EQ(context_.adapter()->QueryInterface(
                  __uuidof(IDXGIAdapter3),
                  reinterpret_cast<void **>(adapter3_.put())),
              S_OK);
    ASSERT_TRUE(adapter3_);
  }

  D3D11TestContext context_;
  ComPtr<IDXGIAdapter3> adapter3_;
};

TEST_F(D3D11DxgiAdapterMemoryContractSpec,
       QueriesSegmentsAndValidatesArguments) {
  std::uint32_t selected_count = 0;
  for (std::uint32_t logical = 0; logical < kMemoryQueryCases.size();
       ++logical) {
    if (!dxmt::test::LogicalCaseSelected(kMemoryQueryRegistration.family(),
                                         logical))
      continue;
    ++selected_count;
    const MemoryQueryCase &test_case = kMemoryQueryCases[logical];
    const auto case_id =
        dxmt::test::LogicalCaseId(kMemoryQueryRegistration.family(), logical);
    SCOPED_TRACE(::testing::Message()
                 << "LogicalCaseId: " << case_id << " case=" << test_case.name
                 << " node=" << test_case.node
                 << " segment=" << static_cast<UINT>(test_case.group)
                 << " Replay: --dxmt-case-id=" << case_id);

    DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
    info.Budget = std::numeric_limits<UINT64>::max();
    info.CurrentUsage = std::numeric_limits<UINT64>::max();
    info.AvailableForReservation = std::numeric_limits<UINT64>::max();
    info.CurrentReservation = std::numeric_limits<UINT64>::max();
    const HRESULT hr =
        adapter3_->QueryVideoMemoryInfo(test_case.node, test_case.group, &info);
    EXPECT_EQ(hr, test_case.expected);
    if (test_case.expected == S_OK) {
      EXPECT_LE(info.CurrentReservation, info.Budget);
      EXPECT_LE(info.AvailableForReservation, info.Budget);
    }
  }

  ASSERT_NE(selected_count, 0u);
  RecordProperty("logical_cases_executed", selected_count);
  RecordProperty("logical_case_prefix",
                 kMemoryQueryRegistration.family().case_id_prefix);
}

} // namespace
