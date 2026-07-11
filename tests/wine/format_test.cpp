#include <dxmt_test.hpp>

#include "dxmt_format.hpp"

#include <dxgi.h>

#include <cstdint>

extern "C" bool MTLDevice_supportsFamily(obj_handle_t, WMTGPUFamily) {
  return false;
}

extern "C" bool MTLDevice_supportsBCTextureCompression(obj_handle_t) {
  return false;
}

namespace {

struct SrgbPair {
  WMTPixelFormat linear;
  WMTPixelFormat srgb;
};

class SrgbFormatTest : public testing::TestWithParam<SrgbPair> {};

struct FormatQueryCase {
  uint32_t dxgi;
  WMTPixelFormat metal;
  uint32_t element_size;
  uint32_t required_flags;
};

class FormatQueryTest : public testing::TestWithParam<FormatQueryCase> {};

} // namespace

TEST_P(SrgbFormatTest, ConvertsBothDirectionsWithoutChangingLinearIdentity) {
  const auto &[linear, srgb] = GetParam();
  EXPECT_EQ(dxmt::Forget_sRGB(srgb), linear);
  EXPECT_EQ(dxmt::Recall_sRGB(linear), srgb);
  EXPECT_TRUE(dxmt::Is_sRGBVariant(srgb));
  EXPECT_FALSE(dxmt::Is_sRGBVariant(linear));
}

INSTANTIATE_TEST_SUITE_P(
    SupportedPairs, SrgbFormatTest,
    testing::Values(
        SrgbPair{WMTPixelFormatR8Unorm, WMTPixelFormatR8Unorm_sRGB},
        SrgbPair{WMTPixelFormatRG8Unorm, WMTPixelFormatRG8Unorm_sRGB},
        SrgbPair{WMTPixelFormatRGBA8Unorm, WMTPixelFormatRGBA8Unorm_sRGB},
        SrgbPair{WMTPixelFormatBGRA8Unorm, WMTPixelFormatBGRA8Unorm_sRGB},
        SrgbPair{WMTPixelFormatBGRX8Unorm, WMTPixelFormatBGRX8Unorm_sRGB},
        SrgbPair{WMTPixelFormatBGR10_XR, WMTPixelFormatBGR10_XR_sRGB},
        SrgbPair{WMTPixelFormatBGRA10_XR, WMTPixelFormatBGRA10_XR_sRGB},
        SrgbPair{WMTPixelFormatBC1_RGBA, WMTPixelFormatBC1_RGBA_sRGB},
        SrgbPair{WMTPixelFormatBC2_RGBA, WMTPixelFormatBC2_RGBA_sRGB},
        SrgbPair{WMTPixelFormatBC3_RGBA, WMTPixelFormatBC3_RGBA_sRGB},
        SrgbPair{WMTPixelFormatBC7_RGBAUnorm, WMTPixelFormatBC7_RGBAUnorm_sRGB},
        SrgbPair{WMTPixelFormatPVRTC_RGBA_2BPP,
                 WMTPixelFormatPVRTC_RGBA_2BPP_sRGB},
        SrgbPair{WMTPixelFormatPVRTC_RGBA_4BPP,
                 WMTPixelFormatPVRTC_RGBA_4BPP_sRGB},
        SrgbPair{WMTPixelFormatPVRTC_RGB_2BPP,
                 WMTPixelFormatPVRTC_RGB_2BPP_sRGB},
        SrgbPair{WMTPixelFormatPVRTC_RGB_4BPP,
                 WMTPixelFormatPVRTC_RGB_4BPP_sRGB},
        SrgbPair{WMTPixelFormatEAC_RGBA8, WMTPixelFormatEAC_RGBA8_sRGB},
        SrgbPair{WMTPixelFormatETC2_RGB8, WMTPixelFormatETC2_RGB8_sRGB},
        SrgbPair{WMTPixelFormatETC2_RGB8A1, WMTPixelFormatETC2_RGB8A1_sRGB}));

TEST(MetalFormat, ClassifiesBlockCompressionAndDepthStencilPlanes) {
  for (const auto format :
       {WMTPixelFormatBC1_RGBA, WMTPixelFormatBC3_RGBA_sRGB,
        WMTPixelFormatBC4_RSnorm, WMTPixelFormatBC5_RGUnorm,
        WMTPixelFormatBC6H_RGBFloat, WMTPixelFormatBC7_RGBAUnorm})
    EXPECT_TRUE(dxmt::IsBlockCompressionFormat(format));

  EXPECT_FALSE(dxmt::IsBlockCompressionFormat(WMTPixelFormatRGBA8Unorm));
  EXPECT_FALSE(dxmt::IsBlockCompressionFormat(WMTPixelFormatPVRTC_RGBA_4BPP));
  EXPECT_EQ(dxmt::DepthStencilPlanarFlags(WMTPixelFormatDepth32Float_Stencil8),
            3u);
  EXPECT_EQ(dxmt::DepthStencilPlanarFlags(WMTPixelFormatDepth32Float), 1u);
  EXPECT_EQ(dxmt::DepthStencilPlanarFlags(WMTPixelFormatX32_Stencil8), 2u);
  EXPECT_EQ(dxmt::DepthStencilPlanarFlags(WMTPixelFormatRGBA8Unorm), 0u);
}

TEST_P(FormatQueryTest, MapsDxgiFormatToExpectedMetalRecord) {
  const auto &[dxgi, metal, element_size, required_flags] = GetParam();
  dxmt::MTL_DXGI_FORMAT_DESC description = {};
  ASSERT_EQ(dxmt::MTLQueryDXGIFormat({}, dxgi, description), S_OK);
  EXPECT_EQ(description.PixelFormat, metal);
  EXPECT_EQ(description.BytesPerTexel, element_size);
  EXPECT_EQ(description.Flag & required_flags, required_flags);
}

INSTANTIATE_TEST_SUITE_P(
    RepresentativeFormats, FormatQueryTest,
    testing::Values(
        FormatQueryCase{DXGI_FORMAT_R8G8B8A8_UNORM, WMTPixelFormatRGBA8Unorm, 4,
                        dxmt::MTL_DXGI_FORMAT_BACKBUFFER},
        FormatQueryCase{DXGI_FORMAT_BC1_UNORM, WMTPixelFormatBC1_RGBA, 8,
                        dxmt::MTL_DXGI_FORMAT_BC},
        FormatQueryCase{DXGI_FORMAT_BC3_UNORM, WMTPixelFormatBC3_RGBA, 16,
                        dxmt::MTL_DXGI_FORMAT_BC},
        FormatQueryCase{DXGI_FORMAT_D32_FLOAT, WMTPixelFormatDepth32Float, 4,
                        dxmt::MTL_DXGI_FORMAT_DEPTH_PLANER},
        FormatQueryCase{DXGI_FORMAT_R24G8_TYPELESS,
                        WMTPixelFormatDepth32Float_Stencil8, 4,
                        dxmt::MTL_DXGI_FORMAT_TYPELESS |
                            dxmt::MTL_DXGI_FORMAT_DEPTH_PLANER |
                            dxmt::MTL_DXGI_FORMAT_STENCIL_PLANER},
        FormatQueryCase{DXGI_FORMAT_NV12, WMTPixelFormatR8Unorm, 1,
                        dxmt::MTL_DXGI_FORMAT_TYPELESS}));

TEST(DxgiFormat, RejectsUnknownMappingAndResetsDescription) {
  dxmt::MTL_DXGI_FORMAT_DESC description = {
      .PixelFormat = WMTPixelFormatRGBA32Float,
      .AttributeFormat = WMTAttributeFormatFloat4,
      .BytesPerTexel = 16,
      .Flag = 0xffffffffu,
  };
  EXPECT_EQ(dxmt::MTLQueryDXGIFormat({}, DXGI_FORMAT_UNKNOWN, description),
            E_FAIL);
  EXPECT_EQ(description.PixelFormat, WMTPixelFormatInvalid);
  EXPECT_EQ(description.AttributeFormat, WMTAttributeFormatInvalid);
  EXPECT_EQ(description.BytesPerTexel, 0u);
  EXPECT_EQ(description.Flag, 0u);
}

TEST(DxgiFormatTraits, DescribesNativeDepthVideoMaskAndUnsupportedClasses) {
  const auto native = dxmt::GetDXGIFormatTraits(DXGI_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(native.classification, dxmt::DXGIFormatClass::Native);
  EXPECT_EQ(native.planeCount, 1u);
  EXPECT_EQ(native.planes[0].backingFormat, DXGI_FORMAT_R8G8B8A8_UNORM);

  const auto depth =
      dxmt::GetDXGIFormatTraits(DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
  EXPECT_EQ(depth.flags, dxmt::DXGI_FORMAT_TRAIT_DEPTH_STENCIL);
  EXPECT_EQ(depth.planeCount, 2u);
  EXPECT_EQ(depth.planes[0].backingFormat, DXGI_FORMAT_R32_TYPELESS);
  EXPECT_EQ(depth.planes[0].elementSize, 4u);
  EXPECT_EQ(depth.planes[1].backingFormat, DXGI_FORMAT_R8_TYPELESS);
  EXPECT_EQ(depth.planes[1].elementSize, 1u);

  const auto nv12 = dxmt::GetDXGIFormatTraits(DXGI_FORMAT_NV12);
  EXPECT_EQ(nv12.classification, dxmt::DXGIFormatClass::Emulated);
  EXPECT_EQ(nv12.flags,
            dxmt::DXGI_FORMAT_TRAIT_MULTIPLANE | dxmt::DXGI_FORMAT_TRAIT_VIDEO);
  EXPECT_EQ(nv12.planeCount, 2u);
  EXPECT_EQ(nv12.planes[1].elementSize, 2u);
  EXPECT_EQ(nv12.planes[1].subsampleXLog2, 1u);
  EXPECT_EQ(nv12.planes[1].subsampleYLog2, 1u);

  EXPECT_EQ(dxmt::GetDXGIFormatTraits(DXGI_FORMAT_R1_UNORM).classification,
            dxmt::DXGIFormatClass::Mask);
  EXPECT_EQ(dxmt::GetDXGIFormatTraits(DXGI_FORMAT_UNKNOWN).classification,
            dxmt::DXGIFormatClass::Unsupported);
  EXPECT_TRUE(dxmt::IsDXGIFormatSupportedByTraits(DXGI_FORMAT_NV12));
  EXPECT_FALSE(dxmt::IsDXGIFormatSupportedByTraits(DXGI_FORMAT_R1_UNORM));
}

TEST(DxgiFormatTraits, ComputesWholeAndPerPlaneFootprints) {
  dxmt::DXGIFormatFootprintLayout footprint;
  ASSERT_TRUE(
      dxmt::GetDXGIFormatFootprintLayout(DXGI_FORMAT_BC1_UNORM, footprint));
  EXPECT_EQ(footprint.blockWidth, 4u);
  EXPECT_EQ(footprint.blockHeight, 4u);
  EXPECT_EQ(footprint.elementSize, 8u);

  ASSERT_TRUE(dxmt::GetDXGIFormatFootprintLayout(DXGI_FORMAT_R8G8B8A8_UNORM,
                                                 footprint));
  EXPECT_EQ(footprint.blockWidth, 1u);
  EXPECT_EQ(footprint.blockHeight, 1u);
  EXPECT_EQ(footprint.elementSize, 4u);

  dxmt::DXGIFormatPlaneFootprintLayout plane;
  ASSERT_TRUE(
      dxmt::GetDXGIFormatPlaneFootprintLayout(DXGI_FORMAT_NV12, 0, plane));
  EXPECT_EQ(plane.elementSize, 1u);
  ASSERT_TRUE(
      dxmt::GetDXGIFormatPlaneFootprintLayout(DXGI_FORMAT_NV12, 1, plane));
  EXPECT_EQ(plane.elementSize, 2u);
  EXPECT_FALSE(
      dxmt::GetDXGIFormatPlaneFootprintLayout(DXGI_FORMAT_NV12, 2, plane));
  EXPECT_FALSE(dxmt::GetDXGIFormatPlaneFootprintLayout(
      DXGI_FORMAT_R8G8B8A8_UNORM, 1, plane));
}

TEST(DxgiFormatTraits, ValidatesVideoAndDepthStencilPlaneViews) {
  EXPECT_TRUE(dxmt::IsDXGIFormatPlaneCompatible(DXGI_FORMAT_NV12,
                                                DXGI_FORMAT_R8_UNORM, 0));
  EXPECT_TRUE(dxmt::IsDXGIFormatPlaneCompatible(DXGI_FORMAT_NV12,
                                                DXGI_FORMAT_R8G8_UNORM, 1));
  EXPECT_FALSE(dxmt::IsDXGIFormatPlaneCompatible(DXGI_FORMAT_NV12,
                                                 DXGI_FORMAT_R8_UNORM, 1));
  EXPECT_FALSE(dxmt::IsDXGIFormatPlaneCompatible(DXGI_FORMAT_NV12,
                                                 DXGI_FORMAT_R8G8_UNORM, 2));

  EXPECT_TRUE(dxmt::IsDXGIFormatPlaneCompatible(DXGI_FORMAT_P010,
                                                DXGI_FORMAT_R16_UNORM, 0));
  EXPECT_TRUE(dxmt::IsDXGIFormatPlaneCompatible(DXGI_FORMAT_P010,
                                                DXGI_FORMAT_R16G16_UNORM, 1));
  EXPECT_TRUE(dxmt::IsDXGIFormatPlaneCompatible(
      DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_TYPELESS, 0));
  EXPECT_TRUE(dxmt::IsDXGIFormatPlaneCompatible(
      DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R8_TYPELESS, 1));
  EXPECT_FALSE(dxmt::IsDXGIFormatPlaneCompatible(DXGI_FORMAT_UNKNOWN,
                                                 DXGI_FORMAT_UNKNOWN, 0));
}

TEST(MetalFormat, ReportsTexelSizeIntegerClassAndUnsignedEquivalent) {
  EXPECT_EQ(dxmt::MTLGetTexelSize(WMTPixelFormatR8Unorm), 1u);
  EXPECT_EQ(dxmt::MTLGetTexelSize(WMTPixelFormatRG8Unorm), 2u);
  EXPECT_EQ(dxmt::MTLGetTexelSize(WMTPixelFormatRGBA8Unorm), 4u);
  EXPECT_EQ(dxmt::MTLGetTexelSize(WMTPixelFormatRGBA16Float), 8u);
  EXPECT_EQ(dxmt::MTLGetTexelSize(WMTPixelFormatRGBA32Float), 16u);
  EXPECT_EQ(dxmt::MTLGetTexelSize(WMTPixelFormatInvalid), 0u);

  EXPECT_EQ(dxmt::MTLGetUnsignedIntegerFormat(WMTPixelFormatR8Snorm),
            WMTPixelFormatR8Uint);
  EXPECT_EQ(dxmt::MTLGetUnsignedIntegerFormat(WMTPixelFormatRGBA8Unorm_sRGB),
            WMTPixelFormatRGBA8Uint);
  EXPECT_EQ(dxmt::MTLGetUnsignedIntegerFormat(WMTPixelFormatRGBA32Float),
            WMTPixelFormatRGBA32Uint);
  EXPECT_EQ(dxmt::MTLGetUnsignedIntegerFormat(WMTPixelFormatBC1_RGBA),
            WMTPixelFormatInvalid);

  EXPECT_TRUE(dxmt::IsIntegerFormat(WMTPixelFormatRGBA16Sint));
  EXPECT_TRUE(dxmt::IsIntegerFormat(WMTPixelFormatStencil8));
  EXPECT_FALSE(dxmt::IsIntegerFormat(WMTPixelFormatRGBA16Float));
  EXPECT_TRUE(dxmt::IsUnorm8RenderTargetFormat(WMTPixelFormatBGRA8Unorm));
  EXPECT_FALSE(dxmt::IsUnorm8RenderTargetFormat(WMTPixelFormatBGRA8Unorm_sRGB));
}
