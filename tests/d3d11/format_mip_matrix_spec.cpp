#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// D3D11 Texture2D format / mip / GenerateMips matrices.
// Public D3D11 / DXGI API only. Staging Map readback with real pixel checks.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr UINT kFormatRtWidth = 8;
constexpr UINT kFormatRtHeight = 8;
constexpr UINT kMipBaseSize = 16;
constexpr UINT kMipLevels = 5; // 16, 8, 4, 2, 1

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

bool ColorMatchesRgba8(uint32_t actual, uint32_t expected, unsigned tolerance) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    const int actual_channel = (actual >> shift) & 0xff;
    const int expected_channel = (expected >> shift) & 0xff;
    if (std::abs(actual_channel - expected_channel) >
        static_cast<int>(tolerance))
      return false;
  }
  return true;
}

// Pack float RGBA [0,1] into little-endian R8G8B8A8_UNORM (0xAABBGGRR).
uint32_t PackR8G8B8A8(float r, float g, float b, float a) {
  const auto quantize = [](float v) -> uint32_t {
    return static_cast<uint32_t>(
        std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  return quantize(r) | (quantize(g) << 8) | (quantize(b) << 16) |
         (quantize(a) << 24);
}

// Pack float RGBA [0,1] into little-endian B8G8R8A8_UNORM (0xAARRGGBB).
uint32_t PackB8G8R8A8(float r, float g, float b, float a) {
  const auto quantize = [](float v) -> uint32_t {
    return static_cast<uint32_t>(
        std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  return quantize(b) | (quantize(g) << 8) | (quantize(r) << 16) |
         (quantize(a) << 24);
}

// IEEE-754 binary16 for finite values in a range usable by these tests.
uint16_t FloatToHalf(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const uint32_t sign = (bits >> 16) & 0x8000u;
  int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
  uint32_t mantissa = bits & 0x7fffffu;
  if (exponent <= 0) {
    if (exponent < -10)
      return static_cast<uint16_t>(sign);
    mantissa |= 0x800000u;
    const uint32_t shift = static_cast<uint32_t>(14 - exponent);
    const uint32_t half_m = mantissa >> shift;
    return static_cast<uint16_t>(sign | half_m);
  }
  if (exponent >= 31)
    return static_cast<uint16_t>(sign | 0x7c00u);
  return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) |
                              (mantissa >> 13));
}

float HalfToFloat(uint16_t half) {
  const uint32_t sign = (static_cast<uint32_t>(half) & 0x8000u) << 16;
  uint32_t exponent = (half >> 10) & 0x1fu;
  uint32_t mantissa = half & 0x3ffu;
  uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      exponent = 1;
      while ((mantissa & 0x400u) == 0) {
        mantissa <<= 1;
        --exponent;
      }
      mantissa &= 0x3ffu;
      bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
    }
  } else if (exponent == 31) {
    bits = sign | 0x7f800000u | (mantissa << 13);
  } else {
    bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
  }
  float out = 0.0f;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

const char *FormatName(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R8G8B8A8_UNORM:
    return "R8G8B8A8_UNORM";
  case DXGI_FORMAT_B8G8R8A8_UNORM:
    return "B8G8R8A8_UNORM";
  case DXGI_FORMAT_R32_FLOAT:
    return "R32_FLOAT";
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
    return "R16G16B16A16_FLOAT";
  default:
    return "Unknown";
  }
}

UINT FormatBytesPerPixel(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_R32_FLOAT:
    return 4;
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
    return 8;
  default:
    return 0;
  }
}

// ---------------------------------------------------------------------------
// 1) Texture2D format matrix: clear / update + staging readback
// ---------------------------------------------------------------------------

struct FormatMatrixCase {
  DXGI_FORMAT format;
  float clear_r;
  float clear_g;
  float clear_b;
  float clear_a;
  // When true, prefer ClearRenderTargetView; otherwise UpdateSubresource.
  bool prefer_clear;
};

class D3D11FormatMatrixSpec
    : public ::testing::TestWithParam<FormatMatrixCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_P(D3D11FormatMatrixSpec, ClearOrUpdateAndReadbackFirstPixel) {
  const auto &test = GetParam();
  const UINT bpp = FormatBytesPerPixel(test.format);
  ASSERT_GT(bpp, 0u);

  UINT format_support = 0;
  const HRESULT support_hr =
      context_.device()->CheckFormatSupport(test.format, &format_support);
  if (FAILED(support_hr))
    GTEST_SKIP() << FormatName(test.format)
                 << " CheckFormatSupport failed: 0x" << std::hex
                 << static_cast<unsigned long>(support_hr);

  const bool can_texture =
      (format_support & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;
  if (!can_texture)
    GTEST_SKIP() << FormatName(test.format) << " is not TEXTURE2D-capable";

  const bool can_rt =
      (format_support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
  bool path_clear = test.prefer_clear && can_rt;

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = kFormatRtWidth;
  desc.Height = kFormatRtHeight;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = test.format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = path_clear ? D3D11_BIND_RENDER_TARGET : 0;

  ComPtr<ID3D11Texture2D> texture;
  HRESULT create_hr =
      context_.device()->CreateTexture2D(&desc, nullptr, texture.put());
  if (FAILED(create_hr) && path_clear) {
    // Fall back to UpdateSubresource when RT bind create is rejected.
    path_clear = false;
    desc.BindFlags = 0;
    texture.reset();
    create_hr =
        context_.device()->CreateTexture2D(&desc, nullptr, texture.put());
  }
  if (FAILED(create_hr))
    GTEST_SKIP() << FormatName(test.format)
                 << " CreateTexture2D failed: 0x" << std::hex
                 << static_cast<unsigned long>(create_hr);

  D3D11_TEXTURE2D_DESC created = {};
  texture->GetDesc(&created);

  if (path_clear) {
    ComPtr<ID3D11RenderTargetView> rtv;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
        texture.get(), nullptr, rtv.put())));
    const float color[4] = {test.clear_r, test.clear_g, test.clear_b,
                            test.clear_a};
    context_.context()->ClearRenderTargetView(rtv.get(), color);
  } else {
    // Upload a solid surface with the known value via UpdateSubresource.
    std::vector<uint8_t> upload(
        static_cast<size_t>(kFormatRtWidth) * kFormatRtHeight * bpp, 0);
    if (test.format == DXGI_FORMAT_R8G8B8A8_UNORM) {
      const uint32_t packed =
          PackR8G8B8A8(test.clear_r, test.clear_g, test.clear_b, test.clear_a);
      for (UINT i = 0; i < kFormatRtWidth * kFormatRtHeight; ++i)
        std::memcpy(upload.data() + i * 4, &packed, 4);
    } else if (test.format == DXGI_FORMAT_B8G8R8A8_UNORM) {
      const uint32_t packed =
          PackB8G8R8A8(test.clear_r, test.clear_g, test.clear_b, test.clear_a);
      for (UINT i = 0; i < kFormatRtWidth * kFormatRtHeight; ++i)
        std::memcpy(upload.data() + i * 4, &packed, 4);
    } else if (test.format == DXGI_FORMAT_R32_FLOAT) {
      for (UINT i = 0; i < kFormatRtWidth * kFormatRtHeight; ++i)
        std::memcpy(upload.data() + i * 4, &test.clear_r, 4);
    } else if (test.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
      const uint16_t halves[4] = {
          FloatToHalf(test.clear_r), FloatToHalf(test.clear_g),
          FloatToHalf(test.clear_b), FloatToHalf(test.clear_a)};
      for (UINT i = 0; i < kFormatRtWidth * kFormatRtHeight; ++i)
        std::memcpy(upload.data() + i * 8, halves, 8);
    }
    context_.context()->UpdateSubresource(texture.get(), 0, nullptr,
                                          upload.data(), kFormatRtWidth * bpp,
                                          0);
  }

  D3D11_TEXTURE2D_DESC staging_desc = created;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  const auto *row0 = static_cast<const uint8_t *>(mapped.pData);

  if (test.format == DXGI_FORMAT_R8G8B8A8_UNORM) {
    uint32_t actual = 0;
    std::memcpy(&actual, row0, 4);
    const uint32_t expected =
        PackR8G8B8A8(test.clear_r, test.clear_g, test.clear_b, test.clear_a);
    EXPECT_TRUE(ColorMatchesRgba8(actual, expected, 1))
        << FormatName(test.format) << " actual=0x" << std::hex << actual
        << " expected=0x" << expected;
  } else if (test.format == DXGI_FORMAT_B8G8R8A8_UNORM) {
    uint32_t actual = 0;
    std::memcpy(&actual, row0, 4);
    const uint32_t expected =
        PackB8G8R8A8(test.clear_r, test.clear_g, test.clear_b, test.clear_a);
    EXPECT_TRUE(ColorMatchesRgba8(actual, expected, 1))
        << FormatName(test.format) << " actual=0x" << std::hex << actual
        << " expected=0x" << expected;
  } else if (test.format == DXGI_FORMAT_R32_FLOAT) {
    float actual = 0.0f;
    std::memcpy(&actual, row0, 4);
    EXPECT_NEAR(actual, test.clear_r, 1e-5f)
        << FormatName(test.format) << " R32 first pixel";
  } else if (test.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
    uint16_t halves[4] = {};
    std::memcpy(halves, row0, 8);
    EXPECT_NEAR(HalfToFloat(halves[0]), test.clear_r, 1e-2f) << "R";
    EXPECT_NEAR(HalfToFloat(halves[1]), test.clear_g, 1e-2f) << "G";
    EXPECT_NEAR(HalfToFloat(halves[2]), test.clear_b, 1e-2f) << "B";
    EXPECT_NEAR(HalfToFloat(halves[3]), test.clear_a, 1e-2f) << "A";
  }

  // Spot-check a second pixel so we are not only validating the origin.
  if (mapped.RowPitch >= bpp * 2 || kFormatRtWidth > 1) {
    const uint8_t *pixel1 =
        row0 + (kFormatRtWidth > 1 ? bpp : mapped.RowPitch);
    if (test.format == DXGI_FORMAT_R8G8B8A8_UNORM) {
      uint32_t actual = 0;
      std::memcpy(&actual, pixel1, 4);
      EXPECT_TRUE(ColorMatchesRgba8(
          actual,
          PackR8G8B8A8(test.clear_r, test.clear_g, test.clear_b, test.clear_a),
          1));
    } else if (test.format == DXGI_FORMAT_R32_FLOAT) {
      float actual = 0.0f;
      std::memcpy(&actual, pixel1, 4);
      EXPECT_NEAR(actual, test.clear_r, 1e-5f);
    }
  }

  context_.context()->Unmap(staging.get(), 0);
}

INSTANTIATE_TEST_SUITE_P(
    Texture2DFormats, D3D11FormatMatrixSpec,
    ::testing::Values(
        FormatMatrixCase{DXGI_FORMAT_R8G8B8A8_UNORM, 0.25f, 0.5f, 0.75f, 1.0f,
                         true},
        FormatMatrixCase{DXGI_FORMAT_B8G8R8A8_UNORM, 1.0f, 0.0f, 0.5f, 1.0f,
                         true},
        FormatMatrixCase{DXGI_FORMAT_R32_FLOAT, 0.375f, 0.0f, 0.0f, 0.0f, true},
        FormatMatrixCase{DXGI_FORMAT_R16G16B16A16_FLOAT, 0.5f, 0.25f, 0.125f,
                         1.0f, true}),
    [](const ::testing::TestParamInfo<FormatMatrixCase> &info) {
      return std::string(FormatName(info.param.format));
    });

// ---------------------------------------------------------------------------
// 2) Mip chain: UpdateSubresource each mip, staging copy per subresource
// ---------------------------------------------------------------------------

class D3D11MipChainSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

// Unique solid color per mip level (R8G8B8A8).
uint32_t MipUniqueColor(UINT mip) {
  // Distinct opaque colors so first-pixel checks cannot cross-match.
  static constexpr std::array<uint32_t, kMipLevels> kColors = {
      0xff101010u, // mip 0: dark gray
      0xff2030ffu, // mip 1: red-ish (R channel high in LE R8G8B8A8)
      0xff30ff20u, // mip 2: green-ish
      0xffff4020u, // mip 3: blue-ish
      0xffc0c0ffu, // mip 4: light red
  };
  return kColors[mip % kColors.size()];
}

TEST_F(D3D11MipChainSpec,
       UpdateEachMipAndReadbackFirstPixelViaStagingSubresource) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = kMipBaseSize;
  desc.Height = kMipBaseSize;
  desc.MipLevels = kMipLevels;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&desc, nullptr, texture.put())));

  D3D11_TEXTURE2D_DESC created = {};
  texture->GetDesc(&created);
  ASSERT_EQ(created.MipLevels, kMipLevels);
  ASSERT_EQ(created.Width, kMipBaseSize);
  ASSERT_EQ(created.Height, kMipBaseSize);

  for (UINT mip = 0; mip < kMipLevels; ++mip) {
    const UINT mip_w = std::max(1u, kMipBaseSize >> mip);
    const UINT mip_h = std::max(1u, kMipBaseSize >> mip);
    const uint32_t color = MipUniqueColor(mip);
    std::vector<uint32_t> pixels(static_cast<size_t>(mip_w) * mip_h, color);
    context_.context()->UpdateSubresource(
        texture.get(), mip, nullptr, pixels.data(),
        mip_w * static_cast<UINT>(sizeof(uint32_t)), 0);
  }

  // Staging texture with matching mip chain; copy each subresource and
  // verify the first texel of every mip.
  D3D11_TEXTURE2D_DESC staging_desc = created;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));

  for (UINT mip = 0; mip < kMipLevels; ++mip) {
    // CopySubresourceRegion path (full subresource when pSrcBox is null).
    context_.context()->CopySubresourceRegion(staging.get(), mip, 0, 0, 0,
                                              texture.get(), mip, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        staging.get(), mip, D3D11_MAP_READ, 0, &mapped)))
        << "Map failed for mip " << mip;
    uint32_t actual = 0;
    std::memcpy(&actual, mapped.pData, sizeof(actual));
    context_.context()->Unmap(staging.get(), mip);

    EXPECT_EQ(actual, MipUniqueColor(mip))
        << "mip " << mip << " first pixel via CopySubresourceRegion";
  }

  // Also exercise CopyResource for the full chain and re-check mip 0 and last.
  context_.context()->CopyResource(staging.get(), texture.get());
  for (UINT mip : {0u, kMipLevels - 1u}) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        staging.get(), mip, D3D11_MAP_READ, 0, &mapped)))
        << "Map after CopyResource failed for mip " << mip;
    uint32_t actual = 0;
    std::memcpy(&actual, mapped.pData, sizeof(actual));
    context_.context()->Unmap(staging.get(), mip);
    EXPECT_EQ(actual, MipUniqueColor(mip))
        << "mip " << mip << " first pixel via CopyResource";
  }
}

TEST_F(D3D11MipChainSpec, AutoMipLevelsCreatesFullChainFromBaseSize) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = kMipBaseSize;
  desc.Height = kMipBaseSize;
  desc.MipLevels = 0; // auto-generate full chain
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&desc, nullptr, texture.put())));

  D3D11_TEXTURE2D_DESC created = {};
  texture->GetDesc(&created);
  // 16→1 is exactly 5 levels.
  EXPECT_EQ(created.MipLevels, kMipLevels);

  // Write only mip 0 and a mid mip; verify both survive staging readback.
  {
    const uint32_t color0 = MipUniqueColor(0);
    std::vector<uint32_t> pixels(static_cast<size_t>(kMipBaseSize) *
                                     kMipBaseSize,
                                 color0);
    context_.context()->UpdateSubresource(
        texture.get(), 0, nullptr, pixels.data(),
        kMipBaseSize * static_cast<UINT>(sizeof(uint32_t)), 0);
  }
  {
    const UINT mip = 2;
    const UINT mip_w = kMipBaseSize >> mip;
    const uint32_t color = MipUniqueColor(mip);
    std::vector<uint32_t> pixels(static_cast<size_t>(mip_w) * mip_w, color);
    context_.context()->UpdateSubresource(
        texture.get(), mip, nullptr, pixels.data(),
        mip_w * static_cast<UINT>(sizeof(uint32_t)), 0);
  }

  D3D11_TEXTURE2D_DESC staging_desc = created;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  for (UINT mip : {0u, 2u}) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        staging.get(), mip, D3D11_MAP_READ, 0, &mapped)));
    uint32_t actual = 0;
    std::memcpy(&actual, mapped.pData, sizeof(actual));
    context_.context()->Unmap(staging.get(), mip);
    EXPECT_EQ(actual, MipUniqueColor(mip)) << "auto-mip chain mip " << mip;
  }
}

// ---------------------------------------------------------------------------
// 3) GenerateMips: top mip write + autogen lower mips
// ---------------------------------------------------------------------------

class D3D11GenerateMipsSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11GenerateMipsSpec,
       WritesTopMipAndFillsLowerMipAwayFromPoison) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  UINT format_support = 0;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CheckFormatSupport(format, &format_support)));
  const UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D |
                        D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                        D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
                        D3D11_FORMAT_SUPPORT_MIP_AUTOGEN;
  if ((format_support & required) != required) {
    GTEST_SKIP() << "R8G8B8A8_UNORM does not advertise MIP_AUTOGEN+RT+SRV "
                    "support (support=0x"
                 << std::hex << format_support << ")";
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = kMipBaseSize;
  desc.Height = kMipBaseSize;
  desc.MipLevels = 0; // full chain
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

  ComPtr<ID3D11Texture2D> texture;
  const HRESULT create_hr =
      context_.device()->CreateTexture2D(&desc, nullptr, texture.put());
  if (FAILED(create_hr))
    GTEST_SKIP() << "CreateTexture2D(GENERATE_MIPS) failed: 0x" << std::hex
                 << static_cast<unsigned long>(create_hr);

  D3D11_TEXTURE2D_DESC created = {};
  texture->GetDesc(&created);
  ASSERT_GE(created.MipLevels, 2u);

  // Poison every mip with a distinctive pattern so we can detect autogen.
  constexpr uint32_t kPoison = 0xff00ff00u; // pure green in R8G8B8A8 LE packing
  for (UINT mip = 0; mip < created.MipLevels; ++mip) {
    const UINT mip_w = std::max(1u, created.Width >> mip);
    const UINT mip_h = std::max(1u, created.Height >> mip);
    std::vector<uint32_t> pixels(static_cast<size_t>(mip_w) * mip_h, kPoison);
    context_.context()->UpdateSubresource(
        texture.get(), mip, nullptr, pixels.data(),
        mip_w * static_cast<UINT>(sizeof(uint32_t)), 0);
  }

  // Write a solid non-poison color into the top mip via RTV clear.
  constexpr float kTopColor[4] = {1.0f, 0.0f, 0.0f, 1.0f}; // red
  constexpr uint32_t kTopPacked = 0xff0000ffu;             // R8G8B8A8 red
  {
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = format;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    ComPtr<ID3D11RenderTargetView> rtv;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRenderTargetView(
        texture.get(), &rtv_desc, rtv.put())));
    context_.context()->ClearRenderTargetView(rtv.get(), kTopColor);
  }

  // Full-mip-chain SRV is required for GenerateMips.
  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = format;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MostDetailedMip = 0;
  srv_desc.Texture2D.MipLevels = static_cast<UINT>(-1);
  ComPtr<ID3D11ShaderResourceView> srv;
  const HRESULT srv_hr = context_.device()->CreateShaderResourceView(
      texture.get(), &srv_desc, srv.put());
  if (FAILED(srv_hr))
    GTEST_SKIP() << "CreateShaderResourceView(full mips) failed: 0x" << std::hex
                 << static_cast<unsigned long>(srv_hr);

  // GenerateMips is void; treat a subsequent lower-mip still-poison as a
  // soft skip only when the driver reports no autogen after the call fails
  // to change anything AND top mip itself did not stick. Otherwise assert.
  context_.context()->GenerateMips(srv.get());

  D3D11_TEXTURE2D_DESC staging_desc = created;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  // Top mip must be the written red.
  {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(
        context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
    uint32_t actual = 0;
    std::memcpy(&actual, mapped.pData, sizeof(actual));
    context_.context()->Unmap(staging.get(), 0);
    EXPECT_TRUE(ColorMatchesRgba8(actual, kTopPacked, 1))
        << "top mip after GenerateMips setup actual=0x" << std::hex << actual;
  }

  // Lower mip (level 1) must leave the poison color. For a solid top mip,
  // autogen should produce approximately the same solid red.
  {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(
        context_.context()->Map(staging.get(), 1, D3D11_MAP_READ, 0, &mapped)));
    uint32_t actual = 0;
    std::memcpy(&actual, mapped.pData, sizeof(actual));
    context_.context()->Unmap(staging.get(), 1);

    EXPECT_NE(actual, kPoison)
        << "mip 1 still has poison after GenerateMips (actual=0x" << std::hex
        << actual << ")";
    // Accept any non-zero red-dominant pixel, or exact red with tolerance.
    const bool near_top = ColorMatchesRgba8(actual, kTopPacked, 8);
    const uint8_t r = static_cast<uint8_t>(actual & 0xff);
    const uint8_t g = static_cast<uint8_t>((actual >> 8) & 0xff);
    const uint8_t b = static_cast<uint8_t>((actual >> 16) & 0xff);
    const bool red_dominant = r > 0 && r >= g && r >= b;
    EXPECT_TRUE(near_top || red_dominant)
        << "mip 1 after GenerateMips actual=0x" << std::hex << actual
        << " expected near red or red-dominant non-poison";
  }
}

} // namespace
