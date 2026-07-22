#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 output-merger blend-state coverage. Sixty-four distinct
// immutable blend objects combine with 64 blend-factor/sample-mask states.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kBlendBindingCaseCount = 4096;
constexpr std::uint32_t kBlendStateCount = 64;

const dxmt::test::LogicalCaseFamilyRegistration kBlendBindingCases(
    "D3D11BlendStateBindingMatrixSpec."
    "RoundTrips4096StateFactorAndMaskCombinations",
    "D3D11.OMGetBlendState.Binding.", kBlendBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "CreateBlendState,OMSetBlendState,OMGetBlendState,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "sixty-four test-local blend-state objects with every pairing from two "
     "sets of eight valid color blend factors",
     "bind every selected immutable state, four-component blend factor and "
     "32-bit sample mask, query and release the returned COM interface, then "
     "bind the documented default state",
     "the getter returns the exact state object, factor and mask, then a null "
     "state, all-one factor and all-one mask after default-state binding",
     "logical ID, selected-case count, state index, source and destination "
     "factors, expected and actual dynamic state, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kBlendBindingCost("D3D11BlendStateBindingMatrixSpec."
                      "RoundTrips4096StateFactorAndMaskCombinations",
                      dxmt::test::kResourceTestCost);

constexpr std::array<D3D11_BLEND, 8> kColorBlendFactors = {
    D3D11_BLEND_ZERO,       D3D11_BLEND_ONE,
    D3D11_BLEND_SRC_COLOR,  D3D11_BLEND_INV_SRC_COLOR,
    D3D11_BLEND_SRC_ALPHA,  D3D11_BLEND_INV_SRC_ALPHA,
    D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA,
};

D3D11_BLEND_DESC StateDesc(std::uint32_t index) {
  D3D11_BLEND_DESC desc = {};
  desc.RenderTarget[0].BlendEnable = TRUE;
  desc.RenderTarget[0].SrcBlend = kColorBlendFactors[index & 7u];
  desc.RenderTarget[0].DestBlend = kColorBlendFactors[(index >> 3u) & 7u];
  desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  return desc;
}

struct BlendBinding {
  std::uint32_t state_index;
  std::array<FLOAT, 4> factor;
  UINT sample_mask;
};

BlendBinding BindingForCase(std::uint32_t logical) {
  const std::uint32_t state_index = logical & 63u;
  const std::uint32_t dynamic_index = (logical >> 6u) & 63u;
  return {state_index,
          {{static_cast<FLOAT>(dynamic_index & 3u) * 0.25f,
            static_cast<FLOAT>((dynamic_index >> 2u) & 3u) * 0.25f,
            static_cast<FLOAT>((dynamic_index >> 4u) & 3u) * 0.25f,
            static_cast<FLOAT>((dynamic_index ^ state_index) & 3u) * 0.25f}},
          0x9e3779b9u * (dynamic_index + 1u) ^
              0x85ebca6bu * (state_index + 1u)};
}

class D3D11BlendStateBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11BlendStateBindingMatrixSpec,
       RoundTrips4096StateFactorAndMaskCombinations) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kBlendBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kBlendBindingCaseCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kBlendBindingCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  std::array<ComPtr<ID3D11BlendState>, kBlendStateCount> states;
  std::array<D3D11_BLEND_DESC, kBlendStateCount> descriptions;
  for (std::uint32_t index = 0; index < kBlendStateCount; ++index) {
    descriptions[index] = StateDesc(index);
    ASSERT_EQ(context_.device()->CreateBlendState(&descriptions[index],
                                                  states[index].put()),
              S_OK)
        << "state_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kBlendBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const BlendBinding binding = BindingForCase(logical);
    context_.context()->OMSetBlendState(states[binding.state_index].get(),
                                        binding.factor.data(),
                                        binding.sample_mask);

    ID3D11BlendState *actual_state = nullptr;
    std::array<FLOAT, 4> actual_factor = {};
    UINT actual_mask = 0;
    context_.context()->OMGetBlendState(&actual_state, actual_factor.data(),
                                        &actual_mask);
    const void *actual_address = actual_state;
    const bool state_matches =
        actual_state == states[binding.state_index].get();
    const bool factor_matches = actual_factor == binding.factor;
    const bool mask_matches = actual_mask == binding.sample_mask;
    if (actual_state)
      actual_state->Release();

    context_.context()->OMSetBlendState(nullptr, nullptr, ~0u);
    actual_state = nullptr;
    std::array<FLOAT, 4> default_factor = {};
    UINT default_mask = 0;
    context_.context()->OMGetBlendState(&actual_state, default_factor.data(),
                                        &default_mask);
    const void *default_address = actual_state;
    const std::array<FLOAT, 4> expected_default_factor = {1.0f, 1.0f, 1.0f,
                                                          1.0f};
    const bool default_matches = actual_state == nullptr &&
                                 default_factor == expected_default_factor &&
                                 default_mask == ~0u;
    if (actual_state)
      actual_state->Release();

    if (state_matches && factor_matches && mask_matches && default_matches)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kBlendBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kBlendBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None "
           "queue=Immediate capability=CreateBlendState,OMSetBlendState,"
           "OMGetBlendState,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kBlendBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " state_index=" << binding.state_index << " src_blend="
        << descriptions[binding.state_index].RenderTarget[0].SrcBlend
        << " dest_blend="
        << descriptions[binding.state_index].RenderTarget[0].DestBlend
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: expected_state=" << states[binding.state_index].get()
        << " actual_state=" << actual_address << " expected_factor=("
        << binding.factor[0] << ',' << binding.factor[1] << ','
        << binding.factor[2] << ',' << binding.factor[3] << ") actual_factor=("
        << actual_factor[0] << ',' << actual_factor[1] << ','
        << actual_factor[2] << ',' << actual_factor[3]
        << ") expected_mask=" << binding.sample_mask
        << " actual_mask=" << actual_mask
        << " default_state=" << default_address << " default_factor=("
        << default_factor[0] << ',' << default_factor[1] << ','
        << default_factor[2] << ',' << default_factor[3]
        << ") default_mask=" << default_mask << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  context_.context()->OMSetBlendState(nullptr, nullptr, ~0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
