#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 predication-state coverage. Sixty-four issued predicates form
// 4096 ordered object transitions with both predicate comparison values.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kPredicationBindingCaseCount = 4096;
constexpr std::uint32_t kPredicateCount = 64;

const dxmt::test::LogicalCaseFamilyRegistration kPredicationBindingCases(
    "D3D11PredicationBindingMatrixSpec."
    "RoundTrips4096OrderedPredicateTransitions",
    "D3D11.GetPredication.Binding.", kPredicationBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "CreatePredicate,Begin,End,SetPredication,GetPredication,"
      "ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "sixty-four test-local occlusion predicate objects issued with empty "
     "brackets before they are bound",
     "bind and query every ordered pair of previous and replacement "
     "predicates with both comparison values, release getter references, "
     "then bind null while preserving a selected comparison value",
     "each getter returns the exact most recently bound predicate and value, "
     "and null predication returns a null object while preserving its value",
     "logical ID, selected-case count, predicate indices and values, expected "
     "and actual addresses, null-state result, failure phase, and exact "
     "replay argument"});

const dxmt::test::TestCostRegistration
    kPredicationBindingCost("D3D11PredicationBindingMatrixSpec."
                            "RoundTrips4096OrderedPredicateTransitions",
                            dxmt::test::kResourceTestCost);

struct PredicationTransition {
  std::uint32_t previous_predicate_index;
  std::uint32_t replacement_predicate_index;
  BOOL previous_value;
  BOOL replacement_value;
  BOOL null_value;
};

PredicationTransition TransitionForCase(std::uint32_t logical) {
  const std::uint32_t previous = logical & 63u;
  const std::uint32_t replacement = (logical >> 6u) & 63u;
  return {previous, replacement, replacement & 1u ? TRUE : FALSE,
          previous & 1u ? TRUE : FALSE,
          (previous ^ replacement) & 1u ? TRUE : FALSE};
}

class D3D11PredicationBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11PredicationBindingMatrixSpec,
       RoundTrips4096OrderedPredicateTransitions) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kPredicationBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kPredicationBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kPredicationBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_QUERY_DESC predicate_desc = {};
  predicate_desc.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
  std::array<ComPtr<ID3D11Predicate>, kPredicateCount> predicates;
  for (std::uint32_t index = 0; index < kPredicateCount; ++index) {
    ASSERT_EQ(context_.device()->CreatePredicate(&predicate_desc,
                                                 predicates[index].put()),
              S_OK)
        << "predicate_index=" << index;
    for (std::uint32_t previous = 0; previous < index; ++previous) {
      ASSERT_NE(predicates[index].get(), predicates[previous].get())
          << "predicate_index=" << index
          << " previous_predicate_index=" << previous;
    }
    context_.context()->Begin(predicates[index].get());
    context_.context()->End(predicates[index].get());
  }
  context_.context()->Flush();

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kPredicationBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const PredicationTransition transition = TransitionForCase(logical);

    context_.context()->SetPredication(
        predicates[transition.previous_predicate_index].get(),
        transition.previous_value);
    ID3D11Predicate *actual_previous = nullptr;
    BOOL actual_previous_value = !transition.previous_value;
    context_.context()->GetPredication(&actual_previous,
                                       &actual_previous_value);
    const void *actual_previous_address = actual_previous;
    const bool previous_matches =
        actual_previous ==
            predicates[transition.previous_predicate_index].get() &&
        actual_previous_value == transition.previous_value;
    if (actual_previous)
      actual_previous->Release();

    context_.context()->SetPredication(
        predicates[transition.replacement_predicate_index].get(),
        transition.replacement_value);
    ID3D11Predicate *actual_replacement = nullptr;
    BOOL actual_replacement_value = !transition.replacement_value;
    context_.context()->GetPredication(&actual_replacement,
                                       &actual_replacement_value);
    const void *actual_replacement_address = actual_replacement;
    const bool replacement_matches =
        actual_replacement ==
            predicates[transition.replacement_predicate_index].get() &&
        actual_replacement_value == transition.replacement_value;
    if (actual_replacement)
      actual_replacement->Release();

    context_.context()->SetPredication(nullptr, transition.null_value);
    ID3D11Predicate *actual_null = nullptr;
    BOOL actual_null_value = !transition.null_value;
    context_.context()->GetPredication(&actual_null, &actual_null_value);
    const void *actual_null_address = actual_null;
    const bool null_matches =
        actual_null == nullptr && actual_null_value == transition.null_value;
    if (actual_null)
      actual_null->Release();

    if (previous_matches && replacement_matches && null_matches)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kPredicationBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kPredicationBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None "
           "queue=Immediate capability=CreatePredicate,Begin,End,"
           "SetPredication,GetPredication,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kPredicationBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " previous_predicate_index=" << transition.previous_predicate_index
        << " previous_value=" << transition.previous_value
        << " replacement_predicate_index="
        << transition.replacement_predicate_index
        << " replacement_value=" << transition.replacement_value
        << " null_value=" << transition.null_value
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: expected_previous="
        << predicates[transition.previous_predicate_index].get()
        << " actual_previous=" << actual_previous_address
        << " actual_previous_value=" << actual_previous_value
        << " expected_replacement="
        << predicates[transition.replacement_predicate_index].get()
        << " actual_replacement=" << actual_replacement_address
        << " actual_replacement_value=" << actual_replacement_value
        << " null_predicate=" << actual_null_address
        << " actual_null_value=" << actual_null_value << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  context_.context()->SetPredication(nullptr, FALSE);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
