#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public D3D11 sampler-state creation and descriptor coverage. Eight filters,
// four address modes on each axis, and eight LOD/comparison profiles form
// exactly 4096 valid and distinct input descriptions.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kSamplerStateDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kSamplerStateDescCases(
    "D3D11SamplerStateDescMatrixSpec."
    "RoundTrips4096DescriptionsAndReusesIdenticalStates",
    "D3D11.SamplerState.Description.", kSamplerStateDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateSamplerState,ID3D11SamplerStateGetDesc,ComObjectIdentity"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live pair of sampler-state COM "
     "references per selected logical case",
     "create every valid combination of eight filters, four address modes "
     "per axis, and eight LOD/comparison profiles, query every descriptor "
     "field, and recreate the same description while the first object lives",
     "GetDesc exactly returns the public creation description and recreating "
     "an identical state returns the same COM interface",
     "logical ID, selected-case count, filter/address/profile indexes, all "
     "expected and returned descriptor fields, object addresses, HRESULTs, "
     "failure phase, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kSamplerStateDescCost("D3D11SamplerStateDescMatrixSpec."
                          "RoundTrips4096DescriptionsAndReusesIdenticalStates",
                          dxmt::test::kResourceTestCost);

constexpr std::array<D3D11_FILTER, 8> kFilters = {
    D3D11_FILTER_MIN_MAG_MIP_POINT,
    D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR,
    D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
    D3D11_FILTER_MIN_MAG_MIP_LINEAR,
    D3D11_FILTER_ANISOTROPIC,
    D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
    D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
    D3D11_FILTER_COMPARISON_ANISOTROPIC,
};

constexpr std::array<D3D11_TEXTURE_ADDRESS_MODE, 4> kAddressModes = {
    D3D11_TEXTURE_ADDRESS_WRAP,
    D3D11_TEXTURE_ADDRESS_MIRROR,
    D3D11_TEXTURE_ADDRESS_CLAMP,
    D3D11_TEXTURE_ADDRESS_MIRROR_ONCE,
};

struct SamplerProfile {
  FLOAT mip_lod_bias;
  UINT max_anisotropy;
  D3D11_COMPARISON_FUNC comparison_func;
  std::array<FLOAT, 4> border_color;
  FLOAT min_lod;
  FLOAT max_lod;
};

constexpr std::array<SamplerProfile, 8> kProfiles = {{
    {0.0f,
     1,
     D3D11_COMPARISON_NEVER,
     {0.0f, 0.0f, 0.0f, 0.0f},
     0.0f,
     D3D11_FLOAT32_MAX},
    {-2.0f, 2, D3D11_COMPARISON_LESS, {1.0f, 0.0f, 0.0f, 1.0f}, -4.0f, 0.0f},
    {-1.0f, 4, D3D11_COMPARISON_EQUAL, {0.0f, 1.0f, 0.0f, 1.0f}, 0.0f, 1.0f},
    {-0.5f,
     8,
     D3D11_COMPARISON_LESS_EQUAL,
     {0.0f, 0.0f, 1.0f, 1.0f},
     0.25f,
     2.0f},
    {0.0f, 16, D3D11_COMPARISON_GREATER, {1.0f, 1.0f, 0.0f, 1.0f}, 1.0f, 4.0f},
    {0.5f,
     3,
     D3D11_COMPARISON_NOT_EQUAL,
     {1.0f, 0.0f, 1.0f, 1.0f},
     -1.0f,
     3.0f},
    {1.0f,
     6,
     D3D11_COMPARISON_GREATER_EQUAL,
     {0.0f, 1.0f, 1.0f, 1.0f},
     2.0f,
     8.0f},
    {2.0f, 12, D3D11_COMPARISON_ALWAYS, {1.0f, 1.0f, 1.0f, 1.0f}, 4.0f, 16.0f},
}};

struct SamplerStateDescCase {
  UINT filter_index;
  std::array<UINT, 3> address_indexes;
  UINT profile_index;
  D3D11_SAMPLER_DESC desc;
};

SamplerStateDescCase CaseForLogical(std::uint32_t logical) {
  SamplerStateDescCase test_case = {};
  test_case.filter_index = logical & 7u;
  std::uint32_t encoded_addresses = (logical >> 3u) & 63u;
  for (UINT axis = 0; axis < test_case.address_indexes.size(); ++axis) {
    test_case.address_indexes[axis] = encoded_addresses & 3u;
    encoded_addresses >>= 2u;
  }
  test_case.profile_index = (logical >> 9u) & 7u;

  const SamplerProfile &profile = kProfiles[test_case.profile_index];
  test_case.desc.Filter = kFilters[test_case.filter_index];
  test_case.desc.AddressU = kAddressModes[test_case.address_indexes[0]];
  test_case.desc.AddressV = kAddressModes[test_case.address_indexes[1]];
  test_case.desc.AddressW = kAddressModes[test_case.address_indexes[2]];
  test_case.desc.MipLODBias = profile.mip_lod_bias;
  test_case.desc.MaxAnisotropy = profile.max_anisotropy;
  test_case.desc.ComparisonFunc = profile.comparison_func;
  for (UINT channel = 0; channel < profile.border_color.size(); ++channel)
    test_case.desc.BorderColor[channel] = profile.border_color[channel];
  test_case.desc.MinLOD = profile.min_lod;
  test_case.desc.MaxLOD = profile.max_lod;
  return test_case;
}

bool SamplerDescsEqual(const D3D11_SAMPLER_DESC &actual,
                       const D3D11_SAMPLER_DESC &expected) {
  if (actual.Filter != expected.Filter ||
      actual.AddressU != expected.AddressU ||
      actual.AddressV != expected.AddressV ||
      actual.AddressW != expected.AddressW ||
      actual.MipLODBias != expected.MipLODBias ||
      actual.MaxAnisotropy != expected.MaxAnisotropy ||
      actual.ComparisonFunc != expected.ComparisonFunc ||
      actual.MinLOD != expected.MinLOD || actual.MaxLOD != expected.MaxLOD)
    return false;
  for (UINT channel = 0; channel < 4; ++channel) {
    if (actual.BorderColor[channel] != expected.BorderColor[channel])
      return false;
  }
  return true;
}

class D3D11SamplerStateDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11SamplerStateDescMatrixSpec,
       RoundTrips4096DescriptionsAndReusesIdenticalStates) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kSamplerStateDescCaseCount);
  for (std::uint32_t logical = 0; logical < kSamplerStateDescCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kSamplerStateDescCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kSamplerStateDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const SamplerStateDescCase test_case = CaseForLogical(logical);
    ComPtr<ID3D11SamplerState> state;
    ComPtr<ID3D11SamplerState> duplicate;
    const HRESULT create_result =
        context_.device()->CreateSamplerState(&test_case.desc, state.put());
    HRESULT duplicate_result = E_FAIL;
    D3D11_SAMPLER_DESC actual = {};
    if (create_result == S_OK && state) {
      state->GetDesc(&actual);
      duplicate_result = context_.device()->CreateSamplerState(&test_case.desc,
                                                               duplicate.put());
    }

    const bool desc_matches = create_result == S_OK && state &&
                              SamplerDescsEqual(actual, test_case.desc);
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
        dxmt::test::LogicalCaseId(kSamplerStateDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kSamplerStateDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateSamplerState,SamplerStateGetDesc,"
           "ComObjectIdentity\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kSamplerStateDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " filter_index=" << test_case.filter_index << " address_indexes=("
        << test_case.address_indexes[0] << ',' << test_case.address_indexes[1]
        << ',' << test_case.address_indexes[2]
        << ") profile_index=" << test_case.profile_index
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: filter=" << static_cast<UINT>(test_case.desc.Filter)
        << " address=(" << static_cast<UINT>(test_case.desc.AddressU) << ','
        << static_cast<UINT>(test_case.desc.AddressV) << ','
        << static_cast<UINT>(test_case.desc.AddressW)
        << ") mip_lod_bias=" << test_case.desc.MipLODBias
        << " max_anisotropy=" << test_case.desc.MaxAnisotropy
        << " comparison_func="
        << static_cast<UINT>(test_case.desc.ComparisonFunc) << " border_color=("
        << test_case.desc.BorderColor[0] << ',' << test_case.desc.BorderColor[1]
        << ',' << test_case.desc.BorderColor[2] << ','
        << test_case.desc.BorderColor[3] << ") lod_range=("
        << test_case.desc.MinLOD << ',' << test_case.desc.MaxLOD << ")\n"
        << "Observed: create_hresult=" << create_result
        << " duplicate_hresult=" << duplicate_result
        << " failure_phase=" << failure_phase
        << " filter=" << static_cast<UINT>(actual.Filter) << " address=("
        << static_cast<UINT>(actual.AddressU) << ','
        << static_cast<UINT>(actual.AddressV) << ','
        << static_cast<UINT>(actual.AddressW)
        << ") mip_lod_bias=" << actual.MipLODBias
        << " max_anisotropy=" << actual.MaxAnisotropy
        << " comparison_func=" << static_cast<UINT>(actual.ComparisonFunc)
        << " border_color=(" << actual.BorderColor[0] << ','
        << actual.BorderColor[1] << ',' << actual.BorderColor[2] << ','
        << actual.BorderColor[3] << ") lod_range=(" << actual.MinLOD << ','
        << actual.MaxLOD << ") state=" << state.get()
        << " duplicate=" << duplicate.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
