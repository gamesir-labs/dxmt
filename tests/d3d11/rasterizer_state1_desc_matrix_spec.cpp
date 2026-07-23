#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11.1 rasterizer-state coverage. The complete 4096-description
// base matrix is crossed with the universally supported forced sample counts
// zero and one, yielding 8192 distinct versioned descriptions.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

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

constexpr std::array<UINT, 2> kForcedSampleCounts = {0, 1};
constexpr std::uint32_t kBaseDescriptionCount = 4096;
constexpr std::uint32_t kRasterizerState1CaseCount =
    kBaseDescriptionCount * kForcedSampleCounts.size();

const dxmt::test::LogicalCaseFamilyRegistration kRasterizerState1Cases(
    "D3D11RasterizerState1DescMatrixSpec."
    "RoundTrips8192DescriptionsAndReusesIdenticalStates",
    "D3D11.RasterizerState1.Description.", kRasterizerState1CaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device1,CreateRasterizerState1,"
      "ID3D11RasterizerState1GetDesc1,ID3D11DeviceChildGetDevice,"
      "ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live pair of RasterizerState1 COM "
     "references per selected logical case",
     "cross all 4096 base rasterizer descriptions with forced sample counts "
     "zero and one, query every Desc1 field, and recreate each description",
     "every description round-trips exactly, exposes its creating device and "
     "legacy interface, and reuses the same live COM state object",
     "logical ID, selected-case count, all expected and returned descriptor "
     "fields, state, legacy and owner addresses, HRESULTs, failure phase, "
     "and exact replay argument"});

const dxmt::test::TestCostRegistration
    kRasterizerState1Cost("D3D11RasterizerState1DescMatrixSpec."
                          "RoundTrips8192DescriptionsAndReusesIdenticalStates",
                          dxmt::test::kResourceTestCost);

struct RasterizerState1Case {
  D3D11_RASTERIZER_DESC1 desc;
};

RasterizerState1Case CaseForLogical(std::uint32_t logical) {
  RasterizerState1Case test_case = {};
  std::uint32_t encoded = logical;
  test_case.desc.ForcedSampleCount = kForcedSampleCounts[encoded & 1u];
  encoded >>= 1u;
  test_case.desc.FillMode = kFillModes[encoded & 1u];
  encoded >>= 1u;
  test_case.desc.CullMode = kCullModes[encoded & 1u];
  encoded >>= 1u;
  test_case.desc.FrontCounterClockwise = encoded & 1u ? TRUE : FALSE;
  encoded >>= 1u;
  test_case.desc.DepthBias = kDepthBiases[encoded & 7u];
  encoded >>= 3u;
  test_case.desc.DepthBiasClamp = encoded & 1u ? 2.0f : 0.0f;
  encoded >>= 1u;
  test_case.desc.SlopeScaledDepthBias = encoded & 1u ? 1.25f : 0.0f;
  encoded >>= 1u;
  test_case.desc.DepthClipEnable = encoded & 1u ? TRUE : FALSE;
  encoded >>= 1u;
  test_case.desc.ScissorEnable = encoded & 1u ? TRUE : FALSE;
  encoded >>= 1u;
  test_case.desc.MultisampleEnable = encoded & 1u ? TRUE : FALSE;
  encoded >>= 1u;
  test_case.desc.AntialiasedLineEnable = encoded & 1u ? TRUE : FALSE;
  return test_case;
}

bool RasterizerDescsEqual(const D3D11_RASTERIZER_DESC1 &actual,
                          const D3D11_RASTERIZER_DESC1 &expected) {
  return actual.FillMode == expected.FillMode &&
         actual.CullMode == expected.CullMode &&
         actual.FrontCounterClockwise == expected.FrontCounterClockwise &&
         actual.DepthBias == expected.DepthBias &&
         actual.DepthBiasClamp == expected.DepthBiasClamp &&
         actual.SlopeScaledDepthBias == expected.SlopeScaledDepthBias &&
         actual.DepthClipEnable == expected.DepthClipEnable &&
         actual.ScissorEnable == expected.ScissorEnable &&
         actual.MultisampleEnable == expected.MultisampleEnable &&
         actual.AntialiasedLineEnable == expected.AntialiasedLineEnable &&
         actual.ForcedSampleCount == expected.ForcedSampleCount;
}

class D3D11RasterizerState1DescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device1), reinterpret_cast<void **>(device1_.put())),
        S_OK);
    ASSERT_NE(device1_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device1> device1_;
};

TEST_F(D3D11RasterizerState1DescMatrixSpec,
       RoundTrips8192DescriptionsAndReusesIdenticalStates) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kRasterizerState1CaseCount);
  for (std::uint32_t logical = 0; logical < kRasterizerState1CaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kRasterizerState1Cases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kRasterizerState1Cases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const RasterizerState1Case test_case = CaseForLogical(logical);
    ComPtr<ID3D11RasterizerState1> state;
    ComPtr<ID3D11RasterizerState1> duplicate;
    const HRESULT create_result =
        device1_->CreateRasterizerState1(&test_case.desc, state.put());
    HRESULT duplicate_result = E_FAIL;
    D3D11_RASTERIZER_DESC1 actual = {};
    ComPtr<ID3D11RasterizerState> legacy;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && state) {
      state->GetDesc1(&actual);
      state->GetDevice(owner.put());
      state->QueryInterface(__uuidof(ID3D11RasterizerState),
                            reinterpret_cast<void **>(legacy.put()));
      duplicate_result =
          device1_->CreateRasterizerState1(&test_case.desc, duplicate.put());
    }

    const bool valid = create_result == S_OK && state &&
                       RasterizerDescsEqual(actual, test_case.desc) && legacy &&
                       owner.get() == context_.device() &&
                       duplicate_result == S_OK &&
                       duplicate.get() == state.get();
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kRasterizerState1Cases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kRasterizerState1Cases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: fill="
                  << static_cast<UINT>(test_case.desc.FillMode)
                  << " cull=" << static_cast<UINT>(test_case.desc.CullMode)
                  << " front_ccw=" << test_case.desc.FrontCounterClockwise
                  << " depth_bias=" << test_case.desc.DepthBias
                  << " depth_bias_clamp=" << test_case.desc.DepthBiasClamp
                  << " slope_bias=" << test_case.desc.SlopeScaledDepthBias
                  << " depth_clip=" << test_case.desc.DepthClipEnable
                  << " scissor=" << test_case.desc.ScissorEnable
                  << " multisample=" << test_case.desc.MultisampleEnable
                  << " aa_line=" << test_case.desc.AntialiasedLineEnable
                  << " forced_sample_count=" << test_case.desc.ForcedSampleCount
                  << " owner=" << context_.device() << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " duplicate_hresult=" << duplicate_result
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
                  << " forced_sample_count=" << actual.ForcedSampleCount
                  << " state=" << state.get()
                  << " duplicate=" << duplicate.get()
                  << " legacy=" << legacy.get() << " owner=" << owner.get()
                  << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11RasterizerState1DescMatrixSpec,
       HandlesInvalidDescriptionAndValidationOnlyCreation) {
  ID3D11RasterizerState1 *state = reinterpret_cast<ID3D11RasterizerState1 *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device1_->CreateRasterizerState1(nullptr, &state), E_INVALIDARG);
  EXPECT_EQ(state, nullptr);

  D3D11_RASTERIZER_DESC1 desc = CaseForLogical(0).desc;
  EXPECT_EQ(device1_->CreateRasterizerState1(&desc, nullptr), S_FALSE);

  desc.ForcedSampleCount = 3;
  state = reinterpret_cast<ID3D11RasterizerState1 *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device1_->CreateRasterizerState1(&desc, &state), E_INVALIDARG);
  EXPECT_EQ(state, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
