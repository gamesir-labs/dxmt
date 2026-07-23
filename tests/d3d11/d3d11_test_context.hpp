#pragma once

#include <dxmt_test_com.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11_3.h>
#include <dxgi1_6.h>

#include <array>

namespace dxmt::test {

class D3D11TestContext {
public:
  HRESULT Initialize() {
    const HRESULT adapter_hr = InitializeAdapter();
    if (FAILED(adapter_hr))
      return adapter_hr;
    return InitializeDevice();
  }

  HRESULT InitializeAdapter() {
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void **>(factory_.put()));
    if (FAILED(hr) || !factory_)
      return FAILED(hr) ? hr : E_UNEXPECTED;

    ComPtr<IDXGIFactory6> factory6;
    factory_->QueryInterface(__uuidof(IDXGIFactory6),
                             reinterpret_cast<void **>(factory6.put()));
    if (factory6) {
      hr = factory6->EnumAdapterByGpuPreference(
          0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter),
          reinterpret_cast<void **>(adapter_.put()));
    } else {
      hr = factory_->EnumAdapters(0, adapter_.put());
    }
    if (FAILED(hr) || !adapter_)
      return FAILED(hr) ? hr : E_UNEXPECTED;

    hr = adapter_->GetDesc(&adapter_desc_);
    return hr;
  }

  HRESULT InitializeDevice() {
    if (!adapter_)
      return E_UNEXPECTED;
    constexpr std::array feature_levels = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    const HRESULT hr = D3D11CreateDevice(
        adapter_.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels.data(),
        static_cast<UINT>(feature_levels.size()), D3D11_SDK_VERSION,
        device_.put(), &feature_level_, context_.put());
    if (SUCCEEDED(hr) && (!device_ || !context_))
      return E_UNEXPECTED;
    return hr;
  }

  IDXGIFactory1 *factory() const { return factory_.get(); }
  IDXGIAdapter *adapter() const { return adapter_.get(); }
  ID3D11Device *device() const { return device_.get(); }
  ID3D11DeviceContext *context() const { return context_.get(); }
  D3D_FEATURE_LEVEL feature_level() const { return feature_level_; }
  const DXGI_ADAPTER_DESC &adapter_desc() const { return adapter_desc_; }

private:
  ComPtr<IDXGIFactory1> factory_;
  ComPtr<IDXGIAdapter> adapter_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL(0);
  DXGI_ADAPTER_DESC adapter_desc_ = {};
};

} // namespace dxmt::test
