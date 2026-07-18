#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstring>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class CopyFootprintSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  static D3D12_RESOURCE_DESC TextureDesc() {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 17;
    desc.Height = 9;
    desc.DepthOrArraySize = 2;
    desc.MipLevels = 3;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return desc;
  }

  static D3D12_RESOURCE_DESC1 TextureDesc1(
      const D3D12_RESOURCE_DESC &desc) {
    D3D12_RESOURCE_DESC1 desc1 = {};
    desc1.Dimension = desc.Dimension;
    desc1.Alignment = desc.Alignment;
    desc1.Width = desc.Width;
    desc1.Height = desc.Height;
    desc1.DepthOrArraySize = desc.DepthOrArraySize;
    desc1.MipLevels = desc.MipLevels;
    desc1.Format = desc.Format;
    desc1.SampleDesc = desc.SampleDesc;
    desc1.Layout = desc.Layout;
    desc1.Flags = desc.Flags;
    return desc1;
  }

  D3D12TestContext context_;
};

TEST_F(CopyFootprintSpec, BaseOffsetOnlyShiftsLayouts) {
  constexpr UINT subresource_count = 6;
  const auto desc = TextureDesc();
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, subresource_count> reference =
      {};
  std::array<UINT, subresource_count> reference_rows = {};
  std::array<UINT64, subresource_count> reference_row_sizes = {};
  UINT64 reference_total = 0;
  context_.device()->GetCopyableFootprints(
      &desc, 0, subresource_count, 0, reference.data(), reference_rows.data(),
      reference_row_sizes.data(), &reference_total);

  for (const UINT64 base_offset : {1ull, 511ull, 512ull, 777ull}) {
    SCOPED_TRACE(::testing::Message() << "base_offset=" << base_offset);
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, subresource_count> layouts =
        {};
    std::array<UINT, subresource_count> rows = {};
    std::array<UINT64, subresource_count> row_sizes = {};
    UINT64 total = 0;
    context_.device()->GetCopyableFootprints(
        &desc, 0, subresource_count, base_offset, layouts.data(), rows.data(),
        row_sizes.data(), &total);

    EXPECT_EQ(rows, reference_rows);
    EXPECT_EQ(row_sizes, reference_row_sizes);
    EXPECT_EQ(total, reference_total);
    for (UINT index = 0; index < subresource_count; ++index) {
      EXPECT_EQ(layouts[index].Offset, reference[index].Offset + base_offset);
      EXPECT_EQ(std::memcmp(&layouts[index].Footprint,
                            &reference[index].Footprint,
                            sizeof(layouts[index].Footprint)),
                0);
    }
  }
}

TEST_F(CopyFootprintSpec, TotalBytesMatchesLastFootprint) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  desc.Width = 7;
  desc.Height = 5;
  desc.DepthOrArraySize = 3;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8_UINT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
  UINT rows = 0;
  UINT64 row_size = 0;
  UINT64 total = 0;
  context_.device()->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rows,
                                           &row_size, &total);

  const UINT64 slice_pitch = UINT64(layout.Footprint.RowPitch) * rows;
  const UINT64 expected =
      layout.Offset + slice_pitch * (layout.Footprint.Depth - 1) +
      UINT64(layout.Footprint.RowPitch) * (rows - 1) + row_size;
  EXPECT_EQ(total, expected);
}

TEST_F(CopyFootprintSpec, DoesNotWritePastOutputArrays) {
  constexpr UINT requested = 3;
  const auto desc = TextureDesc();
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, requested + 1> layouts;
  std::array<UINT, requested + 1> rows;
  std::array<UINT64, requested + 1> row_sizes;
  std::memset(layouts.data(), 0xa5, sizeof(layouts));
  rows.fill(0xa5a5a5a5u);
  row_sizes.fill(0xa5a5a5a5a5a5a5a5ull);
  const auto layout_sentinel = layouts.back();
  const UINT row_sentinel = rows.back();
  const UINT64 row_size_sentinel = row_sizes.back();
  UINT64 total = 0;

  context_.device()->GetCopyableFootprints(&desc, 0, requested, 0,
                                           layouts.data(), rows.data(),
                                           row_sizes.data(), &total);

  EXPECT_NE(total, UINT64_MAX);
  EXPECT_EQ(
      std::memcmp(&layouts.back(), &layout_sentinel, sizeof(layout_sentinel)),
      0);
  EXPECT_EQ(rows.back(), row_sentinel);
  EXPECT_EQ(row_sizes.back(), row_size_sentinel);
}

TEST_F(CopyFootprintSpec, ZeroSubresourcesOnlyInitializesTotalBytes) {
  const auto desc = TextureDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
  std::memset(&layout, 0x5a, sizeof(layout));
  const auto sentinel = layout;
  UINT rows = 0x5a5a5a5au;
  UINT64 row_size = 0x5a5a5a5a5a5a5a5aull;
  UINT64 total = UINT64_MAX;

  context_.device()->GetCopyableFootprints(&desc, 0, 0, 0, &layout, &rows,
                                           &row_size, &total);

  EXPECT_EQ(total, 0u);
  EXPECT_EQ(std::memcmp(&layout, &sentinel, sizeof(sentinel)), 0);
  EXPECT_EQ(rows, 0x5a5a5a5au);
  EXPECT_EQ(row_size, 0x5a5a5a5a5a5a5a5aull);
}

TEST_F(CopyFootprintSpec, Device8MatchesBaseFunction) {
  ComPtr<ID3D12Device8> device8;
  ASSERT_TRUE(SUCCEEDED(
      context_.device()->QueryInterface(IID_PPV_ARGS(device8.put()))));
  ASSERT_TRUE(device8);
  const auto desc = TextureDesc();
  const auto desc1 = TextureDesc1(desc);
  constexpr UINT first_subresource = 1;
  constexpr UINT subresource_count = 4;
  constexpr UINT64 base_offset = 777;
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, subresource_count> base = {};
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, subresource_count> extended =
      {};
  std::array<UINT, subresource_count> base_rows = {};
  std::array<UINT, subresource_count> extended_rows = {};
  std::array<UINT64, subresource_count> base_row_sizes = {};
  std::array<UINT64, subresource_count> extended_row_sizes = {};
  UINT64 base_total = 0;
  UINT64 extended_total = 0;

  context_.device()->GetCopyableFootprints(
      &desc, first_subresource, subresource_count, base_offset, base.data(),
      base_rows.data(), base_row_sizes.data(), &base_total);
  device8->GetCopyableFootprints1(&desc1, first_subresource, subresource_count,
                                  base_offset, extended.data(),
                                  extended_rows.data(),
                                  extended_row_sizes.data(), &extended_total);

  EXPECT_EQ(std::memcmp(base.data(), extended.data(), sizeof(base)), 0);
  EXPECT_EQ(base_rows, extended_rows);
  EXPECT_EQ(base_row_sizes, extended_row_sizes);
  EXPECT_EQ(base_total, extended_total);
}

TEST_F(CopyFootprintSpec, Device8MatchesBaseFailureAndZeroCountContracts) {
  ComPtr<ID3D12Device8> device8;
  ASSERT_TRUE(SUCCEEDED(
      context_.device()->QueryInterface(IID_PPV_ARGS(device8.put()))));
  ASSERT_TRUE(device8);

  auto expect_invalid_match = [&](const D3D12_RESOURCE_DESC &desc,
                                  UINT first_subresource,
                                  UINT subresource_count) {
    SCOPED_TRACE(::testing::Message()
                 << "first_subresource=" << first_subresource
                 << " subresource_count=" << subresource_count);
    ASSERT_LE(subresource_count, 2u);
    const auto desc1 = TextureDesc1(desc);
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> base_layouts = {};
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> extended_layouts = {};
    std::array<UINT, 2> base_rows = {};
    std::array<UINT, 2> extended_rows = {};
    std::array<UINT64, 2> base_row_sizes = {};
    std::array<UINT64, 2> extended_row_sizes = {};
    UINT64 base_total = 0;
    UINT64 extended_total = 0;

    context_.device()->GetCopyableFootprints(
        &desc, first_subresource, subresource_count, 0, base_layouts.data(),
        base_rows.data(), base_row_sizes.data(), &base_total);
    device8->GetCopyableFootprints1(
        &desc1, first_subresource, subresource_count, 0,
        extended_layouts.data(), extended_rows.data(),
        extended_row_sizes.data(), &extended_total);

    EXPECT_EQ(std::memcmp(base_layouts.data(), extended_layouts.data(),
                          sizeof(base_layouts)),
              0);
    EXPECT_EQ(base_rows, extended_rows);
    EXPECT_EQ(base_row_sizes, extended_row_sizes);
    EXPECT_EQ(base_total, extended_total);
    EXPECT_EQ(base_total, UINT64_MAX);
    for (UINT index = 0; index < subresource_count; ++index) {
      EXPECT_EQ(base_layouts[index].Offset, UINT64_MAX);
      EXPECT_EQ(base_rows[index], UINT_MAX);
      EXPECT_EQ(base_row_sizes[index], UINT64_MAX);
    }
  };

  auto invalid_desc = TextureDesc();
  invalid_desc.Width = 0;
  expect_invalid_match(invalid_desc, 0, 2);
  expect_invalid_match(TextureDesc(), 5, 2);

  const auto desc = TextureDesc();
  const auto desc1 = TextureDesc1(desc);
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT base_layout;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT extended_layout;
  std::memset(&base_layout, 0x5a, sizeof(base_layout));
  std::memset(&extended_layout, 0x5a, sizeof(extended_layout));
  UINT base_rows = 0x5a5a5a5au;
  UINT extended_rows = 0x5a5a5a5au;
  UINT64 base_row_size = 0x5a5a5a5a5a5a5a5aull;
  UINT64 extended_row_size = 0x5a5a5a5a5a5a5a5aull;
  UINT64 base_total = UINT64_MAX;
  UINT64 extended_total = UINT64_MAX;

  context_.device()->GetCopyableFootprints(
      &desc, 0, 0, 0, &base_layout, &base_rows, &base_row_size, &base_total);
  device8->GetCopyableFootprints1(&desc1, 0, 0, 0, &extended_layout,
                                  &extended_rows, &extended_row_size,
                                  &extended_total);

  EXPECT_EQ(std::memcmp(&base_layout, &extended_layout, sizeof(base_layout)),
            0);
  EXPECT_EQ(base_rows, extended_rows);
  EXPECT_EQ(base_row_size, extended_row_size);
  EXPECT_EQ(base_total, 0u);
  EXPECT_EQ(extended_total, 0u);
}

TEST_F(CopyFootprintSpec, BaseOffsetOverflowFailsAtomicallyAcrossVersions) {
  ComPtr<ID3D12Device8> device8;
  ASSERT_EQ(context_.device()->QueryInterface(IID_PPV_ARGS(device8.put())),
            S_OK);
  const auto desc = TextureDesc();
  const auto desc1 = TextureDesc1(desc);
  constexpr UINT subresource_count = 2;
  constexpr UINT64 base_offset = UINT64_MAX - 255;
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, subresource_count>
      base_layouts = {};
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, subresource_count>
      extended_layouts = {};
  std::array<UINT, subresource_count> base_rows = {};
  std::array<UINT, subresource_count> extended_rows = {};
  std::array<UINT64, subresource_count> base_row_sizes = {};
  std::array<UINT64, subresource_count> extended_row_sizes = {};
  UINT64 base_total = 0;
  UINT64 extended_total = 0;

  context_.device()->GetCopyableFootprints(
      &desc, 0, subresource_count, base_offset, base_layouts.data(),
      base_rows.data(), base_row_sizes.data(), &base_total);
  device8->GetCopyableFootprints1(
      &desc1, 0, subresource_count, base_offset, extended_layouts.data(),
      extended_rows.data(), extended_row_sizes.data(), &extended_total);

  EXPECT_EQ(std::memcmp(base_layouts.data(), extended_layouts.data(),
                        sizeof(base_layouts)),
            0);
  EXPECT_EQ(base_rows, extended_rows);
  EXPECT_EQ(base_row_sizes, extended_row_sizes);
  EXPECT_EQ(base_total, UINT64_MAX);
  EXPECT_EQ(extended_total, UINT64_MAX);
  for (UINT index = 0; index < subresource_count; ++index) {
    EXPECT_EQ(base_layouts[index].Offset, UINT64_MAX);
    EXPECT_EQ(static_cast<UINT>(base_layouts[index].Footprint.Format),
              UINT_MAX);
    EXPECT_EQ(base_layouts[index].Footprint.Width, UINT_MAX);
    EXPECT_EQ(base_layouts[index].Footprint.Height, UINT_MAX);
    EXPECT_EQ(base_layouts[index].Footprint.Depth, UINT_MAX);
    EXPECT_EQ(base_layouts[index].Footprint.RowPitch, UINT_MAX);
    EXPECT_EQ(base_rows[index], UINT_MAX);
    EXPECT_EQ(base_row_sizes[index], UINT64_MAX);
  }

  UINT64 reference_total = 0;
  UINT64 base_total_only = 0;
  UINT64 extended_total_only = 0;
  context_.device()->GetCopyableFootprints(
      &desc, 0, subresource_count, 0, nullptr, nullptr, nullptr,
      &reference_total);
  context_.device()->GetCopyableFootprints(
      &desc, 0, subresource_count, UINT64_MAX, nullptr, nullptr, nullptr,
      &base_total_only);
  device8->GetCopyableFootprints1(
      &desc1, 0, subresource_count, UINT64_MAX, nullptr, nullptr, nullptr,
      &extended_total_only);
  EXPECT_NE(reference_total, UINT64_MAX);
  EXPECT_EQ(base_total_only, reference_total);
  EXPECT_EQ(extended_total_only, reference_total);
}

} // namespace
