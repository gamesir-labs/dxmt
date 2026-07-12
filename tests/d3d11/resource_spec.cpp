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

TEST_F(D3D11ResourceSpec, RejectsZeroSizedBufferAndClearsOutput) {
  D3D11_BUFFER_DESC desc = {};
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  ID3D11Buffer *buffer = reinterpret_cast<ID3D11Buffer *>(uintptr_t{1});

  const HRESULT hr = context_.device()->CreateBuffer(&desc, nullptr, &buffer);
  EXPECT_TRUE(FAILED(hr));
  EXPECT_EQ(buffer, nullptr);
}

} // namespace
