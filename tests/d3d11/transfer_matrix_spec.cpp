#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// D3D11 CopySubresourceRegion / UpdateSubresource transfer matrices.
// Public D3D11 / DXGI API only. Staging Map readback with exact byte/pixel checks.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

// ---------------------------------------------------------------------------
// Buffer CopySubresourceRegion: src_offset × dst_offset × byte_count
// ---------------------------------------------------------------------------

struct BufferCopyCase {
  UINT buffer_size;
  UINT src_offset;
  UINT dst_offset;
  UINT byte_count;
};

std::vector<BufferCopyCase> BuildBufferCopyCases() {
  // Fixed 64-byte buffers; every case must stay inside both resources.
  // Covers origin/end alignment, mid offsets, single-byte, and full-buffer copies.
  return {
      {64, 0, 0, 64},  // full buffer
      {64, 0, 0, 1},   // single byte origin → origin
      {64, 63, 0, 1},  // single byte end → origin
      {64, 0, 63, 1},  // single byte origin → end
      {64, 7, 31, 19}, // resource_spec-style mid range
      {64, 0, 32, 32}, // first half → second half
      {64, 32, 0, 32}, // second half → first half
      {64, 1, 1, 62},  // interior almost-full
      {64, 8, 0, 16},  // aligned block → origin
      {64, 0, 8, 16},  // origin block → aligned offset
      {64, 5, 17, 3},  // small odd mid range
      {64, 48, 40, 16}, // near-end source → earlier dest
      {64, 16, 48, 16}, // mid source → near-end dest
      {64, 20, 20, 24}, // overlapping offset identity span
  };
}

class D3D11BufferCopyMatrixSpec
    : public ::testing::TestWithParam<BufferCopyCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_P(D3D11BufferCopyMatrixSpec,
       CopiesRangeAndPreservesPoisonOutsideDestination) {
  const auto &test = GetParam();
  ASSERT_GT(test.byte_count, 0u);
  ASSERT_LE(test.src_offset + test.byte_count, test.buffer_size);
  ASSERT_LE(test.dst_offset + test.byte_count, test.buffer_size);

  std::vector<std::uint8_t> source_data(test.buffer_size);
  std::vector<std::uint8_t> expected(test.buffer_size, 0xcd);
  for (UINT i = 0; i < test.buffer_size; ++i)
    source_data[i] = static_cast<std::uint8_t>((i * 17u + 31u) & 0xff);

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = test.buffer_size;
  desc.Usage = D3D11_USAGE_DEFAULT;

  D3D11_SUBRESOURCE_DATA source_initial = {};
  source_initial.pSysMem = source_data.data();
  D3D11_SUBRESOURCE_DATA destination_initial = {};
  destination_initial.pSysMem = expected.data();

  ComPtr<ID3D11Buffer> source;
  ComPtr<ID3D11Buffer> destination;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&desc, &source_initial, source.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &desc, &destination_initial, destination.put())));

  // Buffer boxes use left/right as byte offsets (top/bottom/front/back fixed).
  D3D11_BOX source_box = {test.src_offset, 0, 0,
                          test.src_offset + test.byte_count, 1, 1};
  context_.context()->CopySubresourceRegion(
      destination.get(), 0, test.dst_offset, 0, 0, source.get(), 0, &source_box);

  std::copy(source_data.begin() + test.src_offset,
            source_data.begin() + test.src_offset + test.byte_count,
            expected.begin() + test.dst_offset);

  D3D11_BUFFER_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), destination.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  const auto *actual = static_cast<const std::uint8_t *>(mapped.pData);
  for (UINT i = 0; i < test.buffer_size; ++i) {
    EXPECT_EQ(actual[i], expected[i])
        << "byte " << i << " src_offset=" << test.src_offset
        << " dst_offset=" << test.dst_offset
        << " byte_count=" << test.byte_count;
  }
  context_.context()->Unmap(staging.get(), 0);
}

std::string BufferCopyName(
    const ::testing::TestParamInfo<BufferCopyCase> &info) {
  return "B" + std::to_string(info.param.buffer_size) + "S" +
         std::to_string(info.param.src_offset) + "D" +
         std::to_string(info.param.dst_offset) + "N" +
         std::to_string(info.param.byte_count);
}

INSTANTIATE_TEST_SUITE_P(CopySubresourceRegionBuffer,
                         D3D11BufferCopyMatrixSpec,
                         ::testing::ValuesIn(BuildBufferCopyCases()),
                         BufferCopyName);

// ---------------------------------------------------------------------------
// Buffer UpdateSubresource: non-zero DstBox (left/right as bytes)
// ---------------------------------------------------------------------------

struct BufferUpdateCase {
  UINT buffer_size;
  UINT dst_left;
  UINT dst_right; // exclusive byte end
};

std::vector<BufferUpdateCase> BuildBufferUpdateCases() {
  return {
      {48, 0, 48},  // full buffer
      {48, 0, 1},   // single byte origin
      {48, 47, 48}, // single byte end
      {48, 11, 24}, // resource_spec-style mid range (13 bytes)
      {48, 0, 16},  // prefix
      {48, 32, 48}, // suffix
      {48, 8, 12},  // small aligned mid
      {48, 5, 8},   // small odd mid
      {48, 1, 47},  // almost-full interior
      {48, 20, 28}, // centered block
      {48, 40, 45}, // near-end partial
      {48, 3, 35},  // large interior
  };
}

class D3D11BufferUpdateMatrixSpec
    : public ::testing::TestWithParam<BufferUpdateCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_P(D3D11BufferUpdateMatrixSpec,
       WritesDstBoxBytesAndPreservesPoisonOutside) {
  const auto &test = GetParam();
  ASSERT_LT(test.dst_left, test.dst_right);
  ASSERT_LE(test.dst_right, test.buffer_size);
  const UINT byte_count = test.dst_right - test.dst_left;

  std::vector<std::uint8_t> expected(test.buffer_size, 0x5a);
  std::vector<std::uint8_t> update(byte_count);
  for (UINT i = 0; i < byte_count; ++i)
    update[i] = static_cast<std::uint8_t>((i * 13u + 7u) & 0xff);

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = test.buffer_size;
  desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = expected.data();
  ComPtr<ID3D11Buffer> destination;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&desc, &initial, destination.put())));

  D3D11_BOX destination_box = {test.dst_left, 0, 0, test.dst_right, 1, 1};
  // For buffers, SrcRowPitch and SrcDepthPitch are ignored.
  context_.context()->UpdateSubresource(destination.get(), 0, &destination_box,
                                        update.data(), 0, 0);

  std::copy(update.begin(), update.end(), expected.begin() + test.dst_left);

  D3D11_BUFFER_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), destination.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  const auto *actual = static_cast<const std::uint8_t *>(mapped.pData);
  for (UINT i = 0; i < test.buffer_size; ++i) {
    EXPECT_EQ(actual[i], expected[i])
        << "byte " << i << " dst_left=" << test.dst_left
        << " dst_right=" << test.dst_right;
  }
  context_.context()->Unmap(staging.get(), 0);
}

std::string BufferUpdateName(
    const ::testing::TestParamInfo<BufferUpdateCase> &info) {
  return "B" + std::to_string(info.param.buffer_size) + "L" +
         std::to_string(info.param.dst_left) + "R" +
         std::to_string(info.param.dst_right);
}

INSTANTIATE_TEST_SUITE_P(UpdateSubresourceBuffer, D3D11BufferUpdateMatrixSpec,
                         ::testing::ValuesIn(BuildBufferUpdateCases()),
                         BufferUpdateName);

// ---------------------------------------------------------------------------
// Texture2D CopySubresourceRegion: source box × dest (x,y)
// ---------------------------------------------------------------------------

struct Texture2DCopyCase {
  UINT left;
  UINT top;
  UINT right;
  UINT bottom;
  UINT dst_x;
  UINT dst_y;
};

std::vector<Texture2DCopyCase> BuildTexture2DCopyCases() {
  // Source and destination share a fixed 16×12 R8G8B8A8 surface.
  // Every case keeps the copied rectangle inside the destination.
  return {
      {0, 0, 16, 12, 0, 0},   // full subresource
      {0, 0, 1, 1, 0, 0},     // single texel origin
      {15, 11, 16, 12, 0, 0}, // single texel last → origin
      {3, 4, 4, 5, 7, 6},     // single texel mid → offset
      {0, 0, 4, 3, 0, 0},     // top-left block
      {12, 8, 16, 12, 0, 0},  // bottom-right block → origin
      {2, 2, 10, 9, 1, 1},    // interior box → offset
      {1, 1, 5, 5, 4, 3},     // square box → non-zero dst
      {5, 0, 6, 12, 2, 0},    // tall 1-wide strip
      {0, 5, 16, 6, 0, 3},    // wide 1-high strip
      {0, 0, 8, 6, 8, 6},     // half → opposite quadrant
      {4, 3, 12, 9, 0, 0},    // mid block → origin
      {1, 1, 4, 3, 3, 2},     // resource_spec-style 3×2 → (3,2)
  };
}

class D3D11Texture2DCopyMatrixSpec
    : public ::testing::TestWithParam<Texture2DCopyCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
  static constexpr UINT kWidth = 16;
  static constexpr UINT kHeight = 12;
};

TEST_P(D3D11Texture2DCopyMatrixSpec,
       CopiesSourceBoxToDestinationOffsetWithoutOutsideWrites) {
  const auto &test = GetParam();
  ASSERT_LT(test.left, test.right);
  ASSERT_LT(test.top, test.bottom);
  ASSERT_LE(test.right, kWidth);
  ASSERT_LE(test.bottom, kHeight);
  ASSERT_LE(test.dst_x + (test.right - test.left), kWidth);
  ASSERT_LE(test.dst_y + (test.bottom - test.top), kHeight);

  std::vector<std::uint32_t> source_pixels(kWidth * kHeight);
  std::vector<std::uint32_t> expected(kWidth * kHeight, 0x7f334455u);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x)
      source_pixels[y * kWidth + x] =
          0xff000000u | (y << 12) | (x << 4) | 0x3u;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = kWidth;
  desc.Height = kHeight;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;

  D3D11_SUBRESOURCE_DATA source_initial = {};
  source_initial.pSysMem = source_pixels.data();
  source_initial.SysMemPitch = kWidth * sizeof(std::uint32_t);
  D3D11_SUBRESOURCE_DATA destination_initial = {};
  destination_initial.pSysMem = expected.data();
  destination_initial.SysMemPitch = kWidth * sizeof(std::uint32_t);

  ComPtr<ID3D11Texture2D> source;
  ComPtr<ID3D11Texture2D> destination;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &desc, &source_initial, source.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &desc, &destination_initial, destination.put())));

  D3D11_BOX source_box = {test.left, test.top, 0, test.right, test.bottom, 1};
  context_.context()->CopySubresourceRegion(destination.get(), 0, test.dst_x,
                                            test.dst_y, 0, source.get(), 0,
                                            &source_box);

  for (UINT y = test.top; y < test.bottom; ++y) {
    for (UINT x = test.left; x < test.right; ++x) {
      expected[(test.dst_y + y - test.top) * kWidth + test.dst_x + x -
               test.left] = source_pixels[y * kWidth + x];
    }
  }

  D3D11_TEXTURE2D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), destination.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  static_cast<const std::uint8_t *>(mapped.pData) +
                      y * mapped.RowPitch + x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected[y * kWidth + x])
          << "x=" << x << " y=" << y << " box=[" << test.left << ","
          << test.top << ")-[" << test.right << "," << test.bottom
          << ") dst=(" << test.dst_x << "," << test.dst_y << ")";
    }
  }
  context_.context()->Unmap(staging.get(), 0);
}

std::string Texture2DCopyName(
    const ::testing::TestParamInfo<Texture2DCopyCase> &info) {
  return "L" + std::to_string(info.param.left) + "T" +
         std::to_string(info.param.top) + "R" +
         std::to_string(info.param.right) + "B" +
         std::to_string(info.param.bottom) + "DX" +
         std::to_string(info.param.dst_x) + "DY" +
         std::to_string(info.param.dst_y);
}

INSTANTIATE_TEST_SUITE_P(CopySubresourceRegionTexture2D,
                         D3D11Texture2DCopyMatrixSpec,
                         ::testing::ValuesIn(BuildTexture2DCopyCases()),
                         Texture2DCopyName);

// ---------------------------------------------------------------------------
// Texture2D UpdateSubresource: non-zero dest box matrix
// ---------------------------------------------------------------------------

struct Texture2DUpdateCase {
  UINT left;
  UINT top;
  UINT right;
  UINT bottom;
};

std::vector<Texture2DUpdateCase> BuildTexture2DUpdateCases() {
  // Fixed 16×12 destination; box is the exclusive DstBox written by Update.
  return {
      {0, 0, 16, 12}, // full surface
      {0, 0, 1, 1},   // single texel origin
      {15, 11, 16, 12}, // single texel last
      {3, 4, 4, 5},   // single texel mid
      {0, 0, 4, 3},   // top-left block
      {12, 8, 16, 12}, // bottom-right block
      {2, 2, 10, 9},  // interior
      {1, 1, 5, 5},   // square
      {5, 0, 6, 12},  // tall 1-wide strip
      {0, 5, 16, 6},  // wide 1-high strip
      {4, 3, 12, 9},  // mid block
      {8, 6, 16, 12}, // bottom-right quadrant
      {0, 0, 8, 6},   // top-left quadrant
  };
}

class D3D11Texture2DUpdateMatrixSpec
    : public ::testing::TestWithParam<Texture2DUpdateCase> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
  static constexpr UINT kWidth = 16;
  static constexpr UINT kHeight = 12;
};

TEST_P(D3D11Texture2DUpdateMatrixSpec,
       WritesDestBoxPixelsAndPreservesPoisonOutside) {
  const auto &test = GetParam();
  ASSERT_LT(test.left, test.right);
  ASSERT_LT(test.top, test.bottom);
  ASSERT_LE(test.right, kWidth);
  ASSERT_LE(test.bottom, kHeight);

  const UINT box_width = test.right - test.left;
  const UINT box_height = test.bottom - test.top;

  std::vector<std::uint32_t> expected(kWidth * kHeight, 0xaabbccddu);
  std::vector<std::uint32_t> update(box_width * box_height);
  for (UINT y = 0; y < box_height; ++y) {
    for (UINT x = 0; x < box_width; ++x)
      update[y * box_width + x] =
          0xff000000u | ((test.top + y) << 12) | ((test.left + x) << 4) | 0x9u;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = kWidth;
  desc.Height = kHeight;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;

  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = expected.data();
  initial.SysMemPitch = kWidth * sizeof(std::uint32_t);
  ComPtr<ID3D11Texture2D> destination;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &desc, &initial, destination.put())));

  D3D11_BOX destination_box = {test.left, test.top, 0, test.right, test.bottom,
                               1};
  context_.context()->UpdateSubresource(
      destination.get(), 0, &destination_box, update.data(),
      box_width * sizeof(std::uint32_t), 0);

  for (UINT y = 0; y < box_height; ++y) {
    for (UINT x = 0; x < box_width; ++x)
      expected[(test.top + y) * kWidth + test.left + x] =
          update[y * box_width + x];
  }

  D3D11_TEXTURE2D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), destination.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  static_cast<const std::uint8_t *>(mapped.pData) +
                      y * mapped.RowPitch + x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected[y * kWidth + x])
          << "x=" << x << " y=" << y << " box=[" << test.left << ","
          << test.top << ")-[" << test.right << "," << test.bottom << ")";
    }
  }
  context_.context()->Unmap(staging.get(), 0);
}

std::string Texture2DUpdateName(
    const ::testing::TestParamInfo<Texture2DUpdateCase> &info) {
  return "L" + std::to_string(info.param.left) + "T" +
         std::to_string(info.param.top) + "R" +
         std::to_string(info.param.right) + "B" +
         std::to_string(info.param.bottom);
}

INSTANTIATE_TEST_SUITE_P(UpdateSubresourceTexture2D,
                         D3D11Texture2DUpdateMatrixSpec,
                         ::testing::ValuesIn(BuildTexture2DUpdateCases()),
                         Texture2DUpdateName);

} // namespace
