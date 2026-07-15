#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class CopyTextureSpec : public ::testing::Test {
protected:
  struct CopyCase {
    D3D12_RESOURCE_DIMENSION dimension;
    DXGI_FORMAT format;
    UINT64 width;
    UINT height;
    UINT16 depth_or_array_size;
    UINT16 mip_levels;
    UINT subresource;
  };

  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  static D3D12_RESOURCE_DESC TextureDesc(const CopyCase &copy) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = copy.dimension;
    desc.Width = copy.width;
    desc.Height = copy.height;
    desc.DepthOrArraySize = copy.depth_or_array_size;
    desc.MipLevels = copy.mip_levels;
    desc.Format = copy.format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return desc;
  }

  ComPtr<ID3D12Resource> CreateTexture(const D3D12_RESOURCE_DESC &desc) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    EXPECT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(texture.put()))));
    return texture;
  }

  static std::vector<std::uint8_t>
  MakePayload(UINT rows, UINT depth, UINT64 row_size, std::uint8_t seed) {
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(rows) * depth *
                                      row_size);
    for (UINT z = 0; z < depth; ++z) {
      for (UINT row = 0; row < rows; ++row) {
        for (UINT64 byte = 0; byte < row_size; ++byte) {
          payload[(static_cast<std::size_t>(z) * rows + row) * row_size +
                  byte] =
              static_cast<std::uint8_t>(seed + 31 * z + 17 * row + 7 * byte);
        }
      }
    }
    return payload;
  }

  static void
  WriteFootprint(std::uint8_t *destination,
                 const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint, UINT rows,
                 UINT64 row_size, const std::vector<std::uint8_t> &payload) {
    const UINT64 slice_pitch = UINT64(footprint.Footprint.RowPitch) * rows;
    for (UINT z = 0; z < footprint.Footprint.Depth; ++z) {
      for (UINT row = 0; row < rows; ++row) {
        std::memcpy(destination + footprint.Offset + z * slice_pitch +
                        UINT64(row) * footprint.Footprint.RowPitch,
                    payload.data() +
                        (static_cast<std::size_t>(z) * rows + row) * row_size,
                    static_cast<std::size_t>(row_size));
      }
    }
  }

  static void
  ExpectFootprint(const std::uint8_t *actual,
                  const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint,
                  UINT rows, UINT64 row_size,
                  const std::vector<std::uint8_t> &expected) {
    const UINT64 slice_pitch = UINT64(footprint.Footprint.RowPitch) * rows;
    for (UINT z = 0; z < footprint.Footprint.Depth; ++z) {
      for (UINT row = 0; row < rows; ++row) {
        EXPECT_EQ(std::memcmp(actual + footprint.Offset + z * slice_pitch +
                                  UINT64(row) * footprint.Footprint.RowPitch,
                              expected.data() +
                                  (static_cast<std::size_t>(z) * rows + row) *
                                      row_size,
                              static_cast<std::size_t>(row_size)),
                  0)
            << "depth=" << z << ", row=" << row;
      }
    }
  }

  D3D12TestContext context_;
};

TEST_F(CopyTextureSpec, FullFootprintMatrix) {
  constexpr std::array cases = {
      CopyCase{D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM, 13, 7,
               1, 1, 0},
      CopyCase{D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM,
               15, 9, 2, 3, 5},
      CopyCase{D3D12_RESOURCE_DIMENSION_TEXTURE2D,
               DXGI_FORMAT_R16G16B16A16_FLOAT, 5, 3, 1, 1, 0},
      CopyCase{D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM, 7, 5,
               1, 1, 0},
      CopyCase{D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC3_UNORM, 9, 7,
               1, 1, 0},
      CopyCase{D3D12_RESOURCE_DIMENSION_TEXTURE3D, DXGI_FORMAT_R8_UINT, 7, 5, 3,
               1, 0},
  };
  constexpr std::uint8_t guard = 0xa5;

  for (std::size_t index = 0; index < cases.size(); ++index) {
    const auto &copy = cases[index];
    SCOPED_TRACE(::testing::Message()
                 << "case=" << index << ", format=" << UINT(copy.format));
    const auto desc = TextureDesc(copy);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total_size = 0;
    context_.device()->GetCopyableFootprints(
        &desc, copy.subresource, 1, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
        &footprint, &rows, &row_size, &total_size);
    ASSERT_NE(total_size, UINT64_MAX);
    EXPECT_EQ(footprint.Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, 0u);
    EXPECT_EQ(footprint.Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT,
              0u);
    const UINT64 footprint_size =
        footprint.Offset +
        UINT64(footprint.Footprint.RowPitch) * rows * footprint.Footprint.Depth;
    const UINT64 buffer_size =
        footprint_size + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    auto payload = MakePayload(rows, footprint.Footprint.Depth, row_size,
                               static_cast<std::uint8_t>(17 + index * 29));
    std::vector<std::uint8_t> upload_data(buffer_size, guard);
    WriteFootprint(upload_data.data(), footprint, rows, row_size, payload);
    auto upload = context_.CreateUploadBuffer(buffer_size, upload_data.data(),
                                              upload_data.size());
    auto readback = context_.CreateBuffer(buffer_size, D3D12_HEAP_TYPE_READBACK,
                                          D3D12_RESOURCE_FLAG_NONE,
                                          D3D12_RESOURCE_STATE_COPY_DEST);
    auto texture = CreateTexture(desc);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(readback);
    ASSERT_TRUE(texture);

    void *mapping = nullptr;
    const D3D12_RANGE no_read = {0, 0};
    ASSERT_TRUE(SUCCEEDED(readback->Map(0, &no_read, &mapping)));
    std::memset(mapping, guard, static_cast<std::size_t>(buffer_size));
    const D3D12_RANGE initialized = {0, static_cast<SIZE_T>(buffer_size)};
    readback->Unmap(0, &initialized);

    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = upload.get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION texture_location = {};
    texture_location.pResource = texture.get();
    texture_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    texture_location.SubresourceIndex = copy.subresource;
    context_.list()->CopyTextureRegion(&texture_location, 0, 0, 0, &source,
                                       nullptr);
    D3D12TestContext::Transition(context_.list(), texture.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION destination = source;
    destination.pResource = readback.get();
    context_.list()->CopyTextureRegion(&destination, 0, 0, 0, &texture_location,
                                       nullptr);
    ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

    const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(buffer_size)};
    ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
    const auto *actual = static_cast<const std::uint8_t *>(mapping);
    ExpectFootprint(actual, footprint, rows, row_size, payload);
    UINT64 corrupted_guard = UINT64_MAX;
    for (UINT64 offset = 0; offset < buffer_size; ++offset) {
      const UINT64 relative =
          offset >= footprint.Offset ? offset - footprint.Offset : UINT64_MAX;
      const UINT64 row_offset = relative == UINT64_MAX
                                    ? UINT64_MAX
                                    : relative % footprint.Footprint.RowPitch;
      const bool payload_byte =
          relative < footprint_size - footprint.Offset && row_offset < row_size;
      if (!payload_byte && actual[offset] != guard) {
        corrupted_guard = offset;
        break;
      }
    }
    const D3D12_RANGE no_write = {0, 0};
    readback->Unmap(0, &no_write);
    EXPECT_EQ(corrupted_guard, UINT64_MAX)
        << "guard offset=" << corrupted_guard;
    ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  }
}

TEST_F(CopyTextureSpec, CopiesNonzeroSourceBoxToDestinationOffset) {
  constexpr UINT width = 8;
  constexpr UINT height = 7;
  const CopyCase copy = {D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                         DXGI_FORMAT_R8G8B8A8_UNORM,
                         width,
                         height,
                         1,
                         1,
                         0};
  auto source = CreateTexture(TextureDesc(copy));
  auto destination = CreateTexture(TextureDesc(copy));
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  constexpr std::size_t pixel_count = width * height;
  std::array<std::uint32_t, pixel_count> source_data = {};
  std::array<std::uint32_t, pixel_count> expected = {};
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x)
      source_data[y * width + x] = 0xff000000u | (y << 12) | (x << 4) | 3u;
  }
  expected.fill(0x11223344u);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      source.get(), source_data.data(), width * sizeof(std::uint32_t),
      sizeof(source_data))));
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      destination.get(), expected.data(), width * sizeof(std::uint32_t),
      sizeof(expected))));

  constexpr D3D12_BOX box = {2, 1, 0, 7, 5, 1};
  constexpr UINT destination_x = 1;
  constexpr UINT destination_y = 2;
  D3D12_TEXTURE_COPY_LOCATION source_location = {};
  source_location.pResource = source.get();
  source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION destination_location = {};
  destination_location.pResource = destination.get();
  destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12TestContext::Transition(context_.list(), source.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyTextureRegion(&destination_location, destination_x,
                                     destination_y, 0, &source_location, &box);
  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  for (UINT y = box.top; y < box.bottom; ++y) {
    for (UINT x = box.left; x < box.right; ++x) {
      expected[(destination_y + y - box.top) * width + destination_x + x -
               box.left] = source_data[y * width + x];
    }
  }

  TextureReadback readback;
  ASSERT_TRUE(
      SUCCEEDED(context_.ReadbackTexture(destination.get(), &readback)));
  ASSERT_EQ(readback.width, width);
  ASSERT_EQ(readback.height, height);
  for (UINT y = 0; y < height; ++y) {
    EXPECT_EQ(std::memcmp(readback.data.data() + y * readback.row_pitch,
                          expected.data() + y * width,
                          width * sizeof(std::uint32_t)),
              0)
        << "row=" << y;
  }
}

TEST_F(CopyTextureSpec, CopiesMultipleSubresourcesThroughOneBuffer) {
  constexpr CopyCase copy = {
      D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UINT, 9, 5, 2, 3, 0};
  constexpr UINT subresource_count = copy.depth_or_array_size * copy.mip_levels;
  constexpr std::uint8_t guard = 0x6d;
  const auto desc = TextureDesc(copy);
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, subresource_count> footprints =
      {};
  std::array<UINT, subresource_count> rows = {};
  std::array<UINT64, subresource_count> row_sizes = {};
  UINT64 total_size = 0;
  context_.device()->GetCopyableFootprints(
      &desc, 0, subresource_count, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
      footprints.data(), rows.data(), row_sizes.data(), &total_size);
  ASSERT_NE(total_size, UINT64_MAX);
  const UINT64 buffer_size =
      total_size + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
  std::vector<std::uint8_t> expected(buffer_size, guard);
  for (UINT subresource = 0; subresource < subresource_count; ++subresource) {
    auto payload =
        MakePayload(rows[subresource], footprints[subresource].Footprint.Depth,
                    row_sizes[subresource],
                    static_cast<std::uint8_t>(19 + 23 * subresource));
    WriteFootprint(expected.data(), footprints[subresource], rows[subresource],
                   row_sizes[subresource], payload);
  }

  auto upload = context_.CreateUploadBuffer(buffer_size, expected.data(),
                                            expected.size());
  auto readback = context_.CreateBuffer(buffer_size, D3D12_HEAP_TYPE_READBACK,
                                        D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_COPY_DEST);
  auto texture = CreateTexture(desc);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(readback);
  ASSERT_TRUE(texture);
  void *mapping = nullptr;
  const D3D12_RANGE no_read = {0, 0};
  ASSERT_TRUE(SUCCEEDED(readback->Map(0, &no_read, &mapping)));
  std::memset(mapping, guard, static_cast<std::size_t>(buffer_size));
  const D3D12_RANGE initialized = {0, static_cast<SIZE_T>(buffer_size)};
  readback->Unmap(0, &initialized);

  for (UINT subresource = 0; subresource < subresource_count; ++subresource) {
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = upload.get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprints[subresource];
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = texture.get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = subresource;
    context_.list()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  }
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  for (UINT subresource = 0; subresource < subresource_count; ++subresource) {
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = readback.get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprints[subresource];
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = texture.get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = subresource;
    context_.list()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  }
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(buffer_size)};
  ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
  EXPECT_EQ(std::memcmp(mapping, expected.data(), expected.size()), 0);
  const D3D12_RANGE no_write = {0, 0};
  readback->Unmap(0, &no_write);
}

} // namespace
