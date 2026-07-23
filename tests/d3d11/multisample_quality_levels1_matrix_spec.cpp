#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

// Public D3D11.2 multisample-capability coverage. With Flags set to zero,
// CheckMultisampleQualityLevels1 is the versioned form of the base-device
// query and must report the same result and quality-level count.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::array<DXGI_FORMAT, 6> kFormats = {
    DXGI_FORMAT_R8G8B8A8_UNORM,     DXGI_FORMAT_B8G8R8A8_UNORM,
    DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_D24_UNORM_S8_UINT,  DXGI_FORMAT_D32_FLOAT,
};

constexpr std::array<UINT, 6> kSampleCounts = {1, 2, 4, 8, 16, 32};

constexpr std::uint32_t kMultisampleQualityCaseCount =
    kFormats.size() * kSampleCounts.size();

const dxmt::test::LogicalCaseFamilyRegistration kMultisampleQualityCases(
    "D3D11MultisampleQualityLevels1MatrixSpec."
    "MatchesLegacyQueryWithoutFlags",
    "D3D11.Device2.MultisampleQualityLevels1.Compatibility.",
    kMultisampleQualityCaseCount, 2,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11Device2,CheckMultisampleQualityLevels1,"
      "CheckMultisampleQualityLevels"},
     dxmt::test::kNormalTestCost,
     "one test-local D3D11 device and two capability queries per selected "
     "logical case",
     "query representative color, floating-point, and depth formats at every "
     "power-of-two sample count through the base and D3D11.2 device APIs",
     "with Flags set to zero, both public APIs return the same HRESULT and "
     "quality-level count",
     "logical ID, selected-case count, format and sample-count indexes, "
     "HRESULTs, quality-level counts, failure phase, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kMultisampleQualityCost("D3D11MultisampleQualityLevels1MatrixSpec."
                            "MatchesLegacyQueryWithoutFlags",
                            dxmt::test::kNormalTestCost);

struct MultisampleQualityCase {
  std::size_t format_index;
  std::size_t sample_count_index;
};

MultisampleQualityCase CaseForLogical(std::uint32_t logical) {
  return {logical % kFormats.size(), logical / kFormats.size()};
}

class D3D11MultisampleQualityLevels1MatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device2), reinterpret_cast<void **>(device2_.put())),
        S_OK);
    ASSERT_NE(device2_.get(), nullptr);
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device2> device2_;
};

TEST_F(D3D11MultisampleQualityLevels1MatrixSpec,
       MatchesLegacyQueryWithoutFlags) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kMultisampleQualityCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kMultisampleQualityCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kMultisampleQualityCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const MultisampleQualityCase test_case = CaseForLogical(logical);
    const DXGI_FORMAT format = kFormats[test_case.format_index];
    const UINT sample_count = kSampleCounts[test_case.sample_count_index];
    UINT legacy_quality_levels = std::numeric_limits<UINT>::max();
    UINT versioned_quality_levels = std::numeric_limits<UINT>::max();
    const HRESULT legacy_result =
        context_.device()->CheckMultisampleQualityLevels(
            format, sample_count, &legacy_quality_levels);
    const HRESULT versioned_result = device2_->CheckMultisampleQualityLevels1(
        format, sample_count, 0, &versioned_quality_levels);

    const bool valid = versioned_result == legacy_result &&
                       versioned_quality_levels == legacy_quality_levels;
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kMultisampleQualityCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kMultisampleQualityCases.family().traits.test_class)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " format_index=" << test_case.format_index
                  << " format=" << static_cast<UINT>(format)
                  << " sample_count_index=" << test_case.sample_count_index
                  << " sample_count=" << sample_count
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: versioned_hresult=" << legacy_result
                  << " versioned_quality_levels=" << legacy_quality_levels
                  << '\n'
                  << "Observed: versioned_hresult=" << versioned_result
                  << " versioned_quality_levels=" << versioned_quality_levels
                  << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11MultisampleQualityLevels1MatrixSpec, RejectsUnknownFlags) {
  UINT quality_levels = std::numeric_limits<UINT>::max();
  EXPECT_EQ(device2_->CheckMultisampleQualityLevels1(
                DXGI_FORMAT_R8G8B8A8_UNORM, 4, 0x80000000u, &quality_levels),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
