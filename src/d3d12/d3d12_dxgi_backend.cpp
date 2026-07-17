#include "d3d12_dxgi_backend.hpp"
#include <string>

#include "dxmt_format.hpp"
#include "dxmt_shader_cache.hpp"
#include "log/log.hpp"
#include "util_env.hpp"

#include <exception>
#include <mutex>

namespace dxmt::d3d12 {
namespace {

std::string
ResolveMetalCachePath(const char *path) {
  std::string cache_path = path ? path : "";
  if (cache_path.empty())
    return {};

  try {
    if (cache_path.starts_with("/"))
      return cache_path;

    if (cache_path.size() >= 2 && cache_path[1] == ':')
      return env::getUnixPath(cache_path);

    return cache_path;
  } catch (const std::exception &) {
    dxmt::Logger::warn("Failed to resolve Metal4 cache path");
  } catch (...) {
    dxmt::Logger::warn("Failed to resolve Metal4 cache path");
  }

  return {};
}

bool
SetMetalCachePath(const char *path) {
  auto unix_path = ResolveMetalCachePath(path);
  if (unix_path.empty())
    return false;
  return WMT4SetMetalShaderCachePath(unix_path.c_str());
}

uint32_t
AdapterCount() {
  return uint32_t(WMT::CopyAllDevices().count());
}

bool
GetAdapterInfo(uint32_t index, dxmt::DxgiBackendAdapterInfo *info) {
  if (!info)
    return false;
  auto devices = WMT::CopyAllDevices();
  if (index >= devices.count())
    return false;
  auto device = devices.object(index);
  *info = {};
  info->device_handle = device.handle;
  info->registry_id = device.registryID();
  info->backend_index = index;
  info->has_unified_memory = device.hasUnifiedMemory();
  info->recommended_max_working_set_size = device.recommendedMaxWorkingSetSize();
  info->current_allocated_size = device.currentAllocatedSize();
  device.name().getCString(reinterpret_cast<char *>(info->name),
                           sizeof(info->name), WMTUTF16StringEncoding);
  return true;
}

void
RetainDevice(uint64_t device_handle) {
  WMT::Object{device_handle}.retain();
}

void
ReleaseDevice(uint64_t device_handle) {
  WMT::Object{device_handle}.release();
}

bool
IsBackBufferFormatSupported(uint64_t device_handle, DXGI_FORMAT format) {
  MTL_DXGI_FORMAT_DESC desc = {};
  return SUCCEEDED(MTLQueryDXGIFormat(WMT::Device{device_handle}, format, desc)) &&
         (desc.Flag & MTL_DXGI_FORMAT_BACKBUFFER);
}

void
InitializeMetalCachePath() {
  if (env::getEnvVar("DXMT_USE_DEFAULT_METAL_CACHE") == "1")
    return;
  // Prefer the DXMT shader-cache root itself. Appending com.apple.metal4 can
  // make MTLGetShaderCachePath() report a different active path than we set,
  // which previously forced every FH4 run onto the shared Wine system Metal
  // cache under /var/folders/.../org.winehq.wine and poisoned commit A/B.
  auto metal_cache_path = dxmt::GetDXMTShaderCacheDirectory();
  if (metal_cache_path.empty())
    return;
  if (!SetMetalCachePath(metal_cache_path.c_str())) {
    auto nested = metal_cache_path + "com.apple.metal4";
    if (!SetMetalCachePath(nested.c_str()))
      dxmt::Logger::warn(
          std::string("Failed to set Metal4 cache path to ") + metal_cache_path +
          " (and nested " + nested + "); Metal may use system default cache");
    else
      dxmt::Logger::info(std::string("Metal4 cache path set to nested ") +
                         nested);
  } else {
    dxmt::Logger::info(std::string("Metal4 cache path set to ") +
                       metal_cache_path);
  }
}

dxmt::DxgiBackendProvider
BackendProvider() {
  dxmt::DxgiBackendProvider provider = {};
  provider.kind = dxmt::DxgiBackendKind::Metal4;
  provider.name = "winemetal4";
  provider.priority = 100;
  provider.set_metal_cache_path = SetMetalCachePath;
  provider.adapter_count = AdapterCount;
  provider.get_adapter_info = GetAdapterInfo;
  provider.retain_device = RetainDevice;
  provider.release_device = ReleaseDevice;
  provider.is_backbuffer_format_supported = IsBackBufferFormatSupported;
  return provider;
}

} // namespace

HRESULT
EnsureD3D12DxgiBackendRegistered() {
  static std::once_flag once;
  static HRESULT result = E_FAIL;
  std::call_once(once, [] {
    InitializeMetalCachePath();
    auto provider = BackendProvider();
    result = dxmt::DXMTDXGIRegisterBackend(&provider);
  });
  return result;
}

WMT::Device
GetD3D12AdapterDevice(IMTLDXGIAdapter *adapter) {
  if (!adapter)
    return {};
  if (FAILED(EnsureD3D12DxgiBackendRegistered()))
    return {};
  auto kind = adapter->GetBackendKind();
  if (kind != dxmt::DxgiBackendKind::Unknown &&
      kind != dxmt::DxgiBackendKind::Metal4)
    return {};
  if (!adapter->GetMetalDeviceHandle()) {
    auto provider = BackendProvider();
    if (!adapter->ResolveBackendAdapter(&provider))
      return {};
  }
  return WMT::Device{adapter->GetMetalDeviceHandle()};
}

WMT::Device
GetD3D12DeviceMetalDevice(IMTLDXGIDevice *device) {
  return device ? WMT::Device{device->GetMetalDeviceHandle()} : WMT::Device{};
}

} // namespace dxmt::d3d12
