#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 rasterizer-state creation and descriptor coverage. The ten
// descriptor dimensions below form exactly 4096 valid, distinct states.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kRasterizerStateDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kRasterizerStateDescCases(
    "D3D11RasterizerStateDescMatrixSpec."
    "RoundTrips4096DescriptionsAndReusesIdenticalStates",
    "D3D11.RasterizerState.Description.", kRasterizerStateDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateRasterizerState,ID3D11RasterizerStateGetDesc,"
      "ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live pair of rasterizer-state COM "
     "references per selected logical case",
     "create every combination of two fill modes, two cull modes, both "
     "front-face conventions, eight signed depth biases, two depth-bias "
     "clamps, two slope biases, and all four rasterization flags; query every "
     "descriptor field and recreate the same description while it remains "
     "live",
     "GetDesc exactly returns the public creation description and recreating "
     "an identical state returns the same COM interface",
     "logical ID, selected-case count, all expected and returned descriptor "
     "fields, object addresses, HRESULTs, failure phase, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration kRasterizerStateDescCost(
    "D3D11RasterizerStateDescMatrixSpec."
    "RoundTrips4096DescriptionsAndReusesIdenticalStates",
    dxmt::test::kResourceTestCost);

constexpr std::array<D3D11_FILL_MODE, 2> kFillModes = {
    D3D11_FILL_WIREFRAME,
    D3D11_FILL_SOLID,
};

constexpr std::array<D3D11_CULL_MODE, 2> kCullModes = {
    D3D11_CULL_FRONT,
    D3D11_CULL_BACK,
};

constexpr std::array<INT, 8> kDepthBiases = {
    -8192, -128, -1, 0, 1, 7, 128, 8192,
};

struct RasterizerStateDescCase {
  std::array<UINT, 10> indexes;
  D3D11_RASTERIZER_DESC desc;
};

RasterizerStateDescCase CaseForLogical(std::uint32_t logical) {
  RasterizerStateDescCase test_case = {};
  std::uint32_t encoded = logical;
  test_case.indexes[0] = encoded & 1u;
  encoded >>= 1u;
  test_case.indexes[1] = encoded & 1u;
  encoded >>= 1u;
  test_case.indexes[2] = encoded & 1u;
  encoded >>= 1u;
  test_case.indexes[3] = encoded & 7u;
  encoded >>= 3u;
  for (UINT index = 4; index < test_case.indexes.size(); ++index) {
    test_case.indexes[index] = encoded & 1u;
    encoded >>= 1u;
  }

  test_case.desc.FillMode = kFillModes[test_case.indexes[0]];
  test_case.desc.CullMode = kCullModes[test_case.indexes[1]];
  test_case.desc.FrontCounterClockwise = test_case.indexes[2] ? TRUE : FALSE;
  test_case.desc.DepthBias = kDepthBiases[test_case.indexes[3]];
  test_case.desc.DepthBiasClamp = test_case.indexes[4] ? 2.0f : 0.0f;
  test_case.desc.SlopeScaledDepthBias = test_case.indexes[5] ? 1.25f : 0.0f;
  test_case.desc.DepthClipEnable = test_case.indexes[6] ? TRUE : FALSE;
  test_case.desc.ScissorEnable = test_case.indexes[7] ? TRUE : FALSE;
  test_case.desc.MultisampleEnable = test_case.indexes[8] ? TRUE : FALSE;
  test_case.desc.AntialiasedLineEnable = test_case.indexes[9] ? TRUE : FALSE;
  return test_case;
}

bool RasterizerDescsEqual(const D3D11_RASTERIZER_DESC &actual,
                          const D3D11_RASTERIZER_DESC &expected) {
  return actual.FillMode == expected.FillMode &&
         actual.CullMode == expected.CullMode &&
         actual.FrontCounterClockwise == expected.FrontCounterClockwise &&
         actual.DepthBias == expected.DepthBias &&
         actual.DepthBiasClamp == expected.DepthBiasClamp &&
         actual.SlopeScaledDepthBias == expected.SlopeScaledDepthBias &&
         actual.DepthClipEnable == expected.DepthClipEnable &&
         actual.ScissorEnable == expected.ScissorEnable &&
         actual.MultisampleEnable == expected.MultisampleEnable &&
         actual.AntialiasedLineEnable == expected.AntialiasedLineEnable;
}

class D3D11RasterizerStateDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11RasterizerStateDescMatrixSpec,
       RoundTrips4096DescriptionsAndReusesIdenticalStates) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kRasterizerStateDescCaseCount);
  for (std::uint32_t logical = 0; logical < kRasterizerStateDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kRasterizerStateDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kRasterizerStateDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const RasterizerStateDescCase test_case = CaseForLogical(logical);
    ComPtr<ID3D11RasterizerState> state;
    ComPtr<ID3D11RasterizerState> duplicate;
    const HRESULT create_result =
        context_.device()->CreateRasterizerState(&test_case.desc, state.put());
    HRESULT duplicate_result = E_FAIL;
    D3D11_RASTERIZER_DESC actual = {};
    if (create_result == S_OK && state) {
      state->GetDesc(&actual);
      duplicate_result = context_.device()->CreateRasterizerState(
          &test_case.desc, duplicate.put());
    }

    const bool desc_matches = create_result == S_OK && state &&
                              RasterizerDescsEqual(actual, test_case.desc);
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

    const auto case_id =
        dxmt::test::LogicalCaseId(kRasterizerStateDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kRasterizerStateDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateRasterizerState,RasterizerStateGetDesc,"
           "ComObjectIdentity\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kRasterizerStateDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: fill=" << static_cast<UINT>(test_case.desc.FillMode)
        << " cull=" << static_cast<UINT>(test_case.desc.CullMode)
        << " front_ccw=" << test_case.desc.FrontCounterClockwise
        << " depth_bias=" << test_case.desc.DepthBias
        << " depth_bias_clamp=" << test_case.desc.DepthBiasClamp
        << " slope_bias=" << test_case.desc.SlopeScaledDepthBias
        << " depth_clip=" << test_case.desc.DepthClipEnable
        << " scissor=" << test_case.desc.ScissorEnable
        << " multisample=" << test_case.desc.MultisampleEnable
        << " aa_line=" << test_case.desc.AntialiasedLineEnable << '\n'
        << "Observed: create_hresult=" << create_result
        << " duplicate_hresult=" << duplicate_result
        << " failure_phase=" << failure_phase
        << " fill=" << static_cast<UINT>(actual.FillMode)
        << " cull=" << static_cast<UINT>(actual.CullMode)
        << " front_ccw=" << actual.FrontCounterClockwise
        << " depth_bias=" << actual.DepthBias
        << " depth_bias_clamp=" << actual.DepthBiasClamp
        << " slope_bias=" << actual.SlopeScaledDepthBias
        << " depth_clip=" << actual.DepthClipEnable
        << " scissor=" << actual.ScissorEnable
        << " multisample=" << actual.MultisampleEnable
        << " aa_line=" << actual.AntialiasedLineEnable
        << " state=" << state.get() << " duplicate=" << duplicate.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11RasterizerStateDescMatrixSpec,
       HandlesInvalidDescriptionAndValidationOnlyCreation) {
  ID3D11RasterizerState *state =
      reinterpret_cast<ID3D11RasterizerState *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateRasterizerState(nullptr, &state),
            E_INVALIDARG);
  EXPECT_EQ(state, nullptr);

  D3D11_RASTERIZER_DESC desc = {};
  desc.FillMode = D3D11_FILL_SOLID;
  desc.CullMode = D3D11_CULL_BACK;
  desc.DepthClipEnable = TRUE;
  EXPECT_EQ(context_.device()->CreateRasterizerState(&desc, nullptr), S_FALSE);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
