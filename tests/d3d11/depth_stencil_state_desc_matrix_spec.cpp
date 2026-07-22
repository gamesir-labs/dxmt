#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 depth-stencil state creation and descriptor coverage. The
// enable flags and state fields form 4096 distinct creation descriptions.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kDepthStencilStateDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kDepthStencilStateDescCases(
    "D3D11DepthStencilStateDescMatrixSpec."
    "RoundTrips4096DescriptionsWithDisabledFieldNormalization",
    "D3D11.DepthStencilState.Description.", kDepthStencilStateDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateDepthStencilState,ID3D11DepthStencilStateGetDesc,"
      "ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live pair of depth-stencil-state COM "
     "references per selected logical case",
     "create every combination of depth and stencil enablement, both depth "
     "write masks, all eight depth comparisons, four read masks, four write "
     "masks, and two front/back stencil profiles; query every descriptor "
     "field and recreate the same description while it remains live",
     "GetDesc returns every enabled field and resets fields ignored by "
     "disabled depth or stencil to their documented defaults; recreating an "
     "identical state returns the same COM interface",
     "logical ID, selected-case count, normalized descriptor fields, "
     "object addresses, HRESULTs, failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration kDepthStencilStateDescCost(
    "D3D11DepthStencilStateDescMatrixSpec."
    "RoundTrips4096DescriptionsWithDisabledFieldNormalization",
    dxmt::test::kResourceTestCost);

constexpr std::array<D3D11_COMPARISON_FUNC, 8> kComparisonFuncs = {
    D3D11_COMPARISON_NEVER,         D3D11_COMPARISON_LESS,
    D3D11_COMPARISON_EQUAL,         D3D11_COMPARISON_LESS_EQUAL,
    D3D11_COMPARISON_GREATER,       D3D11_COMPARISON_NOT_EQUAL,
    D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_ALWAYS,
};

constexpr std::array<UINT8, 4> kReadMasks = {0x00, 0x0f, 0xf0, 0xff};
constexpr std::array<UINT8, 4> kWriteMasks = {0x00, 0x33, 0xcc, 0xff};

constexpr D3D11_DEPTH_STENCILOP_DESC kDefaultStencilOp = {
    D3D11_STENCIL_OP_KEEP,
    D3D11_STENCIL_OP_KEEP,
    D3D11_STENCIL_OP_KEEP,
    D3D11_COMPARISON_ALWAYS,
};

constexpr std::array<D3D11_DEPTH_STENCILOP_DESC, 2> kFrontProfiles = {{
    kDefaultStencilOp,
    {D3D11_STENCIL_OP_ZERO, D3D11_STENCIL_OP_INCR_SAT, D3D11_STENCIL_OP_REPLACE,
     D3D11_COMPARISON_EQUAL},
}};

constexpr std::array<D3D11_DEPTH_STENCILOP_DESC, 2> kBackProfiles = {{
    kDefaultStencilOp,
    {D3D11_STENCIL_OP_INVERT, D3D11_STENCIL_OP_DECR_SAT, D3D11_STENCIL_OP_DECR,
     D3D11_COMPARISON_NOT_EQUAL},
}};

struct DepthStencilStateDescCase {
  BOOL depth_enable;
  BOOL stencil_enable;
  UINT depth_write_index;
  UINT depth_func_index;
  UINT read_mask_index;
  UINT write_mask_index;
  UINT front_profile_index;
  UINT back_profile_index;
  D3D11_DEPTH_STENCIL_DESC desc;
};

DepthStencilStateDescCase CaseForLogical(std::uint32_t logical) {
  DepthStencilStateDescCase test_case = {};
  std::uint32_t encoded = logical;
  test_case.depth_enable = (encoded & 1u) ? TRUE : FALSE;
  encoded >>= 1u;
  test_case.stencil_enable = (encoded & 1u) ? TRUE : FALSE;
  encoded >>= 1u;
  test_case.depth_write_index = encoded & 1u;
  encoded >>= 1u;
  test_case.depth_func_index = encoded & 7u;
  encoded >>= 3u;
  test_case.read_mask_index = encoded & 3u;
  encoded >>= 2u;
  test_case.write_mask_index = encoded & 3u;
  encoded >>= 2u;
  test_case.front_profile_index = encoded & 1u;
  encoded >>= 1u;
  test_case.back_profile_index = encoded & 1u;

  test_case.desc.DepthEnable = test_case.depth_enable;
  test_case.desc.DepthWriteMask = test_case.depth_write_index
                                      ? D3D11_DEPTH_WRITE_MASK_ALL
                                      : D3D11_DEPTH_WRITE_MASK_ZERO;
  test_case.desc.DepthFunc = kComparisonFuncs[test_case.depth_func_index];
  test_case.desc.StencilEnable = test_case.stencil_enable;
  test_case.desc.StencilReadMask = kReadMasks[test_case.read_mask_index];
  test_case.desc.StencilWriteMask = kWriteMasks[test_case.write_mask_index];
  test_case.desc.FrontFace = kFrontProfiles[test_case.front_profile_index];
  test_case.desc.BackFace = kBackProfiles[test_case.back_profile_index];
  return test_case;
}

D3D11_DEPTH_STENCIL_DESC
ExpectedDesc(const D3D11_DEPTH_STENCIL_DESC &created) {
  D3D11_DEPTH_STENCIL_DESC expected = created;
  if (!expected.DepthEnable) {
    expected.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    expected.DepthFunc = D3D11_COMPARISON_LESS;
    expected.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    expected.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
  }
  if (!expected.StencilEnable) {
    expected.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    expected.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    expected.FrontFace = kDefaultStencilOp;
    expected.BackFace = kDefaultStencilOp;
  }
  return expected;
}

bool StencilOpsEqual(const D3D11_DEPTH_STENCILOP_DESC &actual,
                     const D3D11_DEPTH_STENCILOP_DESC &expected) {
  return actual.StencilFailOp == expected.StencilFailOp &&
         actual.StencilDepthFailOp == expected.StencilDepthFailOp &&
         actual.StencilPassOp == expected.StencilPassOp &&
         actual.StencilFunc == expected.StencilFunc;
}

bool DepthStencilDescsEqual(const D3D11_DEPTH_STENCIL_DESC &actual,
                            const D3D11_DEPTH_STENCIL_DESC &expected) {
  return actual.DepthEnable == expected.DepthEnable &&
         actual.DepthWriteMask == expected.DepthWriteMask &&
         actual.DepthFunc == expected.DepthFunc &&
         actual.StencilEnable == expected.StencilEnable &&
         actual.StencilReadMask == expected.StencilReadMask &&
         actual.StencilWriteMask == expected.StencilWriteMask &&
         StencilOpsEqual(actual.FrontFace, expected.FrontFace) &&
         StencilOpsEqual(actual.BackFace, expected.BackFace);
}

class D3D11DepthStencilStateDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DepthStencilStateDescMatrixSpec,
       RoundTrips4096DescriptionsWithDisabledFieldNormalization) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kDepthStencilStateDescCaseCount);
  for (std::uint32_t logical = 0; logical < kDepthStencilStateDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kDepthStencilStateDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kDepthStencilStateDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const DepthStencilStateDescCase test_case = CaseForLogical(logical);
    const D3D11_DEPTH_STENCIL_DESC expected = ExpectedDesc(test_case.desc);
    ComPtr<ID3D11DepthStencilState> state;
    ComPtr<ID3D11DepthStencilState> duplicate;
    const HRESULT create_result = context_.device()->CreateDepthStencilState(
        &test_case.desc, state.put());
    HRESULT duplicate_result = E_FAIL;
    D3D11_DEPTH_STENCIL_DESC actual = {};
    if (create_result == S_OK && state) {
      state->GetDesc(&actual);
      duplicate_result = context_.device()->CreateDepthStencilState(
          &test_case.desc, duplicate.put());
    }

    const bool desc_matches = create_result == S_OK && state &&
                              DepthStencilDescsEqual(actual, expected);
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

    const auto case_id = dxmt::test::LogicalCaseId(
        kDepthStencilStateDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kDepthStencilStateDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateDepthStencilState,DepthStencilStateGetDesc,"
           "ComObjectIdentity\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kDepthStencilStateDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " depth_enable=" << test_case.depth_enable
        << " stencil_enable=" << test_case.stencil_enable
        << " depth_write_index=" << test_case.depth_write_index
        << " depth_func_index=" << test_case.depth_func_index
        << " read_mask_index=" << test_case.read_mask_index
        << " write_mask_index=" << test_case.write_mask_index
        << " front_profile_index=" << test_case.front_profile_index
        << " back_profile_index=" << test_case.back_profile_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: depth=(" << expected.DepthEnable << ','
        << static_cast<UINT>(expected.DepthWriteMask) << ','
        << static_cast<UINT>(expected.DepthFunc) << ") stencil=("
        << expected.StencilEnable << ','
        << static_cast<UINT>(expected.StencilReadMask) << ','
        << static_cast<UINT>(expected.StencilWriteMask) << ") front=("
        << static_cast<UINT>(expected.FrontFace.StencilFailOp) << ','
        << static_cast<UINT>(expected.FrontFace.StencilDepthFailOp) << ','
        << static_cast<UINT>(expected.FrontFace.StencilPassOp) << ','
        << static_cast<UINT>(expected.FrontFace.StencilFunc) << ") back=("
        << static_cast<UINT>(expected.BackFace.StencilFailOp) << ','
        << static_cast<UINT>(expected.BackFace.StencilDepthFailOp) << ','
        << static_cast<UINT>(expected.BackFace.StencilPassOp) << ','
        << static_cast<UINT>(expected.BackFace.StencilFunc) << ")\n"
        << "Observed: create_hresult=" << create_result
        << " duplicate_hresult=" << duplicate_result
        << " failure_phase=" << failure_phase << " depth=("
        << actual.DepthEnable << ',' << static_cast<UINT>(actual.DepthWriteMask)
        << ',' << static_cast<UINT>(actual.DepthFunc) << ") stencil=("
        << actual.StencilEnable << ','
        << static_cast<UINT>(actual.StencilReadMask) << ','
        << static_cast<UINT>(actual.StencilWriteMask) << ") front=("
        << static_cast<UINT>(actual.FrontFace.StencilFailOp) << ','
        << static_cast<UINT>(actual.FrontFace.StencilDepthFailOp) << ','
        << static_cast<UINT>(actual.FrontFace.StencilPassOp) << ','
        << static_cast<UINT>(actual.FrontFace.StencilFunc) << ") back=("
        << static_cast<UINT>(actual.BackFace.StencilFailOp) << ','
        << static_cast<UINT>(actual.BackFace.StencilDepthFailOp) << ','
        << static_cast<UINT>(actual.BackFace.StencilPassOp) << ','
        << static_cast<UINT>(actual.BackFace.StencilFunc)
        << ") state=" << state.get() << " duplicate=" << duplicate.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DepthStencilStateDescMatrixSpec,
       ReusesStatesWhoseDisabledFieldsNormalizeIdentically) {
  D3D11_DEPTH_STENCIL_DESC first_desc = {};
  first_desc.DepthEnable = FALSE;
  first_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  first_desc.DepthFunc = D3D11_COMPARISON_GREATER;
  first_desc.StencilEnable = FALSE;
  first_desc.StencilReadMask = 0x0f;
  first_desc.StencilWriteMask = 0xf0;
  first_desc.FrontFace = kFrontProfiles[1];
  first_desc.BackFace = kBackProfiles[1];

  D3D11_DEPTH_STENCIL_DESC second_desc = first_desc;
  second_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  second_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
  second_desc.StencilReadMask = 0x33;
  second_desc.StencilWriteMask = 0xcc;
  second_desc.FrontFace = kDefaultStencilOp;
  second_desc.BackFace = kDefaultStencilOp;

  ComPtr<ID3D11DepthStencilState> first;
  ComPtr<ID3D11DepthStencilState> second;
  ASSERT_EQ(
      context_.device()->CreateDepthStencilState(&first_desc, first.put()),
      S_OK);
  ASSERT_EQ(
      context_.device()->CreateDepthStencilState(&second_desc, second.put()),
      S_OK);
  ASSERT_NE(first.get(), nullptr);
  EXPECT_EQ(second.get(), first.get());

  D3D11_DEPTH_STENCIL_DESC actual = {};
  first->GetDesc(&actual);
  EXPECT_TRUE(DepthStencilDescsEqual(actual, ExpectedDesc(first_desc)));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11DepthStencilStateDescMatrixSpec,
       HandlesInvalidDescriptionAndValidationOnlyCreation) {
  ID3D11DepthStencilState *state = reinterpret_cast<ID3D11DepthStencilState *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateDepthStencilState(nullptr, &state),
            E_INVALIDARG);
  EXPECT_EQ(state, nullptr);

  D3D11_DEPTH_STENCIL_DESC desc = {};
  desc.DepthEnable = TRUE;
  desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  desc.DepthFunc = D3D11_COMPARISON_LESS;
  EXPECT_EQ(context_.device()->CreateDepthStencilState(&desc, nullptr),
            S_FALSE);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
