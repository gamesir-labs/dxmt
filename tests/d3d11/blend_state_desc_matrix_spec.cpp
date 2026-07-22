#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 blend-state creation and descriptor coverage. The render-target
// zero fields below form exactly 4096 distinct creation descriptions.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kBlendStateDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kBlendStateDescCases(
    "D3D11BlendStateDescMatrixSpec."
    "RoundTrips4096DescriptionsWithDisabledFieldNormalization",
    "D3D11.BlendState.Description.", kBlendStateDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateBlendState,ID3D11BlendStateGetDesc,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live pair of blend-state COM "
     "references per selected logical case",
     "create every combination of alpha-to-coverage, blend enablement, eight "
     "source factors, eight destination factors, four RGB operations, two "
     "alpha profiles, and two write masks on an independently blended render "
     "target; query every descriptor field and recreate the description while "
     "it remains live",
     "GetDesc returns every enabled field and resets factors ignored by a "
     "disabled render target to their documented defaults; recreating an "
     "identical state returns the same COM interface",
     "logical ID, selected-case count, combination indexes, expected and "
     "returned render-target fields, failing target, object addresses, "
     "HRESULTs, failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration kBlendStateDescCost(
    "D3D11BlendStateDescMatrixSpec."
    "RoundTrips4096DescriptionsWithDisabledFieldNormalization",
    dxmt::test::kResourceTestCost);

constexpr std::array<D3D11_BLEND, 8> kSourceFactors = {
    D3D11_BLEND_ZERO,       D3D11_BLEND_ONE,
    D3D11_BLEND_SRC_COLOR,  D3D11_BLEND_INV_SRC_COLOR,
    D3D11_BLEND_SRC_ALPHA,  D3D11_BLEND_INV_SRC_ALPHA,
    D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA,
};

constexpr std::array<D3D11_BLEND, 8> kDestinationFactors = {
    D3D11_BLEND_ZERO,       D3D11_BLEND_ONE,
    D3D11_BLEND_SRC_COLOR,  D3D11_BLEND_INV_SRC_COLOR,
    D3D11_BLEND_SRC_ALPHA,  D3D11_BLEND_INV_SRC_ALPHA,
    D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA,
};

constexpr std::array<D3D11_BLEND_OP, 4> kBlendOps = {
    D3D11_BLEND_OP_ADD,
    D3D11_BLEND_OP_SUBTRACT,
    D3D11_BLEND_OP_REV_SUBTRACT,
    D3D11_BLEND_OP_MAX,
};

struct AlphaProfile {
  D3D11_BLEND source;
  D3D11_BLEND destination;
  D3D11_BLEND_OP operation;
};

constexpr std::array<AlphaProfile, 2> kAlphaProfiles = {{
    {D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD},
    {D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
     D3D11_BLEND_OP_REV_SUBTRACT},
}};

constexpr std::array<UINT8, 2> kWriteMasks = {
    D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN,
    D3D11_COLOR_WRITE_ENABLE_BLUE | D3D11_COLOR_WRITE_ENABLE_ALPHA,
};

D3D11_RENDER_TARGET_BLEND_DESC DefaultRenderTargetDesc() {
  D3D11_RENDER_TARGET_BLEND_DESC desc = {};
  desc.BlendEnable = FALSE;
  desc.SrcBlend = D3D11_BLEND_ONE;
  desc.DestBlend = D3D11_BLEND_ZERO;
  desc.BlendOp = D3D11_BLEND_OP_ADD;
  desc.SrcBlendAlpha = D3D11_BLEND_ONE;
  desc.DestBlendAlpha = D3D11_BLEND_ZERO;
  desc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  return desc;
}

struct BlendStateDescCase {
  UINT alpha_to_coverage_index;
  UINT blend_enable_index;
  UINT source_index;
  UINT destination_index;
  UINT operation_index;
  UINT alpha_profile_index;
  UINT write_mask_index;
  D3D11_BLEND_DESC desc;
};

BlendStateDescCase CaseForLogical(std::uint32_t logical) {
  BlendStateDescCase test_case = {};
  std::uint32_t encoded = logical;
  test_case.alpha_to_coverage_index = encoded & 1u;
  encoded >>= 1u;
  test_case.blend_enable_index = encoded & 1u;
  encoded >>= 1u;
  test_case.source_index = encoded & 7u;
  encoded >>= 3u;
  test_case.destination_index = encoded & 7u;
  encoded >>= 3u;
  test_case.operation_index = encoded & 3u;
  encoded >>= 2u;
  test_case.alpha_profile_index = encoded & 1u;
  encoded >>= 1u;
  test_case.write_mask_index = encoded & 1u;

  test_case.desc.AlphaToCoverageEnable =
      test_case.alpha_to_coverage_index ? TRUE : FALSE;
  test_case.desc.IndependentBlendEnable = TRUE;
  for (auto &target : test_case.desc.RenderTarget)
    target = DefaultRenderTargetDesc();

  D3D11_RENDER_TARGET_BLEND_DESC &target = test_case.desc.RenderTarget[0];
  const AlphaProfile &alpha = kAlphaProfiles[test_case.alpha_profile_index];
  target.BlendEnable = test_case.blend_enable_index ? TRUE : FALSE;
  target.SrcBlend = kSourceFactors[test_case.source_index];
  target.DestBlend = kDestinationFactors[test_case.destination_index];
  target.BlendOp = kBlendOps[test_case.operation_index];
  target.SrcBlendAlpha = alpha.source;
  target.DestBlendAlpha = alpha.destination;
  target.BlendOpAlpha = alpha.operation;
  target.RenderTargetWriteMask = kWriteMasks[test_case.write_mask_index];
  return test_case;
}

D3D11_BLEND_DESC ExpectedDesc(const D3D11_BLEND_DESC &created) {
  D3D11_BLEND_DESC expected = created;
  for (UINT index = 0; index < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
       ++index) {
    const D3D11_RENDER_TARGET_BLEND_DESC source =
        created.RenderTarget[created.IndependentBlendEnable ? index : 0];
    D3D11_RENDER_TARGET_BLEND_DESC &target = expected.RenderTarget[index];
    target = source;
    target.BlendEnable = target.BlendEnable ? TRUE : FALSE;
    if (!target.BlendEnable) {
      target.SrcBlend = D3D11_BLEND_ONE;
      target.DestBlend = D3D11_BLEND_ZERO;
      target.BlendOp = D3D11_BLEND_OP_ADD;
      target.SrcBlendAlpha = D3D11_BLEND_ONE;
      target.DestBlendAlpha = D3D11_BLEND_ZERO;
      target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    }
  }
  expected.AlphaToCoverageEnable =
      expected.AlphaToCoverageEnable ? TRUE : FALSE;
  expected.IndependentBlendEnable =
      expected.IndependentBlendEnable ? TRUE : FALSE;
  return expected;
}

bool RenderTargetDescsEqual(const D3D11_RENDER_TARGET_BLEND_DESC &actual,
                            const D3D11_RENDER_TARGET_BLEND_DESC &expected) {
  return actual.BlendEnable == expected.BlendEnable &&
         actual.SrcBlend == expected.SrcBlend &&
         actual.DestBlend == expected.DestBlend &&
         actual.BlendOp == expected.BlendOp &&
         actual.SrcBlendAlpha == expected.SrcBlendAlpha &&
         actual.DestBlendAlpha == expected.DestBlendAlpha &&
         actual.BlendOpAlpha == expected.BlendOpAlpha &&
         actual.RenderTargetWriteMask == expected.RenderTargetWriteMask;
}

UINT FirstMismatchedTarget(const D3D11_BLEND_DESC &actual,
                           const D3D11_BLEND_DESC &expected) {
  if (actual.AlphaToCoverageEnable != expected.AlphaToCoverageEnable ||
      actual.IndependentBlendEnable != expected.IndependentBlendEnable)
    return D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
  for (UINT index = 0; index < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
       ++index) {
    if (!RenderTargetDescsEqual(actual.RenderTarget[index],
                                expected.RenderTarget[index]))
      return index;
  }
  return D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT + 1u;
}

class D3D11BlendStateDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11BlendStateDescMatrixSpec,
       RoundTrips4096DescriptionsWithDisabledFieldNormalization) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kBlendStateDescCaseCount);
  for (std::uint32_t logical = 0; logical < kBlendStateDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kBlendStateDescCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kBlendStateDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const BlendStateDescCase test_case = CaseForLogical(logical);
    const D3D11_BLEND_DESC expected = ExpectedDesc(test_case.desc);
    ComPtr<ID3D11BlendState> state;
    ComPtr<ID3D11BlendState> duplicate;
    const HRESULT create_result =
        context_.device()->CreateBlendState(&test_case.desc, state.put());
    HRESULT duplicate_result = E_FAIL;
    D3D11_BLEND_DESC actual = {};
    if (create_result == S_OK && state) {
      state->GetDesc(&actual);
      duplicate_result =
          context_.device()->CreateBlendState(&test_case.desc, duplicate.put());
    }

    const UINT failing_target = FirstMismatchedTarget(actual, expected);
    const bool desc_matches =
        create_result == S_OK && state &&
        failing_target == D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT + 1u;
    const bool identity_matches =
        duplicate_result == S_OK && duplicate && duplicate.get() == state.get();
    if (desc_matches && identity_matches)
      continue;

    const char *failure_phase = "identity";
    if (create_result != S_OK || !state)
      failure_phase = "create";
    else if (!desc_matches)
      failure_phase = "get_desc";
    else if (duplicate_result != S_OK || !duplicate)
      failure_phase = "recreate";
    const UINT reported_target =
        failing_target < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT ? failing_target
                                                                : 0u;
    const auto &expected_target = expected.RenderTarget[reported_target];
    const auto &actual_target = actual.RenderTarget[reported_target];

    const auto case_id =
        dxmt::test::LogicalCaseId(kBlendStateDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kBlendStateDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateBlendState,BlendStateGetDesc,ComObjectIdentity\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kBlendStateDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " alpha_to_coverage_index=" << test_case.alpha_to_coverage_index
        << " blend_enable_index=" << test_case.blend_enable_index
        << " source_index=" << test_case.source_index
        << " destination_index=" << test_case.destination_index
        << " operation_index=" << test_case.operation_index
        << " alpha_profile_index=" << test_case.alpha_profile_index
        << " write_mask_index=" << test_case.write_mask_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: alpha_to_coverage=" << expected.AlphaToCoverageEnable
        << " independent=" << expected.IndependentBlendEnable
        << " target=" << reported_target << " fields=("
        << expected_target.BlendEnable << ','
        << static_cast<UINT>(expected_target.SrcBlend) << ','
        << static_cast<UINT>(expected_target.DestBlend) << ','
        << static_cast<UINT>(expected_target.BlendOp) << ','
        << static_cast<UINT>(expected_target.SrcBlendAlpha) << ','
        << static_cast<UINT>(expected_target.DestBlendAlpha) << ','
        << static_cast<UINT>(expected_target.BlendOpAlpha) << ','
        << static_cast<UINT>(expected_target.RenderTargetWriteMask) << ")\n"
        << "Observed: create_hresult=" << create_result
        << " duplicate_hresult=" << duplicate_result
        << " failure_phase=" << failure_phase
        << " failing_target=" << failing_target
        << " alpha_to_coverage=" << actual.AlphaToCoverageEnable
        << " independent=" << actual.IndependentBlendEnable << " fields=("
        << actual_target.BlendEnable << ','
        << static_cast<UINT>(actual_target.SrcBlend) << ','
        << static_cast<UINT>(actual_target.DestBlend) << ','
        << static_cast<UINT>(actual_target.BlendOp) << ','
        << static_cast<UINT>(actual_target.SrcBlendAlpha) << ','
        << static_cast<UINT>(actual_target.DestBlendAlpha) << ','
        << static_cast<UINT>(actual_target.BlendOpAlpha) << ','
        << static_cast<UINT>(actual_target.RenderTargetWriteMask)
        << ") state=" << state.get() << " duplicate=" << duplicate.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11BlendStateDescMatrixSpec,
       ReusesStatesWhoseIgnoredFieldsNormalizeIdentically) {
  D3D11_BLEND_DESC first_desc = {};
  first_desc.IndependentBlendEnable = FALSE;
  for (auto &target : first_desc.RenderTarget)
    target = DefaultRenderTargetDesc();
  first_desc.RenderTarget[0].BlendEnable = FALSE;
  first_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_COLOR;
  first_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_DEST_ALPHA;
  first_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_SUBTRACT;
  first_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
  first_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  first_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
  first_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_RED;
  first_desc.RenderTarget[1].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_GREEN;

  D3D11_BLEND_DESC second_desc = first_desc;
  second_desc.RenderTarget[0] = DefaultRenderTargetDesc();
  second_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_RED;
  second_desc.RenderTarget[1].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_BLUE;

  ComPtr<ID3D11BlendState> first;
  ComPtr<ID3D11BlendState> second;
  ASSERT_EQ(context_.device()->CreateBlendState(&first_desc, first.put()),
            S_OK);
  ASSERT_EQ(context_.device()->CreateBlendState(&second_desc, second.put()),
            S_OK);
  ASSERT_NE(first.get(), nullptr);
  EXPECT_EQ(second.get(), first.get());

  D3D11_BLEND_DESC actual = {};
  first->GetDesc(&actual);
  EXPECT_EQ(FirstMismatchedTarget(actual, ExpectedDesc(first_desc)),
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT + 1u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11BlendStateDescMatrixSpec,
       HandlesInvalidDescriptionAndValidationOnlyCreation) {
  ID3D11BlendState *state =
      reinterpret_cast<ID3D11BlendState *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateBlendState(nullptr, &state), E_INVALIDARG);
  EXPECT_EQ(state, nullptr);

  D3D11_BLEND_DESC desc = {};
  for (auto &target : desc.RenderTarget)
    target = DefaultRenderTargetDesc();
  EXPECT_EQ(context_.device()->CreateBlendState(&desc, nullptr), S_FALSE);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
