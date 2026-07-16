#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct SparseTilingCase {
  D3D12_RESOURCE_DIMENSION dimension;
  UINT64 width;
  UINT height;
  UINT16 depth_or_array_size;
  UINT16 mip_levels;
  DXGI_FORMAT format;
  const char *name;
};

class SparseTilingMatrixSpec
    : public ::testing::TestWithParam<SparseTilingCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)),
              S_OK);
    if (options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1)
      GTEST_SKIP() << "Tiled resources are not supported";
  }

  D3D12TestContext context_;
};

TEST_P(SparseTilingMatrixSpec, ReportsCoherentResourceTiling) {
  const auto &test = GetParam();
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = test.dimension;
  desc.Width = test.width;
  desc.Height = test.height;
  desc.DepthOrArraySize = test.depth_or_array_size;
  desc.MipLevels = test.mip_levels;
  desc.Format = test.format;
  desc.SampleDesc.Count = 1;
  desc.Layout = test.dimension == D3D12_RESOURCE_DIMENSION_BUFFER
                    ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR
                    : D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreateReservedResource(
                &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(resource.put())),
            S_OK);
  ASSERT_TRUE(resource);

  const UINT expected_subresources =
      test.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
          ? test.mip_levels
          : test.mip_levels * test.depth_or_array_size;
  UINT total_tiles = 0;
  D3D12_PACKED_MIP_INFO packed = {};
  D3D12_TILE_SHAPE shape = {};
  UINT subresource_count = expected_subresources;
  std::vector<D3D12_SUBRESOURCE_TILING> tilings(expected_subresources);
  context_.device()->GetResourceTiling(
      resource.get(), &total_tiles, &packed, &shape, &subresource_count, 0,
      tilings.data());

  EXPECT_GT(total_tiles, 0u);
  EXPECT_EQ(subresource_count, expected_subresources);
  EXPECT_GT(shape.WidthInTexels, 0u);
  EXPECT_GT(shape.HeightInTexels, 0u);
  EXPECT_GT(shape.DepthInTexels, 0u);
  EXPECT_LE(packed.NumStandardMips, test.mip_levels);
  EXPECT_LE(packed.NumPackedMips, test.mip_levels);
  if (test.dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
    EXPECT_EQ(packed.NumStandardMips + packed.NumPackedMips, test.mip_levels);
  } else {
    EXPECT_EQ(packed.NumStandardMips, 0u);
    EXPECT_EQ(packed.NumPackedMips, 0u);
  }

  UINT previous_start = 0;
  bool saw_standard = false;
  for (UINT index = 0; index < subresource_count; ++index) {
    const auto &tiling = tilings[index];
    if (tiling.StartTileIndexInOverallResource == D3D12_PACKED_TILE)
      continue;
    EXPECT_GT(tiling.WidthInTiles, 0u) << "subresource " << index;
    EXPECT_GT(tiling.HeightInTiles, 0u) << "subresource " << index;
    EXPECT_GT(tiling.DepthInTiles, 0u) << "subresource " << index;
    if (saw_standard) {
      EXPECT_GE(tiling.StartTileIndexInOverallResource, previous_start);
    }
    previous_start = tiling.StartTileIndexInOverallResource;
    saw_standard = true;
  }
  EXPECT_TRUE(saw_standard);
}

std::string SparseTilingCaseName(
    const ::testing::TestParamInfo<SparseTilingCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    ResourceDimensions, SparseTilingMatrixSpec,
    ::testing::Values(
        SparseTilingCase{D3D12_RESOURCE_DIMENSION_BUFFER, 4ull << 16, 1, 1,
                         1, DXGI_FORMAT_UNKNOWN, "Buffer"},
        SparseTilingCase{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 512, 256, 1, 1,
                         DXGI_FORMAT_R32_UINT, "Texture2D"},
        SparseTilingCase{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 512, 512, 4, 1,
                         DXGI_FORMAT_R32_UINT, "Texture2DArray"},
        SparseTilingCase{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 512, 512, 1, 10,
                         DXGI_FORMAT_R32_UINT, "MipmappedTexture2D"},
        SparseTilingCase{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1024, 1024, 1,
                         11, DXGI_FORMAT_BC7_UNORM, "BlockCompressedTexture"},
        SparseTilingCase{D3D12_RESOURCE_DIMENSION_TEXTURE3D, 256, 128, 32, 8,
                         DXGI_FORMAT_R32_UINT, "Texture3D"}),
    SparseTilingCaseName);

class SparseMappingMatrixSpec : public ::testing::Test {
protected:
  static constexpr UINT64 kTileSize =
      D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;

  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)),
              S_OK);
    if (options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1)
      GTEST_SKIP() << "Tiled resources are not supported";
  }

  ComPtr<ID3D12Resource> CreateTexture(D3D12_RESOURCE_STATES state) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 512;
    desc.Height = 512;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_UINT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    ComPtr<ID3D12Resource> resource;
    EXPECT_EQ(context_.device()->CreateReservedResource(
                  &desc, state, nullptr, IID_PPV_ARGS(resource.put())),
              S_OK);
    return resource;
  }

  ComPtr<ID3D12Heap> CreateBacking(UINT tile_count) {
    D3D12_HEAP_DESC desc = {};
    desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.SizeInBytes = UINT64(tile_count) * kTileSize;
    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    ComPtr<ID3D12Heap> heap;
    EXPECT_EQ(context_.device()->CreateHeap(&desc, IID_PPV_ARGS(heap.put())),
              S_OK);
    return heap;
  }

  void MapRange(ID3D12Resource *resource, ID3D12Heap *heap, UINT x,
                UINT tile_count, D3D12_TILE_RANGE_FLAGS flag,
                UINT heap_offset = 0) {
    D3D12_TILED_RESOURCE_COORDINATE coordinate = {x, 0, 0, 0};
    D3D12_TILE_REGION_SIZE region = {};
    region.NumTiles = tile_count;
    region.UseBox = TRUE;
    region.Width = tile_count;
    region.Height = 1;
    region.Depth = 1;
    context_.queue()->UpdateTileMappings(
        resource, 1, &coordinate, &region, heap, 1, &flag, &heap_offset,
        &tile_count, D3D12_TILE_MAPPING_FLAG_NONE);
  }

  void WriteTile(ID3D12Resource *resource, UINT x,
                 const std::vector<std::uint8_t> &bytes) {
    ASSERT_EQ(bytes.size(), kTileSize);
    auto upload =
        context_.CreateUploadBuffer(kTileSize, bytes.data(), bytes.size());
    ASSERT_TRUE(upload);
    D3D12_TILED_RESOURCE_COORDINATE coordinate = {x, 0, 0, 0};
    D3D12_TILE_REGION_SIZE region = {};
    region.NumTiles = 1;
    context_.list()->CopyTiles(
        resource, &coordinate, &region, upload.get(), 0,
        D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }

  std::vector<std::uint8_t> ReadTile(ID3D12Resource *resource, UINT x,
                                     D3D12_RESOURCE_STATES before) {
    auto output = context_.CreateBuffer(
        kTileSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    EXPECT_TRUE(output);
    if (!output)
      return {};
    if (before != D3D12_RESOURCE_STATE_COPY_SOURCE)
      D3D12TestContext::Transition(context_.list(), resource, before,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TILED_RESOURCE_COORDINATE coordinate = {x, 0, 0, 0};
    D3D12_TILE_REGION_SIZE region = {};
    region.NumTiles = 1;
    context_.list()->CopyTiles(
        resource, &coordinate, &region, output.get(), 0,
        D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
    D3D12TestContext::Transition(context_.list(), output.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> actual;
    EXPECT_EQ(context_.ReadbackBuffer(output.get(), kTileSize, &actual), S_OK);
    return actual;
  }

  D3D12TestContext context_;
};

TEST_F(SparseMappingMatrixSpec, PaginatesSubresourceTilings) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 512;
  desc.Height = 512;
  desc.DepthOrArraySize = 4;
  desc.MipLevels = 4;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreateReservedResource(
                &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(resource.put())),
            S_OK);

  std::array<D3D12_SUBRESOURCE_TILING, 3> page = {};
  UINT count = page.size();
  context_.device()->GetResourceTiling(resource.get(), nullptr, nullptr,
                                       nullptr, &count, 2, page.data());
  EXPECT_EQ(count, page.size());

  count = 7;
  context_.device()->GetResourceTiling(resource.get(), nullptr, nullptr,
                                       nullptr, &count, 15, page.data());
  EXPECT_EQ(count, 1u);

  count = 1;
  context_.device()->GetResourceTiling(resource.get(), nullptr, nullptr,
                                       nullptr, &count, 16, page.data());
  EXPECT_EQ(count, 0u);
}

TEST_F(SparseMappingMatrixSpec, ReuseSinglePhysicalTileAliasesVirtualTiles) {
  auto texture = CreateTexture(D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap = CreateBacking(1);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  MapRange(texture.get(), heap.get(), 0, 2,
           D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE);

  std::vector<std::uint8_t> expected(kTileSize);
  for (UINT64 index = 0; index < expected.size(); ++index)
    expected[index] = static_cast<std::uint8_t>((index * 41 + 13) & 0xff);
  WriteTile(texture.get(), 0, expected);
  const auto actual =
      ReadTile(texture.get(), 1, D3D12_RESOURCE_STATE_COPY_DEST);
  EXPECT_EQ(actual, expected);
}

TEST_F(SparseMappingMatrixSpec, SkipRangePreservesExistingMapping) {
  auto texture = CreateTexture(D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap = CreateBacking(1);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  MapRange(texture.get(), heap.get(), 0, 1, D3D12_TILE_RANGE_FLAG_NONE);

  std::vector<std::uint8_t> expected(kTileSize);
  for (UINT64 index = 0; index < expected.size(); ++index)
    expected[index] = static_cast<std::uint8_t>((index * 17 + 29) & 0xff);
  WriteTile(texture.get(), 0, expected);
  MapRange(texture.get(), nullptr, 0, 1, D3D12_TILE_RANGE_FLAG_SKIP);
  const auto actual =
      ReadTile(texture.get(), 0, D3D12_RESOURCE_STATE_COPY_DEST);
  EXPECT_EQ(actual, expected);
}

TEST_F(SparseMappingMatrixSpec, RepeatedNullAndMapRangesRecover) {
  auto texture = CreateTexture(D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap = CreateBacking(1);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);

  for (UINT iteration = 0; iteration < 64; ++iteration) {
    MapRange(texture.get(), heap.get(), 0, 1, D3D12_TILE_RANGE_FLAG_NONE);
    MapRange(texture.get(), nullptr, 0, 1, D3D12_TILE_RANGE_FLAG_NULL);
  }
  MapRange(texture.get(), heap.get(), 0, 1, D3D12_TILE_RANGE_FLAG_NONE);

  std::vector<std::uint8_t> expected(kTileSize);
  for (UINT64 index = 0; index < expected.size(); ++index)
    expected[index] = static_cast<std::uint8_t>((index * 73 + 5) & 0xff);
  WriteTile(texture.get(), 0, expected);
  const auto actual =
      ReadTile(texture.get(), 0, D3D12_RESOURCE_STATE_COPY_DEST);
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
