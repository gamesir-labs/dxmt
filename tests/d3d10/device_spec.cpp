#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
#include <dxgi.h>

#include <iomanip>

extern "C" HRESULT WINAPI D3D10CoreCreateDevice(
    IDXGIFactory *factory, IDXGIAdapter *adapter, UINT flags,
    D3D_FEATURE_LEVEL feature_level, ID3D10Device **device);

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
