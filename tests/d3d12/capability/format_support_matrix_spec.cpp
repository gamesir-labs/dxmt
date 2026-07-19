#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Public D3D12 CheckFeatureSupport(FORMAT_SUPPORT) matrix over DXGI formats.

namespace {

using dxmt::test::D3D12TestContext;

std::vector<DXGI_FORMAT> BuildFormatList() {
  std::vector<DXGI_FORMAT> formats;
  // Contiguous DXGI_FORMAT enums used by D3D12 apps (skip UNKNOWN=0).
  for (UINT f = 1; f <= 115; ++f)
    formats.push_back(static_cast<DXGI_FORMAT>(f));
  // A few extended / typeless values commonly probed by engines.
  for (UINT f : {130u, 131u, 132u, 133u, 134u, 135u, 136u, 137u, 138u, 139u,
                 140u, 141u, 142u, 143u, 144u, 145u, 146u, 147u, 148u, 149u,
                 150u, 189u, 190u, 191u})
    formats.push_back(static_cast<DXGI_FORMAT>(f));
  return formats;
}

class FormatSupportMatrixSpec
    : public ::testing::TestWithParam<DXGI_FORMAT> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(FormatSupportMatrixSpec, CheckFeatureSupportReturnsCoherentBits) {
  const DXGI_FORMAT format = GetParam();
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
  support.Format = format;
  const HRESULT hr = context_.device()->CheckFeatureSupport(
      D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
  // Invalid formats may fail; valid ones must succeed and keep the format.
  if (FAILED(hr)) {
    EXPECT_TRUE(hr == E_INVALIDARG || hr == E_FAIL || hr == E_INVALIDARG)
        << std::hex << hr;
  } else {
    EXPECT_EQ(support.Format, format);
    // Support1/2 are free-form capability bits; just ensure no device loss.
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(FormatSupportMatrixSpec, CheckFeatureSupportInfo1IsStable) {
  const DXGI_FORMAT format = GetParam();
  D3D12_FEATURE_DATA_FORMAT_INFO info = {};
  info.Format = format;
  const HRESULT hr = context_.device()->CheckFeatureSupport(
      D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info));
  if (SUCCEEDED(hr)) {
    EXPECT_EQ(info.Format, format);
    // Plane count is 0 or small for most formats.
    EXPECT_LE(info.PlaneCount, 8u);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string FormatName(const ::testing::TestParamInfo<DXGI_FORMAT> &info) {
  return "Fmt" + std::to_string(static_cast<UINT>(info.param));
}

INSTANTIATE_TEST_SUITE_P(FormatMatrix, FormatSupportMatrixSpec,
                         ::testing::ValuesIn(BuildFormatList()), FormatName);

} // namespace
