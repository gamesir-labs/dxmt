#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

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
