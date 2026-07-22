#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 rasterizer-state coverage. Every one of 64 immutable states is
// replaced by every state, exercising 4096 ordered state transitions.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kRasterizerBindingCaseCount = 4096;
constexpr std::uint32_t kRasterizerStateCount = 64;

const dxmt::test::LogicalCaseFamilyRegistration kRasterizerBindingCases(
    "D3D11RasterizerStateBindingMatrixSpec."
    "RoundTrips4096OrderedStateTransitions",
    "D3D11.RSGetState.Binding.", kRasterizerBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "CreateRasterizerState,RSSetState,RSGetState,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "sixty-four test-local rasterizer-state objects with distinct signed "
     "depth-bias values",
     "bind and query every ordered pair of previous and replacement states, "
     "release each getter reference, then bind the default state",
     "each getter returns the exact most recently bound state object, and the "
     "getter returns null after default-state binding",
     "logical ID, selected-case count, previous and replacement state "
     "indices and depth biases, expected and actual addresses, failure phase, "
     "and exact replay argument"});

const dxmt::test::TestCostRegistration
    kRasterizerBindingCost("D3D11RasterizerStateBindingMatrixSpec."
                           "RoundTrips4096OrderedStateTransitions",
                           dxmt::test::kResourceTestCost);

struct RasterizerTransition {
  std::uint32_t previous_state_index;
  std::uint32_t replacement_state_index;
};

RasterizerTransition TransitionForCase(std::uint32_t logical) {
  return {logical & 63u, (logical >> 6u) & 63u};
}

D3D11_RASTERIZER_DESC StateDesc(std::uint32_t index) {
  D3D11_RASTERIZER_DESC desc = {};
  desc.FillMode = D3D11_FILL_SOLID;
  desc.CullMode = D3D11_CULL_BACK;
  desc.FrontCounterClockwise = FALSE;
  desc.DepthBias = static_cast<INT>(index) - 32;
  desc.DepthBiasClamp = 0.0f;
  desc.SlopeScaledDepthBias = 0.0f;
  desc.DepthClipEnable = TRUE;
  desc.ScissorEnable = FALSE;
  desc.MultisampleEnable = FALSE;
  desc.AntialiasedLineEnable = FALSE;
  return desc;
}

class D3D11RasterizerStateBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11RasterizerStateBindingMatrixSpec,
       RoundTrips4096OrderedStateTransitions) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kRasterizerBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kRasterizerBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kRasterizerBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  std::array<ComPtr<ID3D11RasterizerState>, kRasterizerStateCount> states;
  std::array<D3D11_RASTERIZER_DESC, kRasterizerStateCount> descriptions;
  for (std::uint32_t index = 0; index < kRasterizerStateCount; ++index) {
    descriptions[index] = StateDesc(index);
    ASSERT_EQ(context_.device()->CreateRasterizerState(&descriptions[index],
                                                       states[index].put()),
              S_OK)
        << "state_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kRasterizerBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const RasterizerTransition transition = TransitionForCase(logical);

    context_.context()->RSSetState(
        states[transition.previous_state_index].get());
    ID3D11RasterizerState *actual_previous = nullptr;
    context_.context()->RSGetState(&actual_previous);
    const void *actual_previous_address = actual_previous;
    const bool previous_matches =
        actual_previous == states[transition.previous_state_index].get();
    if (actual_previous)
      actual_previous->Release();

    context_.context()->RSSetState(
        states[transition.replacement_state_index].get());
    ID3D11RasterizerState *actual_replacement = nullptr;
    context_.context()->RSGetState(&actual_replacement);
    const void *actual_replacement_address = actual_replacement;
    const bool replacement_matches =
        actual_replacement == states[transition.replacement_state_index].get();
    if (actual_replacement)
      actual_replacement->Release();

    context_.context()->RSSetState(nullptr);
    ID3D11RasterizerState *actual_default = nullptr;
    context_.context()->RSGetState(&actual_default);
    const void *actual_default_address = actual_default;
    const bool default_matches = actual_default == nullptr;
    if (actual_default)
      actual_default->Release();

    if (previous_matches && replacement_matches && default_matches)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kRasterizerBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kRasterizerBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None "
           "queue=Immediate capability=CreateRasterizerState,RSSetState,"
           "RSGetState,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kRasterizerBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " previous_state_index=" << transition.previous_state_index
        << " previous_depth_bias="
        << descriptions[transition.previous_state_index].DepthBias
        << " replacement_state_index=" << transition.replacement_state_index
        << " replacement_depth_bias="
        << descriptions[transition.replacement_state_index].DepthBias
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: expected_previous="
        << states[transition.previous_state_index].get()
        << " actual_previous=" << actual_previous_address
        << " expected_replacement="
        << states[transition.replacement_state_index].get()
        << " actual_replacement=" << actual_replacement_address
        << " default_state=" << actual_default_address << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  context_.context()->RSSetState(nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
