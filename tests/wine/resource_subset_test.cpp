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

} // namespace
