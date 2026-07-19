#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
#include <dxgi.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>

namespace {

template <typename T> void release_object(T *&object) {
  if (object) {
    object->Release();
    object = nullptr;
  }
}

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();

  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

class D3D10CoreDeviceSpec : public ::testing::Test {
protected:
  void SetUp() override {
    const HRESULT hr =
        D3D10CreateDevice(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0,
                          D3D10_SDK_VERSION, &device_);
    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_NE(device_, nullptr);
  }

  void TearDown() override { release_object(device_); }

  ID3D10Device *device_ = nullptr;
};

} // namespace

TEST_F(D3D10CoreDeviceSpec, QueriesMultithreadInterface) {
  ID3D10Multithread *multithread = nullptr;

  HRESULT hr = device_->QueryInterface(__uuidof(ID3D10Multithread),
                                       reinterpret_cast<void **>(&multithread));

  EXPECT_TRUE(HResultSucceeded(hr));
  EXPECT_NE(multithread, nullptr);

  release_object(multithread);
}

TEST_F(D3D10CoreDeviceSpec, CreatesDefaultVertexBuffer) {
  D3D10_BUFFER_DESC desc = {};
  desc.ByteWidth = 256;
  desc.Usage = D3D10_USAGE_DEFAULT;
  desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;

  ID3D10Buffer *buffer = nullptr;

  HRESULT hr = device_->CreateBuffer(&desc, nullptr, &buffer);

  EXPECT_TRUE(HResultSucceeded(hr));
  EXPECT_NE(buffer, nullptr);

  release_object(buffer);
}

TEST_F(D3D10CoreDeviceSpec, ReadsInitializedBufferThroughStagingCopy) {
  constexpr std::array<uint32_t, 8> input = {
      23, 29, 31, 37, 41, 43, 47, 53,
  };
  D3D10_BUFFER_DESC source_desc = {};
  source_desc.ByteWidth = sizeof(input);
  source_desc.Usage = D3D10_USAGE_DEFAULT;
  source_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
  D3D10_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = input.data();

  ID3D10Buffer *source = nullptr;
  ASSERT_TRUE(
      HResultSucceeded(device_->CreateBuffer(&source_desc, &initial, &source)));
  ASSERT_NE(source, nullptr);

  D3D10_BUFFER_DESC staging_desc = source_desc;
  staging_desc.Usage = D3D10_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
  ID3D10Buffer *staging = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateBuffer(&staging_desc, nullptr, &staging)));
  ASSERT_NE(staging, nullptr);

  device_->CopyResource(staging, source);
  void *mapped = nullptr;
  ASSERT_TRUE(HResultSucceeded(staging->Map(D3D10_MAP_READ, 0, &mapped)));
  EXPECT_EQ(std::memcmp(mapped, input.data(), sizeof(input)), 0);
  staging->Unmap();

  release_object(staging);
  release_object(source);
}

TEST_F(D3D10CoreDeviceSpec, CreatesTextureAndDefaultShaderResourceView) {
  constexpr std::array<uint32_t, 4> pixels = {
      0xff102030,
      0xff405060,
      0xff708090,
      0xffa0b0c0,
  };
  D3D10_TEXTURE2D_DESC desc = {};
  desc.Width = 2;
  desc.Height = 2;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D10_USAGE_DEFAULT;
  desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
  D3D10_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = pixels.data();
  initial.SysMemPitch = 2 * sizeof(uint32_t);

  ID3D10Texture2D *texture = nullptr;
  ASSERT_TRUE(
      HResultSucceeded(device_->CreateTexture2D(&desc, &initial, &texture)));
  ASSERT_NE(texture, nullptr);
  ID3D10ShaderResourceView *view = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateShaderResourceView(texture, nullptr, &view)));
  ASSERT_NE(view, nullptr);

  D3D10_SHADER_RESOURCE_VIEW_DESC view_desc = {};
  view->GetDesc(&view_desc);
  EXPECT_EQ(view_desc.Format, desc.Format);
  EXPECT_EQ(view_desc.ViewDimension, D3D10_SRV_DIMENSION_TEXTURE2D);

  release_object(view);
  release_object(texture);
}

TEST_F(D3D10CoreDeviceSpec, RejectsZeroSizedBufferAndClearsOutput) {
  D3D10_BUFFER_DESC desc = {};
  desc.Usage = D3D10_USAGE_DEFAULT;
  desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
  ID3D10Buffer *buffer = reinterpret_cast<ID3D10Buffer *>(uintptr_t{1});

  const HRESULT hr = device_->CreateBuffer(&desc, nullptr, &buffer);
  EXPECT_TRUE(FAILED(hr));
  EXPECT_EQ(buffer, nullptr);
}

TEST_F(D3D10CoreDeviceSpec, WritesDynamicBufferWithDiscardAndNoOverwrite) {
  std::array<std::uint32_t, 16> expected = {};
  D3D10_BUFFER_DESC dynamic_desc = {};
  dynamic_desc.ByteWidth = sizeof(expected);
  dynamic_desc.Usage = D3D10_USAGE_DYNAMIC;
  dynamic_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
  dynamic_desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
  ID3D10Buffer *dynamic = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateBuffer(&dynamic_desc, nullptr, &dynamic)));

  void *mapped = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      dynamic->Map(D3D10_MAP_WRITE_DISCARD, 0, &mapped)));
  auto *values = static_cast<std::uint32_t *>(mapped);
  for (UINT i = 0; i < 8; ++i)
    values[i] = expected[i] = 0x1000u + i;
  dynamic->Unmap();

  mapped = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      dynamic->Map(D3D10_MAP_WRITE_NO_OVERWRITE, 0, &mapped)));
  values = static_cast<std::uint32_t *>(mapped);
  for (UINT i = 8; i < expected.size(); ++i)
    values[i] = expected[i] = 0x2000u + i;
  dynamic->Unmap();

  D3D10_BUFFER_DESC staging_desc = dynamic_desc;
  staging_desc.Usage = D3D10_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
  ID3D10Buffer *staging = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateBuffer(&staging_desc, nullptr, &staging)));
  device_->CopyResource(staging, dynamic);
  mapped = nullptr;
  ASSERT_TRUE(HResultSucceeded(staging->Map(D3D10_MAP_READ, 0, &mapped)));
  EXPECT_EQ(std::memcmp(mapped, expected.data(), sizeof(expected)), 0);
  staging->Unmap();

  release_object(staging);
  release_object(dynamic);
}

TEST_F(D3D10CoreDeviceSpec, PreservesPaddedTexture2DRowsOnReadback) {
  constexpr UINT width = 7;
  constexpr UINT height = 3;
  constexpr UINT source_pitch = 40;
  std::array<std::uint8_t, source_pitch * height> source = {};
  source.fill(0xee);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      const std::uint32_t pixel = 0xff000000u | y << 8 | x;
      std::memcpy(source.data() + y * source_pitch + x * sizeof(pixel),
                  &pixel, sizeof(pixel));
    }
  }

  D3D10_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D10_USAGE_DEFAULT;
  D3D10_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = source.data();
  initial.SysMemPitch = source_pitch;
  ID3D10Texture2D *texture = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateTexture2D(&desc, &initial, &texture)));

  desc.Usage = D3D10_USAGE_STAGING;
  desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
  ID3D10Texture2D *staging = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateTexture2D(&desc, nullptr, &staging)));
  device_->CopyResource(staging, texture);

  D3D10_MAPPED_TEXTURE2D mapped = {};
  ASSERT_TRUE(
      HResultSucceeded(staging->Map(0, D3D10_MAP_READ, 0, &mapped)));
  for (UINT y = 0; y < height; ++y) {
    const auto *actual = static_cast<const std::uint8_t *>(mapped.pData) +
                         y * mapped.RowPitch;
    const auto *expected = source.data() + y * source_pitch;
    EXPECT_EQ(std::memcmp(actual, expected, width * sizeof(std::uint32_t)), 0)
        << "row " << y;
  }
  staging->Unmap(0);

  release_object(staging);
  release_object(texture);
}

TEST_F(D3D10CoreDeviceSpec, PreservesTexture3DRowsAndDepthSlices) {
  constexpr UINT width = 5;
  constexpr UINT height = 3;
  constexpr UINT depth = 2;
  constexpr UINT source_pitch = 8;
  constexpr UINT source_slice_pitch = 32;
  std::array<std::uint8_t, source_slice_pitch * depth> source = {};
  source.fill(0xee);
  for (UINT z = 0; z < depth; ++z) {
    for (UINT y = 0; y < height; ++y) {
      for (UINT x = 0; x < width; ++x) {
        source[z * source_slice_pitch + y * source_pitch + x] =
            static_cast<std::uint8_t>(0x20u + z * 32u + y * 8u + x);
      }
    }
  }

  D3D10_TEXTURE3D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.Depth = depth;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8_UINT;
  desc.Usage = D3D10_USAGE_DEFAULT;
  D3D10_SUBRESOURCE_DATA initial = {};
  initial.pSysMem = source.data();
  initial.SysMemPitch = source_pitch;
  initial.SysMemSlicePitch = source_slice_pitch;
  ID3D10Texture3D *texture = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateTexture3D(&desc, &initial, &texture)));

  desc.Usage = D3D10_USAGE_STAGING;
  desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
  ID3D10Texture3D *staging = nullptr;
  ASSERT_TRUE(HResultSucceeded(
      device_->CreateTexture3D(&desc, nullptr, &staging)));
  device_->CopyResource(staging, texture);

  D3D10_MAPPED_TEXTURE3D mapped = {};
  ASSERT_TRUE(
      HResultSucceeded(staging->Map(0, D3D10_MAP_READ, 0, &mapped)));
  for (UINT z = 0; z < depth; ++z) {
    for (UINT y = 0; y < height; ++y) {
      const auto *actual = static_cast<const std::uint8_t *>(mapped.pData) +
                           z * mapped.DepthPitch + y * mapped.RowPitch;
      const auto *expected =
          source.data() + z * source_slice_pitch + y * source_pitch;
      EXPECT_EQ(std::memcmp(actual, expected, width), 0)
          << "depth " << z << ", row " << y;
    }
  }
  staging->Unmap(0);

  release_object(staging);
  release_object(texture);
}

TEST_F(D3D10CoreDeviceSpec, RejectsZeroSizedTexturesAndClearsOutputs) {
  D3D10_TEXTURE1D_DESC desc1d = {};
  desc1d.MipLevels = 1;
  desc1d.ArraySize = 1;
  desc1d.Format = DXGI_FORMAT_R8_UINT;
  desc1d.Usage = D3D10_USAGE_DEFAULT;
  ID3D10Texture1D *texture1d =
      reinterpret_cast<ID3D10Texture1D *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(
      device_->CreateTexture1D(&desc1d, nullptr, &texture1d)));
  EXPECT_EQ(texture1d, nullptr);

  D3D10_TEXTURE2D_DESC desc2d = {};
  desc2d.Width = 1;
  desc2d.MipLevels = 1;
  desc2d.ArraySize = 1;
  desc2d.Format = DXGI_FORMAT_R8_UINT;
  desc2d.SampleDesc.Count = 1;
  desc2d.Usage = D3D10_USAGE_DEFAULT;
  ID3D10Texture2D *texture2d =
      reinterpret_cast<ID3D10Texture2D *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(
      device_->CreateTexture2D(&desc2d, nullptr, &texture2d)));
  EXPECT_EQ(texture2d, nullptr);

  D3D10_TEXTURE3D_DESC desc3d = {};
  desc3d.Width = 1;
  desc3d.Height = 1;
  desc3d.MipLevels = 1;
  desc3d.Format = DXGI_FORMAT_R8_UINT;
  desc3d.Usage = D3D10_USAGE_DEFAULT;
  ID3D10Texture3D *texture3d =
      reinterpret_cast<ID3D10Texture3D *>(uintptr_t{1});
  EXPECT_TRUE(FAILED(
      device_->CreateTexture3D(&desc3d, nullptr, &texture3d)));
  EXPECT_EQ(texture3d, nullptr);
}
