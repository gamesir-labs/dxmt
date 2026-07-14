#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

class D3D11ResourceSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ResourceSpec, ReadsInitializedBufferThroughStagingCopy) {
  constexpr std::array<uint32_t, 16> input = {
      0x01020304, 0x11121314, 0x21222324, 0x31323334, 0x41424344, 0x51525354,
      0x61626364, 0x71727374, 0x81828384, 0x91929394, 0xa1a2a3a4, 0xb1b2b3b4,
      0xc1c2c3c4, 0xd1d2d3d4, 0xe1e2e3e4, 0xf1f2f3f4,
  };
  D3D11_BUFFER_DESC source_desc = {};
  source_desc.ByteWidth = sizeof(input);
  source_desc.Usage = D3D11_USAGE_DEFAULT;
  source_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = input.data();

  ComPtr<ID3D11Buffer> source;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&source_desc, &initial, source.put())));

  D3D11_BUFFER_DESC staging_desc = source_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&staging_desc, nullptr, staging.put())));

  context_.context()->CopyResource(staging.get(), source.get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  EXPECT_EQ(std::memcmp(mapped.pData, input.data(), sizeof(input)), 0);
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11ResourceSpec, WritesDynamicBufferAndCopiesLatestContents) {
  constexpr std::array<uint32_t, 8> input = {
      2, 3, 5, 7, 11, 13, 17, 19,
  };
  D3D11_BUFFER_DESC dynamic_desc = {};
  dynamic_desc.ByteWidth = sizeof(input);
  dynamic_desc.Usage = D3D11_USAGE_DYNAMIC;
  dynamic_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  dynamic_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ComPtr<ID3D11Buffer> dynamic;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&dynamic_desc, nullptr, dynamic.put())));

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      dynamic.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  std::memcpy(mapped.pData, input.data(), sizeof(input));
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
  EXPECT_EQ(std::memcmp(mapped.pData, input.data(), sizeof(input)), 0);
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11ResourceSpec, AppendsDynamicBufferDataWithNoOverwrite) {
  std::array<uint32_t, 16> expected = {};
  D3D11_BUFFER_DESC dynamic_desc = {};
  dynamic_desc.ByteWidth = sizeof(expected);
  dynamic_desc.Usage = D3D11_USAGE_DYNAMIC;
  dynamic_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  dynamic_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ComPtr<ID3D11Buffer> dynamic;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&dynamic_desc, nullptr, dynamic.put())));

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      dynamic.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  auto *first = static_cast<uint32_t *>(mapped.pData);
  for (UINT i = 0; i < 8; ++i)
    first[i] = expected[i] = 0x1000u + i;
  context_.context()->Unmap(dynamic.get(), 0);

  mapped = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      dynamic.get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped)));
  auto *second = static_cast<uint32_t *>(mapped.pData);
  for (UINT i = 8; i < expected.size(); ++i)
    second[i] = expected[i] = 0x2000u + i;
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
  EXPECT_EQ(std::memcmp(mapped.pData, expected.data(), sizeof(expected)), 0);
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11ResourceSpec, CopiesBufferRangeAtNonZeroOffsets) {
  std::array<uint8_t, 64> source_data = {};
  std::array<uint8_t, 64> destination_data = {};
  for (size_t i = 0; i < source_data.size(); ++i)
    source_data[i] = static_cast<uint8_t>(i + 1);
  destination_data.fill(0xcd);

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = source_data.size();
  desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA source_initial = {};
  source_initial.pSysMem = source_data.data();
  D3D11_SUBRESOURCE_DATA destination_initial = {};
  destination_initial.pSysMem = destination_data.data();
  ComPtr<ID3D11Buffer> source;
  ComPtr<ID3D11Buffer> destination;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&desc, &source_initial, source.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateBuffer(
      &desc, &destination_initial, destination.put())));

  D3D11_BOX source_box = {7, 0, 0, 26, 1, 1};
  context_.context()->CopySubresourceRegion(destination.get(), 0, 31, 0, 0,
                                            source.get(), 0, &source_box);

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
  std::copy(source_data.begin() + 7, source_data.begin() + 26,
            destination_data.begin() + 31);
  EXPECT_EQ(std::memcmp(mapped.pData, destination_data.data(),
                        destination_data.size()),
            0);
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11ResourceSpec, UpdatesOnlySelectedBufferRange) {
  std::array<uint8_t, 48> expected = {};
  expected.fill(0x5a);
  const std::array<uint8_t, 13> update = {
      0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233,
  };

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = expected.size();
  desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = expected.data();
  ComPtr<ID3D11Buffer> destination;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateBuffer(&desc, &initial, destination.put())));

  D3D11_BOX destination_box = {11, 0, 0, 24, 1, 1};
  context_.context()->UpdateSubresource(destination.get(), 0, &destination_box,
                                        update.data(), 0, 0);

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
  std::copy(update.begin(), update.end(), expected.begin() + 11);
  EXPECT_EQ(std::memcmp(mapped.pData, expected.data(), expected.size()), 0);
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11ResourceSpec, CreatesTextureViewAndPreservesRowsOnReadback) {
  constexpr UINT width = 4;
  constexpr UINT height = 3;
  constexpr std::array<uint32_t, width *height> pixels = {
      0xff000001, 0xff000002, 0xff000003, 0xff000004, 0xff000005, 0xff000006,
      0xff000007, 0xff000008, 0xff000009, 0xff00000a, 0xff00000b, 0xff00000c,
  };
  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = width;
  texture_desc.Height = height;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = pixels.data();
  initial.SysMemPitch = width * sizeof(uint32_t);

  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &texture_desc, &initial, texture.put())));
  ComPtr<ID3D11ShaderResourceView> view;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateShaderResourceView(
      texture.get(), nullptr, view.put())));
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
  view->GetDesc(&view_desc);
  EXPECT_EQ(view_desc.Format, texture_desc.Format);
  EXPECT_EQ(view_desc.ViewDimension, D3D11_SRV_DIMENSION_TEXTURE2D);

  D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  for (UINT row = 0; row < height; ++row) {
    const auto *actual =
        static_cast<const uint8_t *>(mapped.pData) + row * mapped.RowPitch;
    const auto *expected = pixels.data() + row * width;
    EXPECT_EQ(std::memcmp(actual, expected, width * sizeof(uint32_t)), 0);
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11ResourceSpec, CopiesTextureRegionAtNonZeroOffsets) {
  constexpr UINT source_width = 5;
  constexpr UINT source_height = 4;
  constexpr UINT destination_width = 8;
  constexpr UINT destination_height = 6;
  std::array<uint32_t, source_width *source_height> source_pixels = {};
  std::array<uint32_t, destination_width *destination_height> expected = {};
  for (size_t i = 0; i < source_pixels.size(); ++i)
    source_pixels[i] = 0xff000000u | static_cast<uint32_t>(i + 1);
  expected.fill(0x7f334455);

  D3D11_TEXTURE2D_DESC source_desc = {};
  source_desc.Width = source_width;
  source_desc.Height = source_height;
  source_desc.MipLevels = 1;
  source_desc.ArraySize = 1;
  source_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  source_desc.SampleDesc.Count = 1;
  source_desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA source_initial = {};
  source_initial.pSysMem = source_pixels.data();
  source_initial.SysMemPitch = source_width * sizeof(uint32_t);
  ComPtr<ID3D11Texture2D> source;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &source_desc, &source_initial, source.put())));

  D3D11_TEXTURE2D_DESC destination_desc = source_desc;
  destination_desc.Width = destination_width;
  destination_desc.Height = destination_height;
  D3D11_SUBRESOURCE_DATA destination_initial = {};
  destination_initial.pSysMem = expected.data();
  destination_initial.SysMemPitch = destination_width * sizeof(uint32_t);
  ComPtr<ID3D11Texture2D> destination;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &destination_desc, &destination_initial, destination.put())));

  D3D11_BOX source_box = {1, 1, 0, 4, 3, 1};
  context_.context()->CopySubresourceRegion(destination.get(), 0, 3, 2, 0,
                                            source.get(), 0, &source_box);
  for (UINT y = 0; y < 2; ++y) {
    for (UINT x = 0; x < 3; ++x) {
      expected[(y + 2) * destination_width + x + 3] =
          source_pixels[(y + 1) * source_width + x + 1];
    }
  }

  D3D11_TEXTURE2D_DESC staging_desc = destination_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), destination.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  for (UINT y = 0; y < destination_height; ++y) {
    EXPECT_EQ(std::memcmp(static_cast<const uint8_t *>(mapped.pData) +
                              y * mapped.RowPitch,
                          expected.data() + y * destination_width,
                          destination_width * sizeof(uint32_t)),
              0)
        << "row " << y;
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11ResourceSpec, CopiesOneMipWithoutChangingOtherSubresources) {
  constexpr UINT mip_levels = 3;
  constexpr std::array<UINT, mip_levels> widths = {8, 4, 2};
  constexpr std::array<UINT, mip_levels> heights = {4, 2, 1};
  std::array<std::array<uint32_t, 32>, mip_levels> source = {};
  std::array<std::array<uint32_t, 32>, mip_levels> destination = {};
  std::array<D3D11_SUBRESOURCE_DATA, mip_levels> source_initial = {};
  std::array<D3D11_SUBRESOURCE_DATA, mip_levels> destination_initial = {};
  for (UINT mip = 0; mip < mip_levels; ++mip) {
    const UINT pixel_count = widths[mip] * heights[mip];
    std::fill_n(source[mip].begin(), pixel_count, 0xff000010u + mip);
    std::fill_n(destination[mip].begin(), pixel_count, 0x22334450u + mip);
    source_initial[mip].pSysMem = source[mip].data();
    source_initial[mip].SysMemPitch = widths[mip] * sizeof(uint32_t);
    destination_initial[mip].pSysMem = destination[mip].data();
    destination_initial[mip].SysMemPitch = widths[mip] * sizeof(uint32_t);
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = widths[0];
  desc.Height = heights[0];
  desc.MipLevels = mip_levels;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  ComPtr<ID3D11Texture2D> source_texture;
  ComPtr<ID3D11Texture2D> destination_texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &desc, source_initial.data(), source_texture.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &desc, destination_initial.data(), destination_texture.put())));

  context_.context()->CopySubresourceRegion(
      destination_texture.get(), 1, 0, 0, 0, source_texture.get(), 1, nullptr);

  D3D11_TEXTURE2D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), destination_texture.get());

  for (UINT mip = 0; mip < mip_levels; ++mip) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        staging.get(), mip, D3D11_MAP_READ, 0, &mapped)));
    const auto &expected = mip == 1 ? source[mip] : destination[mip];
    for (UINT y = 0; y < heights[mip]; ++y) {
      EXPECT_EQ(std::memcmp(static_cast<const uint8_t *>(mapped.pData) +
                                y * mapped.RowPitch,
                            expected.data() + y * widths[mip],
                            widths[mip] * sizeof(uint32_t)),
                0)
          << "mip " << mip << ", row " << y;
    }
    context_.context()->Unmap(staging.get(), mip);
  }
}

TEST_F(D3D11ResourceSpec, PreservesTexture1DArraySubresources) {
  constexpr UINT width = 8;
  constexpr UINT mip_levels = 2;
  constexpr UINT array_size = 3;
  constexpr UINT subresource_count = mip_levels * array_size;
  std::array<std::array<uint32_t, width>, subresource_count> contents = {};
  std::array<D3D11_SUBRESOURCE_DATA, subresource_count> initial = {};
  for (UINT slice = 0; slice < array_size; ++slice) {
    for (UINT mip = 0; mip < mip_levels; ++mip) {
      const UINT subresource = D3D11CalcSubresource(mip, slice, mip_levels);
      const UINT mip_width = width >> mip;
      for (UINT x = 0; x < mip_width; ++x)
        contents[subresource][x] = 0x10000000u | subresource << 8 | x;
      initial[subresource].pSysMem = contents[subresource].data();
      initial[subresource].SysMemPitch = mip_width * sizeof(uint32_t);
    }
  }

  D3D11_TEXTURE1D_DESC desc = {};
  desc.Width = width;
  desc.MipLevels = mip_levels;
  desc.ArraySize = array_size;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.Usage = D3D11_USAGE_DEFAULT;
  ComPtr<ID3D11Texture1D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture1D(
      &desc, initial.data(), texture.put())));

  D3D11_TEXTURE1D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture1D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture1D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  for (UINT slice = 0; slice < array_size; ++slice) {
    for (UINT mip = 0; mip < mip_levels; ++mip) {
      const UINT subresource = D3D11CalcSubresource(mip, slice, mip_levels);
      const UINT mip_width = width >> mip;
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
          staging.get(), subresource, D3D11_MAP_READ, 0, &mapped)));
      EXPECT_EQ(std::memcmp(mapped.pData, contents[subresource].data(),
                            mip_width * sizeof(uint32_t)),
                0)
          << "slice " << slice << ", mip " << mip
          << ", first actual=0x" << std::hex
          << static_cast<const uint32_t *>(mapped.pData)[0]
          << ", expected=0x" << contents[subresource][0];
      context_.context()->Unmap(staging.get(), subresource);
    }
  }
}

TEST_F(D3D11ResourceSpec, PreservesStagingTexture1DArraySubresources) {
  constexpr UINT width = 8;
  constexpr UINT mip_levels = 2;
  constexpr UINT array_size = 3;
  constexpr UINT subresource_count = mip_levels * array_size;
  std::array<std::array<uint32_t, width>, subresource_count> contents = {};
  std::array<D3D11_SUBRESOURCE_DATA, subresource_count> initial = {};
  for (UINT slice = 0; slice < array_size; ++slice) {
    for (UINT mip = 0; mip < mip_levels; ++mip) {
      const UINT subresource = D3D11CalcSubresource(mip, slice, mip_levels);
      const UINT mip_width = width >> mip;
      for (UINT x = 0; x < mip_width; ++x)
        contents[subresource][x] = 0x20000000u | subresource << 8 | x;
      initial[subresource].pSysMem = contents[subresource].data();
      initial[subresource].SysMemPitch = mip_width * sizeof(uint32_t);
    }
  }

  D3D11_TEXTURE1D_DESC desc = {};
  desc.Width = width;
  desc.MipLevels = mip_levels;
  desc.ArraySize = array_size;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture1D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture1D(
      &desc, initial.data(), texture.put())));

  for (UINT subresource = 0; subresource < subresource_count; ++subresource) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        texture.get(), subresource, D3D11_MAP_READ, 0, &mapped)));
    const UINT mip_width = width >> (subresource % mip_levels);
    EXPECT_EQ(std::memcmp(mapped.pData, contents[subresource].data(),
                          mip_width * sizeof(uint32_t)),
              0)
        << "subresource " << subresource;
    context_.context()->Unmap(texture.get(), subresource);
  }
}

TEST_F(D3D11ResourceSpec,
       PreservesOddWidthStagingTexture1DMipsWithSourcePadding) {
  constexpr UINT width = 7;
  constexpr UINT mip_levels = 3;
  constexpr UINT array_size = 2;
  constexpr UINT source_pitch = 32;
  constexpr UINT subresource_count = mip_levels * array_size;
  std::array<std::array<std::uint8_t, source_pitch>, subresource_count>
      contents = {};
  std::array<D3D11_SUBRESOURCE_DATA, subresource_count> initial = {};
  for (UINT subresource = 0; subresource < subresource_count; ++subresource) {
    const UINT mip = subresource % mip_levels;
    const UINT mip_width = std::max(1u, width >> mip);
    contents[subresource].fill(0xee);
    for (UINT x = 0; x < mip_width; ++x)
      contents[subresource][x] =
          static_cast<std::uint8_t>(0x20u + subresource * 8u + x);
    initial[subresource].pSysMem = contents[subresource].data();
    initial[subresource].SysMemPitch = source_pitch;
  }

  D3D11_TEXTURE1D_DESC desc = {};
  desc.Width = width;
  desc.MipLevels = mip_levels;
  desc.ArraySize = array_size;
  desc.Format = DXGI_FORMAT_R8_UINT;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture1D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture1D(
      &desc, initial.data(), texture.put())));

  for (UINT subresource = 0; subresource < subresource_count; ++subresource) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        texture.get(), subresource, D3D11_MAP_READ, 0, &mapped)));
    const UINT mip_width =
        std::max(1u, width >> (subresource % mip_levels));
    EXPECT_EQ(std::memcmp(mapped.pData, contents[subresource].data(), mip_width),
              0)
        << "subresource " << subresource << ", width " << mip_width;
    context_.context()->Unmap(texture.get(), subresource);
  }
}

TEST_F(D3D11ResourceSpec,
       PreservesPaddedStagingTexture2DArrayMipRows) {
  constexpr UINT width = 7;
  constexpr UINT height = 5;
  constexpr UINT mip_levels = 3;
  constexpr UINT array_size = 2;
  constexpr UINT source_pitch = 16;
  constexpr UINT subresource_count = mip_levels * array_size;
  std::array<std::array<std::uint8_t, source_pitch * height>,
             subresource_count>
      contents = {};
  std::array<D3D11_SUBRESOURCE_DATA, subresource_count> initial = {};
  for (UINT slice = 0; slice < array_size; ++slice) {
    for (UINT mip = 0; mip < mip_levels; ++mip) {
      const UINT subresource = D3D11CalcSubresource(mip, slice, mip_levels);
      const UINT mip_width = std::max(1u, width >> mip);
      const UINT mip_height = std::max(1u, height >> mip);
      contents[subresource].fill(0xee);
      for (UINT y = 0; y < mip_height; ++y) {
        for (UINT x = 0; x < mip_width; ++x) {
          contents[subresource][y * source_pitch + x] =
              static_cast<std::uint8_t>(0x20u + subresource * 16u + y * 4u +
                                        x);
        }
      }
      initial[subresource].pSysMem = contents[subresource].data();
      initial[subresource].SysMemPitch = source_pitch;
    }
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = mip_levels;
  desc.ArraySize = array_size;
  desc.Format = DXGI_FORMAT_R8_UINT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &desc, initial.data(), texture.put())));

  for (UINT subresource = 0; subresource < subresource_count; ++subresource) {
    const UINT mip = subresource % mip_levels;
    const UINT mip_width = std::max(1u, width >> mip);
    const UINT mip_height = std::max(1u, height >> mip);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        texture.get(), subresource, D3D11_MAP_READ, 0, &mapped)));
    for (UINT y = 0; y < mip_height; ++y) {
      const auto *actual = static_cast<const std::uint8_t *>(mapped.pData) +
                           y * mapped.RowPitch;
      const auto *expected =
          contents[subresource].data() + y * source_pitch;
      EXPECT_EQ(std::memcmp(actual, expected, mip_width), 0)
          << "subresource " << subresource << ", row " << y;
    }
    context_.context()->Unmap(texture.get(), subresource);
  }
}

TEST_F(D3D11ResourceSpec,
       PreservesPaddedStagingTexture3DMipRowsAndSlices) {
  constexpr UINT width = 5;
  constexpr UINT height = 3;
  constexpr UINT depth = 3;
  constexpr UINT mip_levels = 2;
  constexpr UINT source_pitch = 8;
  constexpr UINT source_slice_pitch = 32;
  std::array<std::array<std::uint8_t, source_slice_pitch * depth>, mip_levels>
      contents = {};
  std::array<D3D11_SUBRESOURCE_DATA, mip_levels> initial = {};
  for (UINT mip = 0; mip < mip_levels; ++mip) {
    const UINT mip_width = std::max(1u, width >> mip);
    const UINT mip_height = std::max(1u, height >> mip);
    const UINT mip_depth = std::max(1u, depth >> mip);
    contents[mip].fill(0xee);
    for (UINT z = 0; z < mip_depth; ++z) {
      for (UINT y = 0; y < mip_height; ++y) {
        for (UINT x = 0; x < mip_width; ++x) {
          contents[mip][z * source_slice_pitch + y * source_pitch + x] =
              static_cast<std::uint8_t>(0x30u + mip * 32u + z * 8u + y * 2u +
                                        x);
        }
      }
    }
    initial[mip].pSysMem = contents[mip].data();
    initial[mip].SysMemPitch = source_pitch;
    initial[mip].SysMemSlicePitch = source_slice_pitch;
  }

  D3D11_TEXTURE3D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.Depth = depth;
  desc.MipLevels = mip_levels;
  desc.Format = DXGI_FORMAT_R8_UINT;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture3D> texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture3D(
      &desc, initial.data(), texture.put())));

  for (UINT mip = 0; mip < mip_levels; ++mip) {
    const UINT mip_width = std::max(1u, width >> mip);
    const UINT mip_height = std::max(1u, height >> mip);
    const UINT mip_depth = std::max(1u, depth >> mip);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        texture.get(), mip, D3D11_MAP_READ, 0, &mapped)));
    for (UINT z = 0; z < mip_depth; ++z) {
      for (UINT y = 0; y < mip_height; ++y) {
        const auto *actual = static_cast<const std::uint8_t *>(mapped.pData) +
                             z * mapped.DepthPitch + y * mapped.RowPitch;
        const auto *expected = contents[mip].data() + z * source_slice_pitch +
                               y * source_pitch;
        EXPECT_EQ(std::memcmp(actual, expected, mip_width), 0)
            << "mip " << mip << ", depth " << z << ", row " << y;
      }
    }
    context_.context()->Unmap(texture.get(), mip);
  }
}

TEST_F(D3D11ResourceSpec, CopiesCpuWrittenStagingTexture2DToDefaultTexture) {
  constexpr UINT width = 6;
  constexpr UINT height = 4;
  std::array<std::uint32_t, width * height> expected = {};

  D3D11_TEXTURE2D_DESC staging_desc = {};
  staging_desc.Width = width;
  staging_desc.Height = height;
  staging_desc.MipLevels = 1;
  staging_desc.ArraySize = 1;
  staging_desc.Format = DXGI_FORMAT_R32_UINT;
  staging_desc.SampleDesc.Count = 1;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ComPtr<ID3D11Texture2D> source;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, source.put())));

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      source.get(), 0, D3D11_MAP_WRITE, 0, &mapped)));
  for (UINT y = 0; y < height; ++y) {
    auto *row = reinterpret_cast<std::uint32_t *>(
        static_cast<std::uint8_t *>(mapped.pData) + y * mapped.RowPitch);
    for (UINT x = 0; x < width; ++x)
      row[x] = expected[y * width + x] = 0x40000000u | y << 8 | x;
  }
  context_.context()->Unmap(source.get(), 0);

  D3D11_TEXTURE2D_DESC default_desc = staging_desc;
  default_desc.Usage = D3D11_USAGE_DEFAULT;
  default_desc.CPUAccessFlags = 0;
  ComPtr<ID3D11Texture2D> destination;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &default_desc, nullptr, destination.put())));
  context_.context()->CopyResource(destination.get(), source.get());

  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> readback;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, readback.put())));
  context_.context()->CopyResource(readback.get(), destination.get());
  mapped = {};
  ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
      readback.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  for (UINT y = 0; y < height; ++y) {
    const auto *actual = static_cast<const std::uint8_t *>(mapped.pData) +
                         y * mapped.RowPitch;
    EXPECT_EQ(std::memcmp(actual, expected.data() + y * width,
                          width * sizeof(std::uint32_t)),
              0)
        << "row " << y;
  }
  context_.context()->Unmap(readback.get(), 0);
}

TEST_F(D3D11ResourceSpec, CopiesOneTexture2DArraySliceInIsolation) {
  constexpr UINT width = 4;
  constexpr UINT height = 3;
  constexpr UINT array_size = 3;
  constexpr UINT pixel_count = width * height;
  std::array<std::array<uint32_t, pixel_count>, array_size> source = {};
  std::array<std::array<uint32_t, pixel_count>, array_size> destination = {};
  std::array<D3D11_SUBRESOURCE_DATA, array_size> source_initial = {};
  std::array<D3D11_SUBRESOURCE_DATA, array_size> destination_initial = {};
  for (UINT slice = 0; slice < array_size; ++slice) {
    source[slice].fill(0x11000000u + slice);
    destination[slice].fill(0x22000000u + slice);
    source_initial[slice].pSysMem = source[slice].data();
    source_initial[slice].SysMemPitch = width * sizeof(uint32_t);
    destination_initial[slice].pSysMem = destination[slice].data();
    destination_initial[slice].SysMemPitch = width * sizeof(uint32_t);
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = array_size;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  ComPtr<ID3D11Texture2D> source_texture;
  ComPtr<ID3D11Texture2D> destination_texture;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &desc, source_initial.data(), source_texture.put())));
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &desc, destination_initial.data(), destination_texture.put())));

  constexpr UINT source_slice = 2;
  constexpr UINT destination_slice = 1;
  context_.context()->CopySubresourceRegion(
      destination_texture.get(), destination_slice, 0, 0, 0,
      source_texture.get(), source_slice, nullptr);

  D3D11_TEXTURE2D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), destination_texture.get());

  for (UINT slice = 0; slice < array_size; ++slice) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    ASSERT_TRUE(HResultSucceeded(context_.context()->Map(
        staging.get(), slice, D3D11_MAP_READ, 0, &mapped)));
    const auto &expected =
        slice == destination_slice ? source[source_slice] : destination[slice];
    for (UINT y = 0; y < height; ++y) {
      EXPECT_EQ(std::memcmp(static_cast<const uint8_t *>(mapped.pData) +
                                y * mapped.RowPitch,
                            expected.data() + y * width,
                            width * sizeof(uint32_t)),
                0)
          << "slice " << slice << ", row " << y;
    }
    context_.context()->Unmap(staging.get(), slice);
  }
}

TEST_F(D3D11ResourceSpec, PreservesTexture3DRowsAndDepthSlices) {
  constexpr UINT width = 4;
  constexpr UINT height = 3;
  constexpr UINT depth = 2;
  std::array<uint32_t, width *height *depth> contents = {};
  for (UINT z = 0; z < depth; ++z) {
    for (UINT y = 0; y < height; ++y) {
      for (UINT x = 0; x < width; ++x)
        contents[(z * height + y) * width + x] =
            0x30000000u | z << 16 | y << 8 | x;
    }
  }

  D3D11_TEXTURE3D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.Depth = depth;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.Usage = D3D11_USAGE_DEFAULT;
  D3D11_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = contents.data();
  initial.SysMemPitch = width * sizeof(uint32_t);
  initial.SysMemSlicePitch = width * height * sizeof(uint32_t);
  ComPtr<ID3D11Texture3D> texture;
  ASSERT_TRUE(HResultSucceeded(
      context_.device()->CreateTexture3D(&desc, &initial, texture.put())));

  D3D11_TEXTURE3D_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture3D> staging;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture3D(
      &staging_desc, nullptr, staging.put())));
  context_.context()->CopyResource(staging.get(), texture.get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  ASSERT_TRUE(HResultSucceeded(
      context_.context()->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
  for (UINT z = 0; z < depth; ++z) {
    for (UINT y = 0; y < height; ++y) {
      const auto *actual = static_cast<const uint8_t *>(mapped.pData) +
                           z * mapped.DepthPitch + y * mapped.RowPitch;
      const auto *expected = contents.data() + (z * height + y) * width;
      EXPECT_EQ(std::memcmp(actual, expected, width * sizeof(uint32_t)), 0)
          << "depth " << z << ", row " << y;
    }
  }
  context_.context()->Unmap(staging.get(), 0);
}

TEST_F(D3D11ResourceSpec, RejectsZeroSizedBufferAndClearsOutput) {
  D3D11_BUFFER_DESC desc = {};
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  ID3D11Buffer *buffer = reinterpret_cast<ID3D11Buffer *>(uintptr_t{1});

  const HRESULT hr = context_.device()->CreateBuffer(&desc, nullptr, &buffer);
  EXPECT_TRUE(FAILED(hr));
  EXPECT_EQ(buffer, nullptr);
}

TEST_F(D3D11ResourceSpec, RejectsZeroSizedTexturesAndClearsOutputs) {
  D3D11_TEXTURE1D_DESC desc1d = {};
  desc1d.ArraySize = 1;
  desc1d.MipLevels = 1;
  desc1d.Format = DXGI_FORMAT_R8_UINT;
  desc1d.Usage = D3D11_USAGE_DEFAULT;
  ID3D11Texture1D *texture1d =
      reinterpret_cast<ID3D11Texture1D *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(context_.device()->CreateTexture1D(
      &desc1d, nullptr, &texture1d)));
  EXPECT_EQ(texture1d, nullptr);

  D3D11_TEXTURE2D_DESC desc2d = {};
  desc2d.Width = 1;
  desc2d.MipLevels = 1;
  desc2d.ArraySize = 1;
  desc2d.Format = DXGI_FORMAT_R8_UINT;
  desc2d.SampleDesc.Count = 1;
  desc2d.Usage = D3D11_USAGE_DEFAULT;
  ID3D11Texture2D *texture2d =
      reinterpret_cast<ID3D11Texture2D *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(context_.device()->CreateTexture2D(
      &desc2d, nullptr, &texture2d)));
  EXPECT_EQ(texture2d, nullptr);

  D3D11_TEXTURE3D_DESC desc3d = {};
  desc3d.Width = 1;
  desc3d.Height = 1;
  desc3d.MipLevels = 1;
  desc3d.Format = DXGI_FORMAT_R8_UINT;
  desc3d.Usage = D3D11_USAGE_DEFAULT;
  ID3D11Texture3D *texture3d =
      reinterpret_cast<ID3D11Texture3D *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(context_.device()->CreateTexture3D(
      &desc3d, nullptr, &texture3d)));
  EXPECT_EQ(texture3d, nullptr);
}

TEST_F(D3D11ResourceSpec, RejectsBindFlagsForEveryStagingTextureDimension) {
  D3D11_TEXTURE1D_DESC desc1d = {};
  desc1d.Width = 4;
  desc1d.MipLevels = 1;
  desc1d.ArraySize = 1;
  desc1d.Format = DXGI_FORMAT_R8_UINT;
  desc1d.Usage = D3D11_USAGE_STAGING;
  desc1d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc1d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture1D *texture1d =
      reinterpret_cast<ID3D11Texture1D *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(context_.device()->CreateTexture1D(
      &desc1d, nullptr, &texture1d)));
  EXPECT_EQ(texture1d, nullptr);

  D3D11_TEXTURE2D_DESC desc2d = {};
  desc2d.Width = 4;
  desc2d.Height = 4;
  desc2d.MipLevels = 1;
  desc2d.ArraySize = 1;
  desc2d.Format = DXGI_FORMAT_R8_UINT;
  desc2d.SampleDesc.Count = 1;
  desc2d.Usage = D3D11_USAGE_STAGING;
  desc2d.BindFlags = D3D11_BIND_RENDER_TARGET;
  desc2d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D *texture2d =
      reinterpret_cast<ID3D11Texture2D *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(context_.device()->CreateTexture2D(
      &desc2d, nullptr, &texture2d)));
  EXPECT_EQ(texture2d, nullptr);

  D3D11_TEXTURE3D_DESC desc3d = {};
  desc3d.Width = 4;
  desc3d.Height = 4;
  desc3d.Depth = 4;
  desc3d.MipLevels = 1;
  desc3d.Format = DXGI_FORMAT_R8_UINT;
  desc3d.Usage = D3D11_USAGE_STAGING;
  desc3d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  desc3d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ID3D11Texture3D *texture3d =
      reinterpret_cast<ID3D11Texture3D *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(context_.device()->CreateTexture3D(
      &desc3d, nullptr, &texture3d)));
  EXPECT_EQ(texture3d, nullptr);
}

TEST_F(D3D11ResourceSpec, ExpandsAutomaticMipChainsFromTheLargestDimension) {
  D3D11_TEXTURE1D_DESC desc1d = {};
  desc1d.Width = 7;
  desc1d.ArraySize = 1;
  desc1d.Format = DXGI_FORMAT_R8_UINT;
  desc1d.Usage = D3D11_USAGE_DEFAULT;
  desc1d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture1D> texture1d;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture1D(
      &desc1d, nullptr, texture1d.put())));
  texture1d->GetDesc(&desc1d);
  EXPECT_EQ(desc1d.MipLevels, 3u);

  D3D11_TEXTURE2D_DESC desc2d = {};
  desc2d.Width = 3;
  desc2d.Height = 9;
  desc2d.ArraySize = 1;
  desc2d.Format = DXGI_FORMAT_R8_UINT;
  desc2d.SampleDesc.Count = 1;
  desc2d.Usage = D3D11_USAGE_DEFAULT;
  desc2d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> texture2d;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture2D(
      &desc2d, nullptr, texture2d.put())));
  texture2d->GetDesc(&desc2d);
  EXPECT_EQ(desc2d.MipLevels, 4u);

  D3D11_TEXTURE3D_DESC desc3d = {};
  desc3d.Width = 2;
  desc3d.Height = 3;
  desc3d.Depth = 17;
  desc3d.Format = DXGI_FORMAT_R8_UINT;
  desc3d.Usage = D3D11_USAGE_DEFAULT;
  desc3d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture3D> texture3d;
  ASSERT_TRUE(HResultSucceeded(context_.device()->CreateTexture3D(
      &desc3d, nullptr, texture3d.put())));
  texture3d->GetDesc(&desc3d);
  EXPECT_EQ(desc3d.MipLevels, 5u);
}

} // namespace
