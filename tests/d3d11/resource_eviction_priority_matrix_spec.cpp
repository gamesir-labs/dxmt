#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 / DXGI eviction-priority coverage. Sixty-four resources combine
// with 64 distinct histories made only from the five documented priorities.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kEvictionPriorityCaseCount = 4096;
constexpr std::uint32_t kResourceCount = 64;
constexpr UINT kPriorityHistoryCount = 64;
constexpr UINT kPriorityHistoryLength = 3;

const dxmt::test::LogicalCaseFamilyRegistration kEvictionPriorityCases(
    "D3D11ResourceEvictionPriorityMatrixSpec."
    "RoundTrips4096ResourceAndPriorityHistories",
    "D3D11.ResourceEvictionPriority.State.", kEvictionPriorityCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "CreateBuffer,ID3D11ResourceSetEvictionPriority,"
      "ID3D11ResourceGetEvictionPriority,IDXGIResourceSetEvictionPriority,"
      "IDXGIResourceGetEvictionPriority"},
     dxmt::test::kResourceTestCost,
     "sixty-four test-local D3D11 buffers queried for their public "
     "IDXGIResource interfaces",
     "apply 64 distinct three-step histories built from all five documented "
     "priorities to every resource, alternating D3D11 and DXGI setters, and "
     "check an untouched guard resource",
     "both public interfaces return the same most recently assigned priority, "
     "reset independently to NORMAL, and do not change the guard resource",
     "logical ID, selected-case count, resource and history indices, expected "
     "and returned priorities from both APIs, reset and guard results, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kEvictionPriorityCost("D3D11ResourceEvictionPriorityMatrixSpec."
                          "RoundTrips4096ResourceAndPriorityHistories",
                          dxmt::test::kResourceTestCost);

constexpr std::array<UINT, 5> kPriorities = {
    DXGI_RESOURCE_PRIORITY_MINIMUM, DXGI_RESOURCE_PRIORITY_LOW,
    DXGI_RESOURCE_PRIORITY_NORMAL,  DXGI_RESOURCE_PRIORITY_HIGH,
    DXGI_RESOURCE_PRIORITY_MAXIMUM,
};

struct EvictionPriorityCase {
  std::uint32_t resource_index;
  UINT history_index;
  std::array<UINT, kPriorityHistoryLength> history;
};

EvictionPriorityCase CaseForLogical(std::uint32_t logical) {
  const std::uint32_t resource_index = logical % kResourceCount;
  UINT encoded_history = (logical / kResourceCount) % kPriorityHistoryCount;
  EvictionPriorityCase test_case = {resource_index, encoded_history, {}};
  for (UINT step = 0; step < kPriorityHistoryLength; ++step) {
    test_case.history[step] = kPriorities[encoded_history % kPriorities.size()];
    encoded_history /= kPriorities.size();
  }
  return test_case;
}

class D3D11ResourceEvictionPriorityMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ResourceEvictionPriorityMatrixSpec,
       RoundTrips4096ResourceAndPriorityHistories) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kEvictionPriorityCaseCount);
  for (std::uint32_t logical = 0; logical < kEvictionPriorityCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kEvictionPriorityCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = 16;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  std::array<ComPtr<ID3D11Buffer>, kResourceCount> resources;
  std::array<ComPtr<IDXGIResource>, kResourceCount> dxgi_resources;
  for (std::uint32_t index = 0; index < kResourceCount; ++index) {
    ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                              resources[index].put()),
              S_OK)
        << "resource_index=" << index;
    ASSERT_EQ(resources[index]->QueryInterface(
                  __uuidof(IDXGIResource),
                  reinterpret_cast<void **>(dxgi_resources[index].put())),
              S_OK)
        << "resource_index=" << index;
    ASSERT_EQ(resources[index]->GetEvictionPriority(),
              DXGI_RESOURCE_PRIORITY_NORMAL)
        << "resource_index=" << index;
    UINT dxgi_priority = 0;
    ASSERT_EQ(dxgi_resources[index]->GetEvictionPriority(&dxgi_priority), S_OK)
        << "resource_index=" << index;
    ASSERT_EQ(dxgi_priority, DXGI_RESOURCE_PRIORITY_NORMAL)
        << "resource_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kEvictionPriorityCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const EvictionPriorityCase test_case = CaseForLogical(logical);
    const std::uint32_t guard_index =
        (test_case.resource_index + 1u) % kResourceCount;

    std::array<UINT, kPriorityHistoryLength> d3d_returned = {};
    std::array<UINT, kPriorityHistoryLength> dxgi_returned = {};
    std::array<HRESULT, kPriorityHistoryLength> dxgi_results = {};
    bool history_matches = true;
    for (UINT step = 0; step < kPriorityHistoryLength; ++step) {
      if (step == 1) {
        dxgi_results[step] =
            dxgi_resources[test_case.resource_index]->SetEvictionPriority(
                test_case.history[step]);
      } else {
        resources[test_case.resource_index]->SetEvictionPriority(
            test_case.history[step]);
        dxgi_results[step] = S_OK;
      }
      d3d_returned[step] =
          resources[test_case.resource_index]->GetEvictionPriority();
      const HRESULT get_result =
          dxgi_resources[test_case.resource_index]->GetEvictionPriority(
              &dxgi_returned[step]);
      history_matches = history_matches && dxgi_results[step] == S_OK &&
                        get_result == S_OK &&
                        d3d_returned[step] == test_case.history[step] &&
                        dxgi_returned[step] == test_case.history[step];
    }

    const HRESULT reset_result =
        dxgi_resources[test_case.resource_index]->SetEvictionPriority(
            DXGI_RESOURCE_PRIORITY_NORMAL);
    const UINT reset_d3d =
        resources[test_case.resource_index]->GetEvictionPriority();
    UINT reset_dxgi = 0;
    const HRESULT reset_get_result =
        dxgi_resources[test_case.resource_index]->GetEvictionPriority(
            &reset_dxgi);
    UINT guard_dxgi = 0;
    const UINT guard_d3d = resources[guard_index]->GetEvictionPriority();
    const HRESULT guard_get_result =
        dxgi_resources[guard_index]->GetEvictionPriority(&guard_dxgi);
    const bool reset_matches = reset_result == S_OK &&
                               reset_get_result == S_OK &&
                               reset_d3d == DXGI_RESOURCE_PRIORITY_NORMAL &&
                               reset_dxgi == DXGI_RESOURCE_PRIORITY_NORMAL;
    const bool guard_matches = guard_get_result == S_OK &&
                               guard_d3d == DXGI_RESOURCE_PRIORITY_NORMAL &&
                               guard_dxgi == DXGI_RESOURCE_PRIORITY_NORMAL;
    if (history_matches && reset_matches && guard_matches)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kEvictionPriorityCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kEvictionPriorityCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 shader_model=None "
                     "queue=Immediate capability=D3D11ResourceEvictionPriority,"
                     "DXGIResourceEvictionPriority\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kEvictionPriorityCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " resource_index=" << test_case.resource_index
                  << " history_index=" << test_case.history_index
                  << " guard_index=" << guard_index
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Observed: expected_history=(" << test_case.history[0]
                  << ',' << test_case.history[1] << ',' << test_case.history[2]
                  << ") d3d_history=(" << d3d_returned[0] << ','
                  << d3d_returned[1] << ',' << d3d_returned[2]
                  << ") dxgi_history=(" << dxgi_returned[0] << ','
                  << dxgi_returned[1] << ',' << dxgi_returned[2]
                  << ") reset_d3d=" << reset_d3d << " reset_dxgi=" << reset_dxgi
                  << " guard_d3d=" << guard_d3d << " guard_dxgi=" << guard_dxgi
                  << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  for (auto &resource : resources)
    resource->SetEvictionPriority(DXGI_RESOURCE_PRIORITY_NORMAL);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
