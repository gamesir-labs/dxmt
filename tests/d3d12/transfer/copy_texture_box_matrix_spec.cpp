#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §15.2: CopyTextureRegion source-box and destination-offset matrix.
// Public D3D12 API only (texture ↔ texture subresource path).

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct BoxCopyCase {
  UINT left;
  UINT top;
  UINT right;
  UINT bottom;
  UINT dst_x;
  UINT dst_y;
};

std::vector<BoxCopyCase> BuildBoxCopyCases() {
  // Source and destination share a fixed 16×12 R8G8B8A8 surface.
  // Every case must keep the copied rectangle inside the destination.
  return {
      {0, 0, 16, 12, 0, 0}, // full subresource
      {0, 0, 1, 1, 0, 0},   // single texel origin
      {15, 11, 16, 12, 0, 0}, // single texel last
      {3, 4, 4, 5, 7, 6},   // single texel mid → offset
      {0, 0, 4, 3, 0, 0},   // top-left block
      {12, 8, 16, 12, 0, 0}, // bottom-right block → origin
      {2, 2, 10, 9, 1, 1},  // interior box → offset
      {1, 1, 5, 5, 4, 3},   // square box → non-zero dst
      {5, 0, 6, 12, 2, 0},  // tall 1-wide strip
      {0, 5, 16, 6, 0, 3},  // wide 1-high strip
      {0, 0, 8, 6, 8, 6},   // half → opposite quadrant
      {4, 3, 12, 9, 0, 0},  // mid block → origin
  };
}

class CopyTextureBoxMatrixSpec : public ::testing::TestWithParam<BoxCopyCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
  static constexpr UINT kWidth = 16;
  static constexpr UINT kHeight = 12;
};

TEST_P(CopyTextureBoxMatrixSpec,
     CopiesSourceBoxToDestinationOffsetWithoutOutsideWrites) {
  const auto &test = GetParam();
  ASSERT_LT(test.left, test.right);
  ASSERT_LT(test.top, test.bottom);
  ASSERT_LE(test.right, kWidth);
  ASSERT_LE(test.bottom, kHeight);
  ASSERT_LE(test.dst_x + (test.right - test.left), kWidth);
  ASSERT_LE(test.dst_y + (test.bottom - test.top), kHeight);

  auto source = context_.CreateTexture2D(
      kWidth, kHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto destination = context_.CreateTexture2D(
      kWidth, kHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);

  std::vector<std::uint32_t> source_data(kWidth * kHeight);
  std::vector<std::uint32_t> expected(kWidth * kHeight, 0x11223344u);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x)
      source_data[y * kWidth + x] =
          0xff000000u | (y << 12) | (x << 4) | 0x3u;
  }
  ASSERT_EQ(context_.UploadTextureAndReset(
                source.get(), source_data.data(),
                kWidth * sizeof(std::uint32_t),
                source_data.size() * sizeof(std::uint32_t)),
            S_OK);
  ASSERT_EQ(context_.UploadTextureAndReset(
                destination.get(), expected.data(),
                kWidth * sizeof(std::uint32_t),
                expected.size() * sizeof(std::uint32_t)),
            S_OK);

  const D3D12_BOX box = {test.left, test.top, 0, test.right, test.bottom, 1};
  D3D12_TEXTURE_COPY_LOCATION source_location = {};
  source_location.pResource = source.get();
  source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION destination_location = {};
  destination_location.pResource = destination.get();
  destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

  D3D12TestContext::Transition(context_.list(), source.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyTextureRegion(&destination_location, test.dst_x,
                                     test.dst_y, 0, &source_location, &box);
  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  for (UINT y = test.top; y < test.bottom; ++y) {
    for (UINT x = test.left; x < test.right; ++x) {
      expected[(test.dst_y + y - test.top) * kWidth + test.dst_x + x -
               test.left] = source_data[y * kWidth + x];
    }
  }

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(destination.get(), &readback), S_OK);
  ASSERT_EQ(readback.width, kWidth);
  ASSERT_EQ(readback.height, kHeight);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected[y * kWidth + x])
          << "x=" << x << " y=" << y << " box=[" << test.left << ","
          << test.top << ")-[" << test.right << "," << test.bottom
          << ") dst=(" << test.dst_x << "," << test.dst_y << ")";
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string BoxCopyName(const ::testing::TestParamInfo<BoxCopyCase> &info) {
  return "L" + std::to_string(info.param.left) + "T" +
         std::to_string(info.param.top) + "R" +
         std::to_string(info.param.right) + "B" +
         std::to_string(info.param.bottom) + "DX" +
         std::to_string(info.param.dst_x) + "DY" +
         std::to_string(info.param.dst_y);
}

INSTANTIATE_TEST_SUITE_P(BoxOffsetMatrix, CopyTextureBoxMatrixSpec,
                         ::testing::ValuesIn(BuildBoxCopyCases()), BoxCopyName);

} // namespace
