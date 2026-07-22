#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 input-assembler topology coverage. Forty-two documented D3D11
// topology values form 4096 distinct three-step binding histories.

namespace {

using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kPrimitiveTopologyCaseCount = 4096;
constexpr UINT kTopologyCount = 42;
constexpr UINT kSequenceLength = 3;

const dxmt::test::LogicalCaseFamilyRegistration kPrimitiveTopologyCases(
    "D3D11PrimitiveTopologyStateMatrixSpec."
    "RoundTrips4096DistinctThreeStepHistories",
    "D3D11.IAGetPrimitiveTopology.State.", kPrimitiveTopologyCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "IASetPrimitiveTopology,IAGetPrimitiveTopology"},
     dxmt::test::kNormalTestCost,
     "one test-local immediate context and all forty-two documented D3D11 "
     "primitive-topology values, including every patch-list size",
     "bind and query each step of 4096 injectively encoded three-topology "
     "histories, then restore and query the undefined topology",
     "every getter returns the exact most recently bound valid topology and "
     "returns undefined after the explicit reset",
     "logical ID, selected-case count, encoded history, expected and actual "
     "topology values for all steps, reset result, failure phase, and exact "
     "replay argument"});

const dxmt::test::TestCostRegistration
    kPrimitiveTopologyCost("D3D11PrimitiveTopologyStateMatrixSpec."
                           "RoundTrips4096DistinctThreeStepHistories",
                           dxmt::test::kNormalTestCost);

constexpr std::array<D3D11_PRIMITIVE_TOPOLOGY, 10> kNonPatchTopologies = {
    D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,
    D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
    D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
    D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
    D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,
    D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ,
};

D3D11_PRIMITIVE_TOPOLOGY TopologyForIndex(UINT index) {
  if (index < kNonPatchTopologies.size())
    return kNonPatchTopologies[index];
  return static_cast<D3D11_PRIMITIVE_TOPOLOGY>(
      D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + index -
      kNonPatchTopologies.size());
}

std::array<D3D11_PRIMITIVE_TOPOLOGY, kSequenceLength>
SequenceForCase(std::uint32_t logical) {
  std::uint32_t encoded = logical * 17u;
  std::array<D3D11_PRIMITIVE_TOPOLOGY, kSequenceLength> sequence;
  for (UINT step = 0; step < kSequenceLength; ++step) {
    sequence[step] = TopologyForIndex(encoded % kTopologyCount);
    encoded /= kTopologyCount;
  }
  return sequence;
}

class D3D11PrimitiveTopologyStateMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11PrimitiveTopologyStateMatrixSpec,
       RoundTrips4096DistinctThreeStepHistories) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kPrimitiveTopologyCaseCount);
  for (std::uint32_t logical = 0; logical < kPrimitiveTopologyCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kPrimitiveTopologyCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kPrimitiveTopologyCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const auto expected = SequenceForCase(logical);
    std::array<D3D11_PRIMITIVE_TOPOLOGY, kSequenceLength> actual = {};
    bool sequence_matches = true;
    for (UINT step = 0; step < kSequenceLength; ++step) {
      context_.context()->IASetPrimitiveTopology(expected[step]);
      actual[step] = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      context_.context()->IAGetPrimitiveTopology(&actual[step]);
      sequence_matches = sequence_matches && actual[step] == expected[step];
    }

    context_.context()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
    D3D11_PRIMITIVE_TOPOLOGY reset = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    context_.context()->IAGetPrimitiveTopology(&reset);
    const bool reset_matches = reset == D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    if (sequence_matches && reset_matches)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kPrimitiveTopologyCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kPrimitiveTopologyCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 shader_model=None "
                     "queue=Immediate capability=IASetPrimitiveTopology,"
                     "IAGetPrimitiveTopology\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kPrimitiveTopologyCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " encoded=" << logical * 17u
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Observed: expected_sequence=("
                  << static_cast<UINT>(expected[0]) << ','
                  << static_cast<UINT>(expected[1]) << ','
                  << static_cast<UINT>(expected[2]) << ") actual_sequence=("
                  << static_cast<UINT>(actual[0]) << ','
                  << static_cast<UINT>(actual[1]) << ','
                  << static_cast<UINT>(actual[2]) << ") expected_reset="
                  << static_cast<UINT>(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)
                  << " actual_reset=" << static_cast<UINT>(reset) << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  context_.context()->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
