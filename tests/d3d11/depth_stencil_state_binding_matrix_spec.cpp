#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 output-merger depth-stencil state coverage. Sixty-four distinct
// immutable state objects combine with 64 stencil references for 4096 cases.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kDepthStencilBindingCaseCount = 4096;
constexpr std::uint32_t kStateCount = 64;

const dxmt::test::LogicalCaseFamilyRegistration kDepthStencilBindingCases(
    "D3D11DepthStencilStateBindingMatrixSpec."
    "RoundTrips4096StateAndStencilRefPairs",
    "D3D11.OMGetDepthStencilState.Binding.", kDepthStencilBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "CreateDepthStencilState,OMSetDepthStencilState,OMGetDepthStencilState,"
      "ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "sixty-four test-local depth-stencil state objects with distinct stencil "
     "read and write masks",
     "bind every selected immutable-state and dynamic stencil-reference pair, "
     "query and release the returned COM interface, then bind the default "
     "state",
     "the getter returns the exact state object and stencil reference, then a "
     "null state and zero reference after default-state binding",
     "logical ID, selected-case count, state index, masks, reference, failure "
     "phase, expected and actual addresses and values, and exact "
     "replay argument"});

const dxmt::test::TestCostRegistration
    kDepthStencilBindingCost("D3D11DepthStencilStateBindingMatrixSpec."
                             "RoundTrips4096StateAndStencilRefPairs",
                             dxmt::test::kResourceTestCost);

struct DepthStencilBinding {
  std::uint32_t state_index;
  UINT stencil_ref;
};

DepthStencilBinding BindingForCase(std::uint32_t logical) {
  const std::uint32_t state_index = logical & 63u;
  const std::uint32_t reference_index = (logical >> 6u) & 63u;
  return {state_index, reference_index * 4u + (state_index & 3u)};
}

D3D11_DEPTH_STENCIL_DESC StateDesc(std::uint32_t index) {
  D3D11_DEPTH_STENCIL_DESC desc = {};
  desc.DepthEnable = TRUE;
  desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  desc.StencilEnable = TRUE;
  desc.StencilReadMask = static_cast<UINT8>(index + 1u);
  desc.StencilWriteMask = static_cast<UINT8>(0xffu - index);
  desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
  desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR_SAT;
  desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
  desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
  desc.BackFace.StencilFailOp = D3D11_STENCIL_OP_ZERO;
  desc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR_SAT;
  desc.BackFace.StencilPassOp = D3D11_STENCIL_OP_INVERT;
  desc.BackFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
  return desc;
}

class D3D11DepthStencilStateBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DepthStencilStateBindingMatrixSpec,
       RoundTrips4096StateAndStencilRefPairs) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kDepthStencilBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kDepthStencilBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kDepthStencilBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  std::array<ComPtr<ID3D11DepthStencilState>, kStateCount> states;
  std::array<D3D11_DEPTH_STENCIL_DESC, kStateCount> descriptions;
  for (std::uint32_t index = 0; index < kStateCount; ++index) {
    descriptions[index] = StateDesc(index);
    ASSERT_EQ(context_.device()->CreateDepthStencilState(&descriptions[index],
                                                         states[index].put()),
              S_OK)
        << "state_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kDepthStencilBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const DepthStencilBinding binding = BindingForCase(logical);
    context_.context()->OMSetDepthStencilState(
        states[binding.state_index].get(), binding.stencil_ref);

    ID3D11DepthStencilState *actual_state = nullptr;
    UINT actual_ref = ~0u;
    context_.context()->OMGetDepthStencilState(&actual_state, &actual_ref);
    const void *actual_address = actual_state;
    const bool state_matches =
        actual_state == states[binding.state_index].get();
    const bool reference_matches = actual_ref == binding.stencil_ref;
    if (actual_state)
      actual_state->Release();

    context_.context()->OMSetDepthStencilState(nullptr, 0);
    actual_state = nullptr;
    UINT unbound_ref = ~0u;
    context_.context()->OMGetDepthStencilState(&actual_state, &unbound_ref);
    const void *unbound_address = actual_state;
    const bool default_matches = actual_state == nullptr && unbound_ref == 0;
    if (actual_state)
      actual_state->Release();

    if (state_matches && reference_matches && default_matches)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kDepthStencilBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kDepthStencilBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None "
           "queue=Immediate capability=CreateDepthStencilState,"
           "OMSetDepthStencilState,OMGetDepthStencilState,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kDepthStencilBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " state_index=" << binding.state_index << " stencil_read_mask="
        << static_cast<UINT>(descriptions[binding.state_index].StencilReadMask)
        << " stencil_write_mask="
        << static_cast<UINT>(descriptions[binding.state_index].StencilWriteMask)
        << " stencil_ref=" << binding.stencil_ref
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: expected_state=" << states[binding.state_index].get()
        << " actual_state=" << actual_address
        << " expected_ref=" << binding.stencil_ref
        << " actual_ref=" << actual_ref << " default_state=" << unbound_address
        << " default_ref=" << unbound_ref << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  context_.context()->OMSetDepthStencilState(nullptr, 0);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
