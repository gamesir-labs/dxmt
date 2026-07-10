#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11_3.h>
#include <dxgi1_6.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ComPtr;

constexpr UINT kMicrosoftVendorId = 0x1414;
constexpr UINT kTextureWidth = 64;
constexpr UINT kTextureHeight = 64;

bool Fail(const char *stage, const char *detail) {
  std::fprintf(stderr, "ue_d3d11_initialization: %s failed: %s\n", stage,
               detail);
  return false;
}

bool CheckHResult(const char *stage, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;

  std::fprintf(stderr,
               "ue_d3d11_initialization: %s failed with HRESULT 0x%08lx\n",
               stage, static_cast<unsigned long>(hr));
  return false;
}

bool CheckWin32(const char *stage, BOOL result) {
  if (result)
    return true;

  std::fprintf(stderr,
               "ue_d3d11_initialization: %s failed with Win32 error %lu\n",
               stage, static_cast<unsigned long>(GetLastError()));
  return false;
}

struct AdapterCandidate {
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC description = {};
  D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL(0);
  bool unified_memory = false;
};

enum class AdapterProbeResult {
  Supported,
  Unsupported,
  FatalError,
};

HRESULT EnumerateAdapter(IDXGIFactory1 *factory, IDXGIFactory6 *factory6,
                         UINT index, IDXGIAdapter **adapter) {
  if (factory6) {
    return factory6->EnumAdapterByGpuPreference(
        index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter),
        reinterpret_cast<void **>(adapter));
  }
  return factory->EnumAdapters(index, adapter);
}

AdapterProbeResult ProbeAdapter(IDXGIAdapter *adapter,
                                AdapterCandidate *candidate) {
  if (FAILED(adapter->GetDesc(&candidate->description)))
    return AdapterProbeResult::Unsupported;

  constexpr std::array feature_levels = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
  };

  ComPtr<ID3D11Device> probe_device;
  ComPtr<ID3D11DeviceContext> probe_context;
  D3D_FEATURE_LEVEL actual_feature_level = D3D_FEATURE_LEVEL(0);
  const HRESULT create_hr = D3D11CreateDevice(
      adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      feature_levels.data(), static_cast<UINT>(feature_levels.size()),
      D3D11_SDK_VERSION, probe_device.put(), &actual_feature_level,
      probe_context.put());
  if (FAILED(create_hr))
    return AdapterProbeResult::Unsupported;
  if (!probe_device || !probe_context) {
    Fail("probe D3D11CreateDevice", "missing device or context");
    return AdapterProbeResult::FatalError;
  }
  if (actual_feature_level < D3D_FEATURE_LEVEL_11_0 ||
      actual_feature_level > D3D_FEATURE_LEVEL_11_1) {
    Fail("probe feature level", "UE requires feature level 11_0 or 11_1");
    return AdapterProbeResult::FatalError;
  }
  if (probe_device->GetFeatureLevel() != actual_feature_level) {
    Fail("probe feature level", "device and output feature levels differ");
    return AdapterProbeResult::FatalError;
  }

  const UINT required_probe_flags =
      D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  if ((probe_device->GetCreationFlags() & required_probe_flags) !=
      required_probe_flags) {
    Fail("probe creation flags",
         "SINGLETHREADED or BGRA_SUPPORT was not preserved");
    return AdapterProbeResult::FatalError;
  }

  ComPtr<ID3D11Device3> probe_device3;
  D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2 = {};
  if (SUCCEEDED(probe_device->QueryInterface(
          __uuidof(ID3D11Device3),
          reinterpret_cast<void **>(probe_device3.put()))) &&
      probe_device3) {
    probe_device3->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options2,
                                       sizeof(options2));
  }

  candidate->feature_level = actual_feature_level;
  candidate->unified_memory = options2.UnifiedMemoryArchitecture != FALSE;
  if (!CheckHResult("retain probed adapter",
                    adapter->QueryInterface(
                        __uuidof(IDXGIAdapter),
                        reinterpret_cast<void **>(candidate->adapter.put()))))
    return AdapterProbeResult::FatalError;
  if (!candidate->adapter) {
    Fail("retain probed adapter", "successful call returned null");
    return AdapterProbeResult::FatalError;
  }

  // The probe device and context intentionally die here before final creation.
  return AdapterProbeResult::Supported;
}

std::size_t SelectAdapter(const std::vector<AdapterCandidate> &candidates) {
  std::size_t best_discrete = candidates.size();
  SIZE_T best_dedicated_memory = 0;
  std::size_t first_hardware = candidates.size();

  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const AdapterCandidate &candidate = candidates[i];
    if (candidate.description.VendorId == kMicrosoftVendorId)
      continue;

    if (first_hardware == candidates.size())
      first_hardware = i;

    if (!candidate.unified_memory &&
        (best_discrete == candidates.size() ||
         candidate.description.DedicatedVideoMemory > best_dedicated_memory)) {
      best_discrete = i;
      best_dedicated_memory = candidate.description.DedicatedVideoMemory;
    }
  }

  if (best_discrete != candidates.size())
    return best_discrete;
  return first_hardware;
}

struct FormatRequirement {
  DXGI_FORMAT format;
  UINT required_support;
  UINT required_support2;
  const char *name;
};

bool DiscoverFormat(ID3D11Device *device,
                    const FormatRequirement &requirement) {
  D3D11_FEATURE_DATA_FORMAT_SUPPORT support = {};
  support.InFormat = requirement.format;
  HRESULT hr = device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT,
                                           &support, sizeof(support));
  if (FAILED(hr)) {
    std::fprintf(stderr,
                 "ue_d3d11_initialization: format %s support query failed "
                 "with HRESULT 0x%08lx\n",
                 requirement.name, static_cast<unsigned long>(hr));
    return false;
  }
  if ((support.OutFormatSupport & requirement.required_support) !=
      requirement.required_support) {
    std::fprintf(stderr,
                 "ue_d3d11_initialization: format %s support was 0x%08x, "
                 "required 0x%08x\n",
                 requirement.name, support.OutFormatSupport,
                 requirement.required_support);
    return false;
  }

  D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support2 = {};
  support2.InFormat = requirement.format;
  hr = device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support2,
                                   sizeof(support2));
  if (FAILED(hr)) {
    std::fprintf(stderr,
                 "ue_d3d11_initialization: format %s support2 query failed "
                 "with HRESULT 0x%08lx\n",
                 requirement.name, static_cast<unsigned long>(hr));
    return false;
  }
  if ((support2.OutFormatSupport2 & requirement.required_support2) !=
      requirement.required_support2) {
    std::fprintf(stderr,
                 "ue_d3d11_initialization: format %s support2 was 0x%08x, "
                 "required 0x%08x\n",
                 requirement.name, support2.OutFormatSupport2,
                 requirement.required_support2);
    return false;
  }
  return true;
}

bool DiscoverDeviceCapabilities(ID3D11Device *device, IDXGIAdapter *adapter) {
  ComPtr<ID3D11Device3> device3;
  D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2 = {};
  if (SUCCEEDED(device->QueryInterface(
          __uuidof(ID3D11Device3), reinterpret_cast<void **>(device3.put()))) &&
      device3) {
    device3->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options2,
                                 sizeof(options2));
  }

  D3D11_FEATURE_DATA_THREADING threading = {};
  if (!CheckHResult("D3D11_THREADING",
                    device->CheckFeatureSupport(D3D11_FEATURE_THREADING,
                                                &threading, sizeof(threading))))
    return false;

  D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
  device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options,
                              sizeof(options));

  D3D11_FEATURE_DATA_D3D11_OPTIONS3 options3 = {};
  device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &options3,
                              sizeof(options3));

  constexpr std::array format_requirements = {
      FormatRequirement{DXGI_FORMAT_R8G8B8A8_UNORM,
                        D3D11_FORMAT_SUPPORT_TEXTURE2D |
                            D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                            D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
                            D3D11_FORMAT_SUPPORT_BLENDABLE,
                        0, "R8G8B8A8_UNORM"},
      FormatRequirement{
          DXGI_FORMAT_B8G8R8A8_UNORM,
          D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET |
              D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_DISPLAY,
          0, "B8G8R8A8_UNORM"},
      FormatRequirement{DXGI_FORMAT_R16G16B16A16_FLOAT,
                        D3D11_FORMAT_SUPPORT_TEXTURE2D |
                            D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                            D3D11_FORMAT_SUPPORT_SHADER_SAMPLE,
                        0, "R16G16B16A16_FLOAT"},
      FormatRequirement{DXGI_FORMAT_R32G8X24_TYPELESS,
                        D3D11_FORMAT_SUPPORT_TEXTURE2D, 0, "R32G8X24_TYPELESS"},
      FormatRequirement{DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
                        D3D11_FORMAT_SUPPORT_TEXTURE2D |
                            D3D11_FORMAT_SUPPORT_SHADER_SAMPLE,
                        0, "R32_FLOAT_X8X24_TYPELESS"},
      FormatRequirement{DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
                        D3D11_FORMAT_SUPPORT_TEXTURE2D |
                            D3D11_FORMAT_SUPPORT_DEPTH_STENCIL,
                        0, "D32_FLOAT_S8X24_UINT"},
      FormatRequirement{DXGI_FORMAT_R32_UINT,
                        D3D11_FORMAT_SUPPORT_BUFFER |
                            D3D11_FORMAT_SUPPORT_TEXTURE2D,
                        0, "R32_UINT"},
  };
  for (const FormatRequirement &requirement : format_requirements) {
    if (!DiscoverFormat(device, requirement))
      return false;
  }

  constexpr std::array<UINT, 4> sample_counts = {1, 2, 4, 8};
  for (UINT sample_count : sample_counts) {
    UINT quality_levels = 0;
    const HRESULT msaa_hr = device->CheckMultisampleQualityLevels(
        DXGI_FORMAT_R8G8B8A8_UNORM, sample_count, &quality_levels);
    if (FAILED(msaa_hr) && sample_count == 1)
      return CheckHResult("CheckMultisampleQualityLevels", msaa_hr);
    if (sample_count == 1 && quality_levels == 0)
      return Fail("CheckMultisampleQualityLevels",
                  "single-sample color target was reported unsupported");
  }

  ComPtr<IDXGIAdapter3> adapter3;
  if (SUCCEEDED(
          adapter->QueryInterface(__uuidof(IDXGIAdapter3),
                                  reinterpret_cast<void **>(adapter3.put()))) &&
      adapter3) {
    DXGI_QUERY_VIDEO_MEMORY_INFO memory_info = {};
    const HRESULT memory_hr = adapter3->QueryVideoMemoryInfo(
        0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory_info);
    if (SUCCEEDED(memory_hr) && memory_info.Budget == 0)
      return Fail("video memory budget", "adapter reported a zero budget");
  }

  return true;
}

struct Vertex {
  float position[3];
  float color[4];
};

bool ValidateReadback(const D3D11_MAPPED_SUBRESOURCE &mapped,
                      const std::array<float, 4> &clear_color) {
  if (!mapped.pData || mapped.RowPitch < kTextureWidth * 4)
    return Fail("color readback", "invalid mapped pointer or row pitch");

  std::array<int, 4> expected = {};
  for (std::size_t channel = 0; channel < expected.size(); ++channel) {
    expected[channel] = static_cast<int>(clear_color[channel] * 255.0f + 0.5f);
  }

  const auto *base = static_cast<const std::uint8_t *>(mapped.pData);
  for (UINT y = 0; y < kTextureHeight; ++y) {
    const std::uint8_t *row =
        base + static_cast<std::size_t>(y) * mapped.RowPitch;
    for (UINT x = 0; x < kTextureWidth; ++x) {
      const std::uint8_t *pixel = row + x * 4;
      for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        const int difference =
            static_cast<int>(pixel[channel]) - expected[channel];
        if (difference < -1 || difference > 1) {
          std::fprintf(
              stderr,
              "ue_d3d11_initialization: color readback failed at (%u,%u) "
              "channel %zu: got %u, expected %d\n",
              x, y, channel, static_cast<unsigned int>(pixel[channel]),
              expected[channel]);
          return false;
        }
      }
    }
  }
  return true;
}

bool BootstrapResourcesAndReadback(ID3D11Device *device,
                                   ID3D11DeviceContext *context) {
  const std::array<float, 16> constants = {
      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
  };
  D3D11_BUFFER_DESC constant_desc = {};
  constant_desc.ByteWidth = static_cast<UINT>(sizeof(constants));
  constant_desc.Usage = D3D11_USAGE_DEFAULT;
  constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  D3D11_SUBRESOURCE_DATA constant_data = {};
  constant_data.pSysMem = constants.data();
  ComPtr<ID3D11Buffer> constant_buffer;
  if (!CheckHResult("constant buffer creation",
                    device->CreateBuffer(&constant_desc, &constant_data,
                                         constant_buffer.put())))
    return false;

  constexpr std::array vertices = {
      Vertex{{-1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      Vertex{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
      Vertex{{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
  };
  D3D11_BUFFER_DESC vertex_desc = {};
  vertex_desc.ByteWidth = static_cast<UINT>(sizeof(vertices));
  vertex_desc.Usage = D3D11_USAGE_DEFAULT;
  vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA vertex_data = {};
  vertex_data.pSysMem = vertices.data();
  ComPtr<ID3D11Buffer> vertex_buffer;
  if (!CheckHResult("vertex buffer creation",
                    device->CreateBuffer(&vertex_desc, &vertex_data,
                                         vertex_buffer.put())))
    return false;

  D3D11_TEXTURE2D_DESC color_desc = {};
  color_desc.Width = kTextureWidth;
  color_desc.Height = kTextureHeight;
  color_desc.MipLevels = 1;
  color_desc.ArraySize = 1;
  color_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  color_desc.SampleDesc.Count = 1;
  color_desc.Usage = D3D11_USAGE_DEFAULT;
  color_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> color_texture;
  if (!CheckHResult(
          "color texture creation",
          device->CreateTexture2D(&color_desc, nullptr, color_texture.put())))
    return false;

  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
  rtv_desc.Format = color_desc.Format;
  rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  ComPtr<ID3D11RenderTargetView> color_rtv;
  if (!CheckHResult("color RTV creation",
                    device->CreateRenderTargetView(color_texture.get(),
                                                   &rtv_desc, color_rtv.put())))
    return false;

  D3D11_SHADER_RESOURCE_VIEW_DESC color_srv_desc = {};
  color_srv_desc.Format = color_desc.Format;
  color_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  color_srv_desc.Texture2D.MipLevels = 1;
  ComPtr<ID3D11ShaderResourceView> color_srv;
  if (!CheckHResult("color SRV creation",
                    device->CreateShaderResourceView(
                        color_texture.get(), &color_srv_desc, color_srv.put())))
    return false;

  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_desc.Width = kTextureWidth;
  depth_desc.Height = kTextureHeight;
  depth_desc.MipLevels = 1;
  depth_desc.ArraySize = 1;
  depth_desc.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Usage = D3D11_USAGE_DEFAULT;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> depth_texture;
  if (!CheckHResult(
          "depth texture creation",
          device->CreateTexture2D(&depth_desc, nullptr, depth_texture.put())))
    return false;

  D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
  dsv_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  ComPtr<ID3D11DepthStencilView> depth_dsv;
  if (!CheckHResult("depth DSV creation",
                    device->CreateDepthStencilView(depth_texture.get(),
                                                   &dsv_desc, depth_dsv.put())))
    return false;

  D3D11_SHADER_RESOURCE_VIEW_DESC depth_srv_desc = {};
  depth_srv_desc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
  depth_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  depth_srv_desc.Texture2D.MipLevels = 1;
  ComPtr<ID3D11ShaderResourceView> depth_srv;
  if (!CheckHResult("depth SRV creation",
                    device->CreateShaderResourceView(
                        depth_texture.get(), &depth_srv_desc, depth_srv.put())))
    return false;

  D3D11_SAMPLER_DESC sampler_desc = {};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler_state;
  if (!CheckHResult(
          "sampler state creation",
          device->CreateSamplerState(&sampler_desc, sampler_state.put())))
    return false;

  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D11BlendState> blend_state;
  if (!CheckHResult("blend state creation",
                    device->CreateBlendState(&blend_desc, blend_state.put())))
    return false;

  D3D11_RASTERIZER_DESC rasterizer_desc = {};
  rasterizer_desc.FillMode = D3D11_FILL_SOLID;
  rasterizer_desc.CullMode = D3D11_CULL_BACK;
  rasterizer_desc.DepthClipEnable = TRUE;
  ComPtr<ID3D11RasterizerState> rasterizer_state;
  if (!CheckHResult("rasterizer state creation",
                    device->CreateRasterizerState(&rasterizer_desc,
                                                  rasterizer_state.put())))
    return false;

  D3D11_DEPTH_STENCIL_DESC depth_state_desc = {};
  depth_state_desc.DepthEnable = TRUE;
  depth_state_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_state_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  ComPtr<ID3D11DepthStencilState> depth_state;
  if (!CheckHResult("depth stencil state creation",
                    device->CreateDepthStencilState(&depth_state_desc,
                                                    depth_state.put())))
    return false;

  ID3D11Buffer *vertex_buffers[] = {vertex_buffer.get()};
  const UINT vertex_strides[] = {static_cast<UINT>(sizeof(Vertex))};
  const UINT vertex_offsets[] = {0};
  context->IASetVertexBuffers(0, 1, vertex_buffers, vertex_strides,
                              vertex_offsets);
  ID3D11Buffer *constant_buffers[] = {constant_buffer.get()};
  context->VSSetConstantBuffers(0, 1, constant_buffers);
  ID3D11SamplerState *samplers[] = {sampler_state.get()};
  context->PSSetSamplers(0, 1, samplers);
  context->OMSetBlendState(blend_state.get(), nullptr, 0xffffffff);
  context->RSSetState(rasterizer_state.get());
  context->OMSetDepthStencilState(depth_state.get(), 0);
  ID3D11RenderTargetView *render_targets[] = {color_rtv.get()};
  context->OMSetRenderTargets(1, render_targets, depth_dsv.get());

  constexpr std::array<float, 4> clear_color = {0.25f, 0.5f, 0.75f, 1.0f};
  context->ClearRenderTargetView(color_rtv.get(), clear_color.data());
  context->ClearDepthStencilView(
      depth_dsv.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.375f, 0x5a);

  D3D11_TEXTURE2D_DESC staging_desc = color_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging_texture;
  if (!CheckHResult("staging texture creation",
                    device->CreateTexture2D(&staging_desc, nullptr,
                                            staging_texture.put())))
    return false;

  context->CopyResource(staging_texture.get(), color_texture.get());
  context->Flush();

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (!CheckHResult(
          "staging texture map",
          context->Map(staging_texture.get(), 0, D3D11_MAP_READ, 0, &mapped)))
    return false;
  const bool readback_valid = ValidateReadback(mapped, clear_color);
  context->Unmap(staging_texture.get(), 0);
  if (!readback_valid)
    return false;

  context->ClearState();
  context->Flush();
  if (!CheckHResult("final device status", device->GetDeviceRemovedReason()))
    return false;

  // Release in reverse bootstrap order while the device and context are alive.
  staging_texture.reset();
  depth_state.reset();
  rasterizer_state.reset();
  blend_state.reset();
  sampler_state.reset();
  depth_srv.reset();
  depth_dsv.reset();
  depth_texture.reset();
  color_srv.reset();
  color_rtv.reset();
  color_texture.reset();
  vertex_buffer.reset();
  constant_buffer.reset();
  return true;
}

LRESULT CALLBACK InitializationWindowProcedure(HWND window, UINT message,
                                               WPARAM wparam, LPARAM lparam) {
  return DefWindowProcW(window, message, wparam, lparam);
}

class HiddenInitializationWindow {
public:
  ~HiddenInitializationWindow() { Destroy(); }

  bool Initialize() {
    instance_ = GetModuleHandleW(nullptr);
    if (!instance_)
      return CheckWin32("GetModuleHandleW", FALSE);

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = InitializationWindowProcedure;
    window_class.hInstance = instance_;
    window_class.lpszClassName = kClassName;
    if (!RegisterClassExW(&window_class))
      return CheckWin32("RegisterClassExW", FALSE);
    class_registered_ = true;

    window_ =
        CreateWindowExW(0, kClassName, L"DXMT Unreal D3D11 Initialization",
                        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640,
                        480, nullptr, nullptr, instance_, nullptr);
    if (!window_)
      return CheckWin32("CreateWindowExW", FALSE);
    return true;
  }

  bool Destroy() {
    bool succeeded = true;
    if (window_) {
      if (!DestroyWindow(window_)) {
        CheckWin32("DestroyWindow", FALSE);
        succeeded = false;
      }
      window_ = nullptr;
    }
    if (class_registered_) {
      if (!UnregisterClassW(kClassName, instance_)) {
        CheckWin32("UnregisterClassW", FALSE);
        succeeded = false;
      }
      class_registered_ = false;
    }
    return succeeded;
  }

  HWND get() const { return window_; }

private:
  static constexpr const wchar_t *kClassName =
      L"DXMTUnrealD3D11InitializationWindow";

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  bool class_registered_ = false;
};

bool CreateViewportSwapChain(IDXGIFactory1 *factory, ID3D11Device *device,
                             HWND window, UINT width, UINT height,
                             ComPtr<IDXGISwapChain> *swap_chain) {
  HRESULT modern_hr = E_NOINTERFACE;
  ComPtr<IDXGIFactory2> factory2;
  if (SUCCEEDED(
          factory->QueryInterface(__uuidof(IDXGIFactory2),
                                  reinterpret_cast<void **>(factory2.put()))) &&
      factory2) {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc = {};
    fullscreen_desc.Windowed = TRUE;
    ComPtr<IDXGISwapChain1> swap_chain1;
    modern_hr = factory2->CreateSwapChainForHwnd(
        device, window, &desc, &fullscreen_desc, nullptr, swap_chain1.put());
    if (SUCCEEDED(modern_hr) && swap_chain1) {
      const HRESULT interface_hr = swap_chain1->QueryInterface(
          __uuidof(IDXGISwapChain),
          reinterpret_cast<void **>(swap_chain->put()));
      if (!CheckHResult("flip-discard swap-chain interface", interface_hr))
        return false;
      if (!swap_chain->get())
        return Fail("flip-discard swap-chain interface",
                    "successful query returned null");
      return true;
    }
    if (SUCCEEDED(modern_hr))
      modern_hr = E_POINTER;
  }

  DXGI_SWAP_CHAIN_DESC desc = {};
  desc.BufferDesc.Width = width;
  desc.BufferDesc.Height = height;
  desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
  desc.BufferCount = 1;
  desc.OutputWindow = window;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  const HRESULT legacy_hr =
      factory->CreateSwapChain(device, &desc, swap_chain->put());
  if (FAILED(legacy_hr)) {
    std::fprintf(
        stderr,
        "ue_d3d11_initialization: viewport swap-chain creation failed; "
        "modern HRESULT 0x%08lx, legacy HRESULT 0x%08lx\n",
        static_cast<unsigned long>(modern_hr),
        static_cast<unsigned long>(legacy_hr));
    return false;
  }
  if (!swap_chain->get())
    return Fail("legacy swap-chain creation",
                "successful creation returned null");
  return true;
}

bool ClearAndPresentViewport(ID3D11Device *device, ID3D11DeviceContext *context,
                             IDXGISwapChain *swap_chain, UINT expected_width,
                             UINT expected_height,
                             const std::array<float, 4> &clear_color,
                             const char *stage) {
  ComPtr<ID3D11Texture2D> back_buffer;
  HRESULT hr =
      swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                            reinterpret_cast<void **>(back_buffer.put()));
  if (!CheckHResult(stage, hr))
    return false;
  if (!back_buffer)
    return Fail(stage, "successful GetBuffer returned null");

  D3D11_TEXTURE2D_DESC back_buffer_desc = {};
  back_buffer->GetDesc(&back_buffer_desc);
  if (back_buffer_desc.Width != expected_width ||
      back_buffer_desc.Height != expected_height) {
    std::fprintf(stderr,
                 "ue_d3d11_initialization: %s failed: back buffer was "
                 "%ux%u, expected %ux%u\n",
                 stage, back_buffer_desc.Width, back_buffer_desc.Height,
                 expected_width, expected_height);
    return false;
  }

  ComPtr<ID3D11RenderTargetView> render_target;
  if (!CheckHResult(stage,
                    device->CreateRenderTargetView(back_buffer.get(), nullptr,
                                                   render_target.put())))
    return false;
  if (!render_target)
    return Fail(stage, "successful RTV creation returned null");

  ID3D11RenderTargetView *render_targets[] = {render_target.get()};
  context->OMSetRenderTargets(1, render_targets, nullptr);
  context->ClearRenderTargetView(render_target.get(), clear_color.data());
  context->Flush();
  hr = swap_chain->Present(0, 0);

  context->OMSetRenderTargets(0, nullptr, nullptr);
  context->Flush();
  return CheckHResult(stage, hr);
}

bool BootstrapViewport(IDXGIFactory1 *factory, ID3D11Device *device,
                       ID3D11DeviceContext *context) {
  constexpr UINT initial_width = 320;
  constexpr UINT initial_height = 180;
  constexpr UINT resized_width = 400;
  constexpr UINT resized_height = 240;

  HiddenInitializationWindow window;
  if (!window.Initialize())
    return false;

  ComPtr<IDXGISwapChain> swap_chain;
  if (!CreateViewportSwapChain(factory, device, window.get(), initial_width,
                               initial_height, &swap_chain))
    return false;
  if (!CheckHResult("MakeWindowAssociation",
                    factory->MakeWindowAssociation(window.get(),
                                                   DXGI_MWA_NO_WINDOW_CHANGES)))
    return false;

  constexpr std::array<float, 4> first_clear = {0.1f, 0.2f, 0.3f, 1.0f};
  if (!ClearAndPresentViewport(device, context, swap_chain.get(), initial_width,
                               initial_height, first_clear,
                               "initial viewport clear and Present"))
    return false;

  context->ClearState();
  context->Flush();
  if (!CheckHResult("ResizeBuffers",
                    swap_chain->ResizeBuffers(0, resized_width, resized_height,
                                              DXGI_FORMAT_B8G8R8A8_UNORM, 0)))
    return false;

  constexpr std::array<float, 4> second_clear = {0.6f, 0.4f, 0.2f, 1.0f};
  if (!ClearAndPresentViewport(device, context, swap_chain.get(), resized_width,
                               resized_height, second_clear,
                               "resized viewport clear and Present"))
    return false;

  context->ClearState();
  context->Flush();
  swap_chain.reset();
  return window.Destroy();
}

bool RunUnrealD3D11Initialization() {
  ComPtr<IDXGIFactory1> factory;
  if (!CheckHResult(
          "CreateDXGIFactory1",
          CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                             reinterpret_cast<void **>(factory.put()))))
    return false;
  if (!factory)
    return Fail("CreateDXGIFactory1", "successful creation returned null");

  ComPtr<IDXGIFactory6> factory6;
  const HRESULT factory6_hr = factory->QueryInterface(
      __uuidof(IDXGIFactory6), reinterpret_cast<void **>(factory6.put()));
  if (FAILED(factory6_hr))
    factory6.reset();

  std::vector<AdapterCandidate> candidates;
  for (UINT adapter_index = 0;; ++adapter_index) {
    ComPtr<IDXGIAdapter> adapter;
    const HRESULT enum_hr = EnumerateAdapter(factory.get(), factory6.get(),
                                             adapter_index, adapter.put());
    if (enum_hr == DXGI_ERROR_NOT_FOUND)
      break;
    if (!CheckHResult("adapter enumeration", enum_hr))
      return false;
    if (!adapter)
      return Fail("adapter enumeration", "successful call returned null");

    AdapterCandidate candidate;
    const AdapterProbeResult probe_result =
        ProbeAdapter(adapter.get(), &candidate);
    if (probe_result == AdapterProbeResult::FatalError)
      return false;
    if (probe_result == AdapterProbeResult::Unsupported)
      continue;
    candidates.push_back(std::move(candidate));
  }
  if (candidates.empty())
    return Fail("adapter enumeration", "no D3D11 adapters were found");

  const std::size_t selected_index = SelectAdapter(candidates);
  if (selected_index == candidates.size())
    return Fail("adapter selection", "no non-software adapter was found");

  const D3D_FEATURE_LEVEL selected_feature_level =
      candidates[selected_index].feature_level;
  ComPtr<IDXGIAdapter> selected_adapter;
  if (!CheckHResult("selected adapter retention",
                    candidates[selected_index].adapter->QueryInterface(
                        __uuidof(IDXGIAdapter),
                        reinterpret_cast<void **>(selected_adapter.put()))))
    return false;
  if (!selected_adapter)
    return Fail("selected adapter retention", "successful query returned null");
  candidates.clear();

  ComPtr<IDXGIFactory1> selected_parent_factory;
  if (!CheckHResult(
          "selected adapter parent factory",
          selected_adapter->GetParent(
              __uuidof(IDXGIFactory1),
              reinterpret_cast<void **>(selected_parent_factory.put()))))
    return false;
  if (!selected_parent_factory)
    return Fail("selected adapter parent factory",
                "successful query returned null");

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL actual_feature_level = D3D_FEATURE_LEVEL(0);
  if (!CheckHResult("actual D3D11CreateDevice",
                    D3D11CreateDevice(
                        selected_adapter.get(), D3D_DRIVER_TYPE_UNKNOWN,
                        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                        &selected_feature_level, 1, D3D11_SDK_VERSION,
                        device.put(), &actual_feature_level, context.put())))
    return false;
  if (!device || !context)
    return Fail("actual D3D11CreateDevice", "missing device or context");
  if (actual_feature_level != selected_feature_level ||
      device->GetFeatureLevel() != selected_feature_level) {
    return Fail("actual feature level", "probe and actual levels differ");
  }
  if ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_BGRA_SUPPORT) == 0 ||
      (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0) {
    return Fail("actual creation flags",
                "expected BGRA support without SINGLETHREADED");
  }

  if (!DiscoverDeviceCapabilities(device.get(), selected_adapter.get()))
    return false;
  if (!BootstrapResourcesAndReadback(device.get(), context.get()))
    return false;
  if (!BootstrapViewport(selected_parent_factory.get(), device.get(),
                         context.get()))
    return false;

  // Final reverse teardown: context, device, adapter, then factories.
  context.reset();
  device.reset();
  selected_parent_factory.reset();
  selected_adapter.reset();
  factory6.reset();
  factory.reset();
  return true;
}

} // namespace

int main() { return RunUnrealD3D11Initialization() ? 0 : 1; }
