#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11.3 rasterizer-state coverage for every required forced sample
// count. Conservative rasterization remains disabled so the valid matrix is
// independent of the optional conservative-raster tier.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::array<UINT, 5> kForcedSampleCounts = {0, 1, 2, 4, 8};
constexpr std::uint32_t kRasterizerState2CaseCount = kForcedSampleCounts.size();

const dxmt::test::LogicalCaseFamilyRegistration kRasterizerState2Cases(
    "D3D11RasterizerState2DescMatrixSpec."
    "RoundTripsRequiredForcedSampleCounts",
    "D3D11.RasterizerState2.ForcedSampleCount.Description.",
    kRasterizerState2CaseCount, 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device3,CreateRasterizerState2,"
      "ID3D11RasterizerState2GetDesc2,ID3D11DeviceChildGetDevice,"
      "ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live pair of RasterizerState2 COM "
     "references per selected logical case",
     "create the five universally valid forced sample counts with conservative "
     "rasterization disabled, query Desc2, and recreate the same state",
     "every required forced sample count round-trips exactly, the legacy "
     "rasterizer interface is available, and identical descriptions reuse the "
     "same COM object",
     "logical ID, selected-case count, forced sample count, expected and "
     "returned descriptors, state and owner addresses, HRESULTs, failure "
     "phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kRasterizerState2Cost("D3D11RasterizerState2DescMatrixSpec."
                          "RoundTripsRequiredForcedSampleCounts",
                          dxmt::test::kResourceTestCost);

D3D11_RASTERIZER_DESC2 DescForLogical(std::uint32_t logical) {
  D3D11_RASTERIZER_DESC2 desc = {};
  desc.FillMode = D3D11_FILL_SOLID;
  desc.CullMode = D3D11_CULL_BACK;
  desc.FrontCounterClockwise = FALSE;
  desc.DepthBias = 0;
  desc.DepthBiasClamp = 0.0f;
  desc.SlopeScaledDepthBias = 0.0f;
  desc.DepthClipEnable = TRUE;
  desc.ScissorEnable = FALSE;
  desc.MultisampleEnable = FALSE;
  desc.AntialiasedLineEnable = FALSE;
  desc.ForcedSampleCount = kForcedSampleCounts[logical];
  desc.ConservativeRaster = D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF;
  return desc;
}

bool RasterizerDescsEqual(const D3D11_RASTERIZER_DESC2 &actual,
                          const D3D11_RASTERIZER_DESC2 &expected) {
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
         actual.ForcedSampleCount == expected.ForcedSampleCount &&
         actual.ConservativeRaster == expected.ConservativeRaster;
}

class D3D11RasterizerState2DescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device3), reinterpret_cast<void **>(device3_.put())),
        S_OK);
    ASSERT_NE(device3_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device3> device3_;
};

TEST_F(D3D11RasterizerState2DescMatrixSpec,
       RoundTripsRequiredForcedSampleCounts) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kRasterizerState2CaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kRasterizerState2Cases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kRasterizerState2Cases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const D3D11_RASTERIZER_DESC2 expected = DescForLogical(logical);
    ComPtr<ID3D11RasterizerState2> state;
    ComPtr<ID3D11RasterizerState2> duplicate;
    const HRESULT create_result =
        device3_->CreateRasterizerState2(&expected, state.put());
    HRESULT duplicate_result = E_FAIL;
    D3D11_RASTERIZER_DESC2 actual = {};
    ComPtr<ID3D11RasterizerState> legacy;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && state) {
      state->GetDesc2(&actual);
      state->GetDevice(owner.put());
      state->QueryInterface(__uuidof(ID3D11RasterizerState),
                            reinterpret_cast<void **>(legacy.put()));
      duplicate_result =
          device3_->CreateRasterizerState2(&expected, duplicate.put());
    }

    const bool valid = create_result == S_OK && state &&
                       RasterizerDescsEqual(actual, expected) && legacy &&
                       owner.get() == context_.device() &&
                       duplicate_result == S_OK &&
                       duplicate.get() == state.get();
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kRasterizerState2Cases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kRasterizerState2Cases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " forced_sample_count=" << expected.ForcedSampleCount
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: create_hresult=" << S_OK
                  << " duplicate_hresult=" << S_OK << " conservative="
                  << static_cast<UINT>(expected.ConservativeRaster)
                  << " owner=" << context_.device() << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " duplicate_hresult=" << duplicate_result
                  << " forced_sample_count=" << actual.ForcedSampleCount
                  << " conservative="
                  << static_cast<UINT>(actual.ConservativeRaster)
                  << " legacy=" << legacy.get() << " owner=" << owner.get()
                  << " state=" << state.get()
                  << " duplicate=" << duplicate.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11RasterizerState2DescMatrixSpec,
       RejectsNullAndInvalidVersionedFields) {
  ID3D11RasterizerState2 *state = reinterpret_cast<ID3D11RasterizerState2 *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateRasterizerState2(nullptr, &state), E_INVALIDARG);
  EXPECT_EQ(state, nullptr);

  D3D11_RASTERIZER_DESC2 desc = DescForLogical(0);
  EXPECT_EQ(device3_->CreateRasterizerState2(&desc, nullptr), S_FALSE);

  desc.ForcedSampleCount = 3;
  state = reinterpret_cast<ID3D11RasterizerState2 *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateRasterizerState2(&desc, &state), E_INVALIDARG);
  EXPECT_EQ(state, nullptr);

  desc = DescForLogical(0);
  desc.ConservativeRaster =
      static_cast<D3D11_CONSERVATIVE_RASTERIZATION_MODE>(2);
  state = reinterpret_cast<ID3D11RasterizerState2 *>(
      static_cast<std::uintptr_t>(1));
  EXPECT_EQ(device3_->CreateRasterizerState2(&desc, &state), E_INVALIDARG);
  EXPECT_EQ(state, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
