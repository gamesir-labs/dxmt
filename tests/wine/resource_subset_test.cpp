#include <dxmt_test.hpp>

#include "dxmt_subresource.hpp"
#include "dxmt_texture.hpp"

namespace {

TEST(ResourceSubset, WholeResourceOverlapsEverySubsetSymmetrically) {
  const dxmt::ResourceSubsetState whole;
  const dxmt::ResourceSubsetState slice(10, 5);

  EXPECT_TRUE(whole.overlapWith(slice));
  EXPECT_TRUE(slice.overlapWith(whole));
}

TEST(ResourceSubset, BufferRangesUseHalfOpenIntervals) {
  const dxmt::ResourceSubsetState first(10, 5);
  const dxmt::ResourceSubsetState overlapping(14, 2);
  const dxmt::ResourceSubsetState adjacent(15, 4);
  const dxmt::ResourceSubsetState empty(12, 0);

  EXPECT_TRUE(first.overlapWith(overlapping));
  EXPECT_TRUE(overlapping.overlapWith(first));
  EXPECT_FALSE(first.overlapWith(adjacent));
  EXPECT_FALSE(adjacent.overlapWith(first));
  EXPECT_FALSE(first.overlapWith(empty));
  EXPECT_FALSE(empty.overlapWith(first));
}

TEST(ResourceSubset, HandlesLargeBufferSumsAndConservativelyWidensOverflow) {
  const dxmt::ResourceSubsetState large(0x70000000u, 0x20000000u);
  const dxmt::ResourceSubsetState overlapping(0x78000000u, 1u);
  EXPECT_TRUE(large.overlapWith(overlapping));
  EXPECT_TRUE(overlapping.overlapWith(large));

  const dxmt::ResourceSubsetState unencodable_offset(0x80000000u, 1u);
  const dxmt::ResourceSubsetState unencodable_length(0u, 0x80000000u);
  const dxmt::ResourceSubsetState ordinary(16u, 4u);
  EXPECT_TRUE(unencodable_offset.overlapWith(ordinary));
  EXPECT_TRUE(ordinary.overlapWith(unencodable_offset));
  EXPECT_TRUE(unencodable_length.overlapWith(ordinary));
  EXPECT_TRUE(ordinary.overlapWith(unencodable_length));
}

dxmt::TextureViewDescriptor
MakeTextureView(WMTPixelFormat format, uint32_t first_mip, uint32_t mip_count,
                uint32_t first_slice, uint32_t slice_count) {
  dxmt::TextureViewDescriptor view = {};
  view.format = format;
  view.type = WMTTextureType2DArray;
  view.firstMiplevel = first_mip;
  view.miplevelCount = mip_count;
  view.firstArraySlice = first_slice;
  view.arraySize = slice_count;
  return view;
}

TEST(ResourceSubset, CompactTextureMaskSeparatesMipsAndArraySlices) {
  const auto first_view = MakeTextureView(WMTPixelFormatRGBA8Unorm, 0, 2, 0, 1);
  const auto overlapping_view =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 1, 1, 0, 1);
  const auto other_slice_view =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 0, 2, 1, 1);

  const dxmt::ResourceSubsetState first(&first_view, 4, 2);
  const dxmt::ResourceSubsetState overlapping(&overlapping_view, 4, 2);
  const dxmt::ResourceSubsetState other_slice(&other_slice_view, 4, 2);
  EXPECT_TRUE(first.overlapWith(overlapping));
  EXPECT_FALSE(first.overlapWith(other_slice));
}

TEST(ResourceSubset, LargeTextureRangeUsesTheSameHalfOpenSemantics) {
  const auto first_view = MakeTextureView(WMTPixelFormatRGBA8Unorm, 2, 3, 4, 2);
  const auto overlapping_view =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 4, 2, 5, 2);
  const auto adjacent_mip_view =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 5, 1, 4, 2);

  const dxmt::ResourceSubsetState first(&first_view, 16, 16);
  const dxmt::ResourceSubsetState overlapping(&overlapping_view, 16, 16);
  const dxmt::ResourceSubsetState adjacent(&adjacent_mip_view, 16, 16);
  EXPECT_TRUE(first.overlapWith(overlapping));
  EXPECT_FALSE(first.overlapWith(adjacent));
}

TEST(ResourceSubset, DepthAndStencilPlanesCanBeTrackedIndependently) {
  const auto view =
      MakeTextureView(WMTPixelFormatDepth32Float_Stencil8, 0, 1, 0, 1);
  const dxmt::ResourceSubsetState depth_only(&view, 1, 1, 0b10);
  const dxmt::ResourceSubsetState stencil_only(&view, 1, 1, 0b01);
  const dxmt::ResourceSubsetState both(&view, 1, 1);

  EXPECT_FALSE(depth_only.overlapWith(stencil_only));
  EXPECT_TRUE(depth_only.overlapWith(both));
  EXPECT_TRUE(stencil_only.overlapWith(both));
}

TEST(ResourceSubset, DifferentSubsetKindsNeverOverlap) {
  const dxmt::ResourceSubsetState buffer(0, 16);
  const auto view =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 0, 1, 0, 1);
  const dxmt::ResourceSubsetState texture(&view, 1, 1);

  EXPECT_FALSE(buffer.overlapWith(texture));
  EXPECT_FALSE(texture.overlapWith(buffer));
}

TEST(ResourceSubset, EmptyTextureRangesNeverOverlap) {
  const auto compact_full =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 0, 1, 0, 1);
  const auto compact_empty_mips =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 0, 0, 0, 1);
  const auto large_full =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 0, 1, 0, 1);
  const auto large_empty_slices =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 0, 1, 0, 0);

  const dxmt::ResourceSubsetState compact(&compact_full, 4, 4);
  const dxmt::ResourceSubsetState compact_empty(&compact_empty_mips, 4, 4);
  const dxmt::ResourceSubsetState large(&large_full, 16, 4);
  const dxmt::ResourceSubsetState large_empty(&large_empty_slices, 16, 4);
  EXPECT_FALSE(compact.overlapWith(compact_empty));
  EXPECT_FALSE(compact_empty.overlapWith(compact));
  EXPECT_FALSE(large.overlapWith(large_empty));
  EXPECT_FALSE(large_empty.overlapWith(large));
}

TEST(ResourceSubset, PreservesSemanticsAcrossCompactMaskBoundary) {
  const auto last_mip =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 14, 1, 3, 1);
  const auto adjacent_mip =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 13, 1, 3, 1);
  const auto other_slice =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 14, 1, 2, 1);

  const dxmt::ResourceSubsetState compact(&last_mip, 15, 4);
  const dxmt::ResourceSubsetState compact_adjacent(&adjacent_mip, 15, 4);
  const dxmt::ResourceSubsetState compact_other_slice(&other_slice, 15, 4);
  EXPECT_TRUE(compact.overlapWith(compact));
  EXPECT_FALSE(compact.overlapWith(compact_adjacent));
  EXPECT_FALSE(compact.overlapWith(compact_other_slice));

  const dxmt::ResourceSubsetState ranged(&last_mip, 15, 5);
  const dxmt::ResourceSubsetState ranged_adjacent(&adjacent_mip, 15, 5);
  const dxmt::ResourceSubsetState ranged_other_slice(&other_slice, 15, 5);
  EXPECT_TRUE(ranged.overlapWith(ranged));
  EXPECT_FALSE(ranged.overlapWith(ranged_adjacent));
  EXPECT_FALSE(ranged.overlapWith(ranged_other_slice));
}

TEST(ResourceSubset, LargeRangesKeepDepthAndStencilPlanesIndependent) {
  const auto view =
      MakeTextureView(WMTPixelFormatDepth32Float_Stencil8, 3, 2, 1, 1);
  const dxmt::ResourceSubsetState depth_only(&view, 16, 2, 0b10);
  const dxmt::ResourceSubsetState stencil_only(&view, 16, 2, 0b01);
  const dxmt::ResourceSubsetState both(&view, 16, 2);

  EXPECT_FALSE(depth_only.overlapWith(stencil_only));
  EXPECT_TRUE(depth_only.overlapWith(both));
  EXPECT_TRUE(stencil_only.overlapWith(both));
}

TEST(ResourceSubset, RepresentsTheLastMipAndArraySliceWithoutTruncation) {
  const auto last_mip =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 15, 1, 0, 1);
  const auto prior_mip =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 14, 1, 0, 1);
  const dxmt::ResourceSubsetState mip(&last_mip, 16, 16);
  const dxmt::ResourceSubsetState same_mip(&last_mip, 16, 16);
  const dxmt::ResourceSubsetState adjacent_mip(&prior_mip, 16, 16);
  EXPECT_TRUE(mip.overlapWith(same_mip));
  EXPECT_FALSE(mip.overlapWith(adjacent_mip));

  const auto last_slice =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 0, 1, 2047, 1);
  const auto prior_slice =
      MakeTextureView(WMTPixelFormatRGBA8Unorm, 0, 1, 2046, 1);
  const dxmt::ResourceSubsetState slice(&last_slice, 1, 2048);
  const dxmt::ResourceSubsetState same_slice(&last_slice, 1, 2048);
  const dxmt::ResourceSubsetState adjacent_slice(&prior_slice, 1, 2048);
  EXPECT_TRUE(slice.overlapWith(same_slice));
  EXPECT_FALSE(slice.overlapWith(adjacent_slice));
}

TEST(ResourceSubset, PreservesPlaneMasksAtTheCompactRangeBoundary) {
  const auto view =
      MakeTextureView(WMTPixelFormatDepth32Float_Stencil8, 30, 1, 0, 1);
  const dxmt::ResourceSubsetState compact_depth(&view, 31, 1, 0b10);
  const dxmt::ResourceSubsetState compact_stencil(&view, 31, 1, 0b01);
  const dxmt::ResourceSubsetState compact_both(&view, 31, 1);
  EXPECT_FALSE(compact_depth.overlapWith(compact_stencil));
  EXPECT_TRUE(compact_depth.overlapWith(compact_both));
  EXPECT_TRUE(compact_stencil.overlapWith(compact_both));

  const dxmt::ResourceSubsetState ranged_depth(&view, 32, 1, 0b10);
  const dxmt::ResourceSubsetState ranged_stencil(&view, 32, 1, 0b01);
  const dxmt::ResourceSubsetState ranged_both(&view, 32, 1);
  EXPECT_FALSE(ranged_depth.overlapWith(ranged_stencil));
  EXPECT_TRUE(ranged_depth.overlapWith(ranged_both));
  EXPECT_TRUE(ranged_stencil.overlapWith(ranged_both));
}

} // namespace
