#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §15.3: GetCopyableFootprints matrix over sizes, formats, base offsets.
// Public D3D12 API only.

namespace {

using dxmt::test::D3D12TestContext;

struct FootprintCase {
  UINT width;
  UINT height;
  UINT16 array_size;
  UINT16 mips;
  DXGI_FORMAT format;
  UINT first_subresource;
  UINT num_subresources;
  UINT64 base_offset;
};

std::vector<FootprintCase> BuildFootprintCases() {
  std::vector<FootprintCase> cases;
  const DXGI_FORMAT formats[] = {
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_UINT,
      DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R8_UINT,
      DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC3_UNORM,
  };
  const UINT dims[] = {1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 64, 127,
                       128};
  const UINT64 bases[] = {0, 1, 255, 256, 511, 512, 1024, 4096};
  // These inputs are orthogonal. Cover every value while varying one axis at
  // a time instead of multiplying them into thousands of equivalent calls.
  for (const UINT width : dims)
    cases.push_back({width, width, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 1, 0});
  for (const DXGI_FORMAT format : formats) {
    const UINT extent = format == DXGI_FORMAT_BC1_UNORM ||
                                format == DXGI_FORMAT_BC3_UNORM
                            ? 16
                            : 17;
    cases.push_back({extent, extent, 1, 1, format, 0, 1, 0});
  }
  for (const UINT64 base : bases)
    cases.push_back({33, 7, 1, 1, DXGI_FORMAT_R32_FLOAT, 0, 1, base});
  for (const UINT16 array_size : {UINT16{1}, UINT16{2}, UINT16{4}}) {
    for (const UINT16 mips : {UINT16{1}, UINT16{2}, UINT16{3}}) {
      const UINT total = static_cast<UINT>(array_size) * mips;
      cases.push_back({32, 16, array_size, mips, DXGI_FORMAT_BC1_UNORM, 0,
                       total, 512});
      if (total > 1)
        cases.push_back({32, 16, array_size, mips, DXGI_FORMAT_BC1_UNORM, 0,
                         1, 512});
    }
  }
  return cases;
}

class FootprintMatrixSpec : public ::testing::TestWithParam<FootprintCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(FootprintMatrixSpec, ReportsCoherentLayoutsAndBaseOffsetShift) {
  const auto &test = GetParam();
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = test.width;
  desc.Height = test.height;
  desc.DepthOrArraySize = test.array_size;
  desc.MipLevels = test.mips;
  desc.Format = test.format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> zero_layouts(
      test.num_subresources);
  std::vector<UINT> zero_rows(test.num_subresources);
  std::vector<UINT64> zero_row_sizes(test.num_subresources);
  UINT64 zero_total = 0;
  context_.device()->GetCopyableFootprints(
      &desc, test.first_subresource, test.num_subresources, 0,
      zero_layouts.data(), zero_rows.data(), zero_row_sizes.data(),
      &zero_total);

  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(test.num_subresources);
  std::vector<UINT> rows(test.num_subresources);
  std::vector<UINT64> row_sizes(test.num_subresources);
  UINT64 total = 0;
  context_.device()->GetCopyableFootprints(
      &desc, test.first_subresource, test.num_subresources, test.base_offset,
      layouts.data(), rows.data(), row_sizes.data(), &total);

  EXPECT_EQ(total, zero_total);
  EXPECT_GT(zero_total, 0u);
  for (UINT i = 0; i < test.num_subresources; ++i) {
    EXPECT_EQ(rows[i], zero_rows[i]) << i;
    EXPECT_EQ(row_sizes[i], zero_row_sizes[i]) << i;
    EXPECT_EQ(layouts[i].Offset, zero_layouts[i].Offset + test.base_offset)
        << i;
    EXPECT_EQ(std::memcmp(&layouts[i].Footprint, &zero_layouts[i].Footprint,
                          sizeof(layouts[i].Footprint)),
              0)
        << i;
    EXPECT_EQ(layouts[i].Footprint.Format, test.format) << i;
    EXPECT_GE(layouts[i].Footprint.Width, 1u) << i;
    EXPECT_GE(layouts[i].Footprint.Height, 1u) << i;
    EXPECT_GE(layouts[i].Footprint.RowPitch, row_sizes[i]) << i;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string FootprintName(const ::testing::TestParamInfo<FootprintCase> &info) {
  return "W" + std::to_string(info.param.width) + "H" +
         std::to_string(info.param.height) + "A" +
         std::to_string(info.param.array_size) + "M" +
         std::to_string(info.param.mips) + "F" +
         std::to_string(static_cast<UINT>(info.param.format)) + "S" +
         std::to_string(info.param.num_subresources) + "B" +
         std::to_string(info.param.base_offset) + "I" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(LayoutMatrix, FootprintMatrixSpec,
                         ::testing::ValuesIn(BuildFootprintCases()),
                         FootprintName);

} // namespace
