#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// D3D11 Map / CopyResource / CopyStructureCount matrix.
// Public D3D11 / DXGI API only. Staging Map readback with exact byte checks.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

void FillBytePattern(std::vector<std::uint8_t> *data, UINT seed) {
  for (size_t i = 0; i < data->size(); ++i)
    (*data)[i] = static_cast<std::uint8_t>((i * 17u + seed) & 0xffu);
}

// ---------------------------------------------------------------------------
// 1. Dynamic buffer Map WRITE_DISCARD size matrix → staging exact bytes
// ---------------------------------------------------------------------------

class D3D11DynamicMapDiscardMatrixSpec
    : public ::testing::TestWithParam<UINT> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_P(D3D11DynamicMapDiscardMatrixSpec,
       WriteDiscardPatternCopiesToStagingExactBytes) {
  const UINT byte_width = GetParam();
  ASSERT_GT(byte_width, 0u);

  std::vector<std::uint8_t> pattern(byte_width);
  FillBytePattern(&pattern, /*seed=*/0x3bu);

  D3D11_BUFFER_DESC dynamic_desc = {};
  dynamic_desc.ByteWidth = byte_width;
  dynamic_desc.Usage = D3D11_USAGE_DYNAMIC;
  dynamic_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  dynamic_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ComPtr<ID3D11Buffer> dynamic;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&dynamic_desc, nullptr, dynamic.put())));

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      dynamic.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  ASSERT_NE(mapped.pData, nullptr);
  std::memcpy(mapped.pData, pattern.data(), pattern.size());
  context_.context()->Unmap(dynamic.get(), 0);

  D3D11_BUFFER_DESC staging_desc = dynamic_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), dynamic.get());

  mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  ASSERT_NE(mapped.pData, nullptr);
  const auto *actual = static_cast<const std::uint8_t *>(mapped.pData);
  for (UINT i = 0; i < byte_width; ++i) {
    EXPECT_EQ(actual[i], pattern[i])
        << "byte " << i << " byte_width=" << byte_width;
  }
  context_.context()->Unmap(staging.get(), 0);
}

std::string DynamicDiscardName(const ::testing::TestParamInfo<UINT> &info) {
  return "Size" + std::to_string(info.param);
}

INSTANTIATE_TEST_SUITE_P(MapWriteDiscardSizes, D3D11DynamicMapDiscardMatrixSpec,
                         ::testing::Values(64u, 256u, 1024u, 4096u),
                         DynamicDiscardName);

// ---------------------------------------------------------------------------
// 2. Dynamic Map WRITE_NO_OVERWRITE append-style multi-region (no discard)
// ---------------------------------------------------------------------------

class D3D11DynamicMapNoOverwriteSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DynamicMapNoOverwriteSpec,
       AppendsTwoRegionsWithoutDiscardBetweenAndReadbackExactBytes) {
  // Three equal regions: seed with DISCARD, then two NO_OVERWRITE appends.
  // Second and third maps do not discard; earlier regions must stay intact.
  constexpr UINT kRegionCount = 3;
  constexpr UINT kRegionBytes = 64;
  constexpr UINT kByteWidth = kRegionCount * kRegionBytes;

  std::vector<std::uint8_t> expected(kByteWidth, 0);
  std::array<std::vector<std::uint8_t>, kRegionCount> regions;
  for (UINT r = 0; r < kRegionCount; ++r) {
    regions[r].assign(kRegionBytes, 0);
    FillBytePattern(&regions[r], /*seed=*/0x40u + r * 0x11u);
  }

  D3D11_BUFFER_DESC dynamic_desc = {};
  dynamic_desc.ByteWidth = kByteWidth;
  dynamic_desc.Usage = D3D11_USAGE_DYNAMIC;
  dynamic_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  dynamic_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ComPtr<ID3D11Buffer> dynamic;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&dynamic_desc, nullptr, dynamic.put())));

  // Region 0 via WRITE_DISCARD (legal first write / full-buffer ownership).
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      dynamic.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  ASSERT_NE(mapped.pData, nullptr);
  auto *base = static_cast<std::uint8_t *>(mapped.pData);
  std::memcpy(base + 0 * kRegionBytes, regions[0].data(), kRegionBytes);
  std::memcpy(expected.data() + 0 * kRegionBytes, regions[0].data(),
              kRegionBytes);
  context_.context()->Unmap(dynamic.get(), 0);

  // Regions 1 and 2 via WRITE_NO_OVERWRITE without discard between maps.
  for (UINT r = 1; r < kRegionCount; ++r) {
    mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        dynamic.get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped)))
        << "region=" << r;
    ASSERT_NE(mapped.pData, nullptr);
    base = static_cast<std::uint8_t *>(mapped.pData);
    std::memcpy(base + r * kRegionBytes, regions[r].data(), kRegionBytes);
    std::memcpy(expected.data() + r * kRegionBytes, regions[r].data(),
                kRegionBytes);
    context_.context()->Unmap(dynamic.get(), 0);
  }

  D3D11_BUFFER_DESC staging_desc = dynamic_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), dynamic.get());

  mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  ASSERT_NE(mapped.pData, nullptr);
  const auto *actual = static_cast<const std::uint8_t *>(mapped.pData);
  for (UINT i = 0; i < kByteWidth; ++i) {
    EXPECT_EQ(actual[i], expected[i]) << "byte " << i;
  }
  context_.context()->Unmap(staging.get(), 0);
}

// ---------------------------------------------------------------------------
// 3. Staging texture Map READ after CopyResource (size matrix)
// ---------------------------------------------------------------------------

struct StagingTextureSize {
  UINT width;
  UINT height;
};

class D3D11StagingTextureMapMatrixSpec
    : public ::testing::TestWithParam<StagingTextureSize> {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_P(D3D11StagingTextureMapMatrixSpec,
       MapReadAfterCopyResourceReturnsExactTexels) {
  const auto &size = GetParam();
  ASSERT_GT(size.width, 0u);
  ASSERT_GT(size.height, 0u);

  std::vector<std::uint32_t> pixels(static_cast<size_t>(size.width) *
                                    size.height);
  for (UINT y = 0; y < size.height; ++y) {
    for (UINT x = 0; x < size.width; ++x) {
      pixels[y * size.width + x] =
          0xff000000u | (static_cast<std::uint32_t>(y) << 12) |
          (static_cast<std::uint32_t>(x) << 4) | 0x7u;
    }
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = size.width;
  desc.Height = size.height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = pixels.data();
  initial.SysMemPitch = size.width * sizeof(std::uint32_t);
  ComPtr<ID3D11Texture2D> source;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture2D(&desc, &initial, source.put())));

  D3D11_TEXTURE2D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), source.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  ASSERT_NE(mapped.pData, nullptr);
  ASSERT_GE(mapped.RowPitch, size.width * sizeof(std::uint32_t));

  for (UINT y = 0; y < size.height; ++y) {
    for (UINT x = 0; x < size.width; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  static_cast<const std::uint8_t *>(mapped.pData) +
                      y * mapped.RowPitch + x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, pixels[y * size.width + x])
          << "x=" << x << " y=" << y << " size=" << size.width << "x"
          << size.height;
    }
  }
  context_.context()->Unmap(staging.get(), 0);
}

std::string StagingTextureName(
    const ::testing::TestParamInfo<StagingTextureSize> &info) {
  return "W" + std::to_string(info.param.width) + "H" +
         std::to_string(info.param.height);
}

INSTANTIATE_TEST_SUITE_P(StagingMapReadSizes, D3D11StagingTextureMapMatrixSpec,
                         ::testing::Values(StagingTextureSize{8, 8},
                                           StagingTextureSize{32, 16}),
                         StagingTextureName);

// ---------------------------------------------------------------------------
// 4. CopyStructureCount from counter-capable structured UAV
// ---------------------------------------------------------------------------

class D3D11CopyStructureCountSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11CopyStructureCountSpec,
       CopiesInitializedCounterToDestinationExactBytes) {
  // Prefer the lightweight path: structured buffer + COUNTER UAV, set the
  // hidden counter via CSSetUnorderedAccessViews pUAVInitialCounts (public
  // D3D11 counter init), then CopyStructureCount → staging Map READ.
  // Skip only when creating the counter-capable UAV fails.
  constexpr UINT kElementCount = 16;
  constexpr UINT kStructureStride = sizeof(std::uint32_t);
  constexpr UINT kExpectedCount = 42u;
  constexpr UINT kCountOffset = 4; // non-zero dst offset (4-byte aligned)

  D3D11_BUFFER_DESC structured_desc = {};
  structured_desc.ByteWidth = kElementCount * kStructureStride;
  structured_desc.Usage = D3D11_USAGE_DEFAULT;
  structured_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  structured_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  structured_desc.StructureByteStride = kStructureStride;

  ComPtr<ID3D11Buffer> structured;
  const HRESULT create_buffer_hr =
      context_.device()->CreateBuffer(&structured_desc, nullptr, structured.put());
  if (FAILED(create_buffer_hr) || !structured) {
    GTEST_SKIP() << "CreateBuffer for counter-capable structured UAV failed: 0x"
                 << std::hex << static_cast<unsigned long>(create_buffer_hr);
  }

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.NumElements = kElementCount;
  uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;

  ComPtr<ID3D11UnorderedAccessView> uav;
  const HRESULT create_uav_hr = context_.device()->CreateUnorderedAccessView(
      structured.get(), &uav_desc, uav.put());
  if (FAILED(create_uav_hr) || !uav) {
    GTEST_SKIP() << "CreateUnorderedAccessView with D3D11_BUFFER_UAV_FLAG_COUNTER "
                    "failed: 0x"
                 << std::hex << static_cast<unsigned long>(create_uav_hr);
  }

  // Initialize hidden counter through the public UAV bind path.
  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  const UINT initial_counts[] = {kExpectedCount};
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, initial_counts);

  // Destination holds poison + room for the copied UINT32 count.
  constexpr UINT kDestBytes = 16;
  std::array<std::uint8_t, kDestBytes> dest_poison = {};
  dest_poison.fill(0xcd);
  D3D11_BUFFER_DESC dest_desc = {};
  dest_desc.ByteWidth = kDestBytes;
  dest_desc.Usage = D3D11_USAGE_DEFAULT;
  dest_desc.BindFlags = 0;
  D3D11_SUBRESOURCE_DATA dest_initial = {};
  dest_initial.pSysMem = dest_poison.data();
  ComPtr<ID3D11Buffer> destination;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &dest_desc, &dest_initial, destination.put())));

  context_.context()->CopyStructureCount(destination.get(), kCountOffset,
                                         uav.get());

  // Unbind UAV after the copy (counter value is already captured).
  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

  D3D11_BUFFER_DESC staging_desc = dest_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), destination.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  ASSERT_NE(mapped.pData, nullptr);
  const auto *actual = static_cast<const std::uint8_t *>(mapped.pData);

  std::array<std::uint8_t, kDestBytes> expected = dest_poison;
  std::uint32_t count_le = kExpectedCount;
  std::memcpy(expected.data() + kCountOffset, &count_le, sizeof(count_le));

  for (UINT i = 0; i < kDestBytes; ++i) {
    EXPECT_EQ(actual[i], expected[i])
        << "byte " << i << " expected_count=" << kExpectedCount
        << " count_offset=" << kCountOffset;
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11CopyStructureCountSpec,
       CopiesZeroCounterAfterRebindWithInitialCountZero) {
  constexpr UINT kElementCount = 8;
  constexpr UINT kStructureStride = sizeof(std::uint32_t);

  D3D11_BUFFER_DESC structured_desc = {};
  structured_desc.ByteWidth = kElementCount * kStructureStride;
  structured_desc.Usage = D3D11_USAGE_DEFAULT;
  structured_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  structured_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  structured_desc.StructureByteStride = kStructureStride;

  ComPtr<ID3D11Buffer> structured;
  const HRESULT create_buffer_hr =
      context_.device()->CreateBuffer(&structured_desc, nullptr, structured.put());
  if (FAILED(create_buffer_hr) || !structured) {
    GTEST_SKIP() << "CreateBuffer for counter-capable structured UAV failed: 0x"
                 << std::hex << static_cast<unsigned long>(create_buffer_hr);
  }

  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.FirstElement = 0;
  uav_desc.Buffer.NumElements = kElementCount;
  uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;

  ComPtr<ID3D11UnorderedAccessView> uav;
  const HRESULT create_uav_hr = context_.device()->CreateUnorderedAccessView(
      structured.get(), &uav_desc, uav.put());
  if (FAILED(create_uav_hr) || !uav) {
    GTEST_SKIP() << "CreateUnorderedAccessView with D3D11_BUFFER_UAV_FLAG_COUNTER "
                    "failed: 0x"
                 << std::hex << static_cast<unsigned long>(create_uav_hr);
  }

  // Seed a non-zero counter, then rebind with initial count 0 and copy again.
  ID3D11UnorderedAccessView *uavs[] = {uav.get()};
  const UINT seed_count[] = {7u};
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, seed_count);
  const UINT zero_count[] = {0u};
  context_.context()->CSSetUnorderedAccessViews(0, 1, uavs, zero_count);

  constexpr UINT kDestBytes = 4;
  std::array<std::uint8_t, kDestBytes> dest_poison = {0xab, 0xcd, 0xef, 0x99};
  D3D11_BUFFER_DESC dest_desc = {};
  dest_desc.ByteWidth = kDestBytes;
  dest_desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA dest_initial = {};
  dest_initial.pSysMem = dest_poison.data();
  ComPtr<ID3D11Buffer> destination;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &dest_desc, &dest_initial, destination.put())));

  context_.context()->CopyStructureCount(destination.get(), 0, uav.get());

  ID3D11UnorderedAccessView *null_uav[] = {nullptr};
  context_.context()->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);

  D3D11_BUFFER_DESC staging_desc = dest_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), destination.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  ASSERT_NE(mapped.pData, nullptr);

  std::uint32_t actual_count = 0xffffffffu;
  std::memcpy(&actual_count, mapped.pData, sizeof(actual_count));
  EXPECT_EQ(actual_count, 0u);
  context_.context()->Unmap(staging.get(), 0);
}

} // namespace
