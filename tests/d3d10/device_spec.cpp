#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
#include <dxgi.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>

extern "C" HRESULT WINAPI D3D10CoreCreateDevice(IDXGIFactory *factory,
                                                IDXGIAdapter *adapter,
                                                UINT flags,
                                                D3D_FEATURE_LEVEL feature_level,
                                                ID3D10Device **device);

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
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void **>(&factory_));
    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_NE(factory_, nullptr);

    hr = factory_->EnumAdapters(0, &adapter_);
    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_NE(adapter_, nullptr);

    hr = D3D10CoreCreateDevice(factory_, adapter_, 0, D3D_FEATURE_LEVEL_10_0,
                               &device_);
    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_NE(device_, nullptr);
  }

  void TearDown() override {
    release_object(device_);
    release_object(adapter_);
    release_object(factory_);
  }

  IDXGIFactory1 *factory_ = nullptr;
  IDXGIAdapter *adapter_ = nullptr;
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
