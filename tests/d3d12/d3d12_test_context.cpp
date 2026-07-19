#include "d3d12_test_context.hpp"

#include "shaders/runtime_test_shaders.hpp"

#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace dxmt::test {
namespace {

std::mutex &SharedDeviceMutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<std::string, ComPtr<ID3D12Device>> &SharedDevices() {
  static std::unordered_map<std::string, ComPtr<ID3D12Device>> devices;
  return devices;
}

const char *DebugSeverityName(D3D12_MESSAGE_SEVERITY severity) {
  switch (severity) {
  case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    return "CORRUPTION";
  case D3D12_MESSAGE_SEVERITY_ERROR:
    return "ERROR";
  case D3D12_MESSAGE_SEVERITY_WARNING:
    return "WARNING";
  case D3D12_MESSAGE_SEVERITY_INFO:
    return "INFO";
  case D3D12_MESSAGE_SEVERITY_MESSAGE:
    return "MESSAGE";
  }
  return "UNKNOWN";
}

void __stdcall DebugMessageCallback(D3D12_MESSAGE_CATEGORY category,
                                    D3D12_MESSAGE_SEVERITY severity,
                                    D3D12_MESSAGE_ID id,
                                    const char *description, void *) {
  static std::mutex output_mutex;
  std::scoped_lock lock(output_mutex);
  std::fprintf(stderr, "[ D3D12   ] %s category=%u id=%u: %s\n",
               DebugSeverityName(severity), static_cast<unsigned>(category),
               static_cast<unsigned>(id), description ? description : "");
  std::fflush(stderr);
}

bool EnvironmentEquals(const char *name, std::string_view expected) {
  std::array<char, 32> value = {};
  const DWORD length = GetEnvironmentVariableA(
      name, value.data(), static_cast<DWORD>(value.size()));
  return length == expected.size() && length < value.size() &&
         std::string_view(value.data(), length) == expected;
}

HRESULT EnableWindowsDebugLayer() {
  if (!EnvironmentEquals("DXMT_TEST_WINDOWS_DEBUG_LAYER", "1"))
    return S_OK;

  ComPtr<ID3D12Debug> debug;
  const HRESULT hr = D3D12GetDebugInterface(
      __uuidof(ID3D12Debug), reinterpret_cast<void **>(debug.put()));
  if (FAILED(hr))
    return hr;
  debug->EnableDebugLayer();
  return S_OK;
}

HRESULT CreateNativeD3D12TestDevice(ComPtr<ID3D12Device> *device) {
  static std::once_flag debug_once;
  static HRESULT debug_result = S_OK;
  std::call_once(debug_once,
                 [] { debug_result = EnableWindowsDebugLayer(); });
  if (FAILED(debug_result))
    return debug_result;

  ComPtr<IDXGIAdapter1> adapter;
  if (EnvironmentEquals("DXMT_TEST_WINDOWS_ADAPTER", "warp")) {
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(
        __uuidof(IDXGIFactory4), reinterpret_cast<void **>(factory.put()));
    if (FAILED(hr))
      return hr;
    hr = factory->EnumWarpAdapter(
        __uuidof(IDXGIAdapter1), reinterpret_cast<void **>(adapter.put()));
    if (FAILED(hr))
      return hr;
  }

  return D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0,
                           __uuidof(ID3D12Device),
                           reinterpret_cast<void **>(device->put()));
}

} // namespace

ComPtr<ID3D12Device> CreateIsolatedD3D12Device() {
  ComPtr<ID3D12Device> device;
  const HRESULT hr = CreateNativeD3D12TestDevice(&device);
  return SUCCEEDED(hr) ? std::move(device) : ComPtr<ID3D12Device>{};
}

D3D12TestContext::~D3D12TestContext() {
  if (debug_info_queue_)
    debug_info_queue_->UnregisterMessageCallback(debug_callback_cookie_);
  if (fence_event_)
    CloseHandle(fence_event_);
}

void D3D12TestContext::RegisterDebugMessageCallback() {
  if (!EnvironmentEquals("DXMT_TEST_WINDOWS_DEBUG_LAYER", "1"))
    return;

  ComPtr<ID3D12InfoQueue1> info_queue;
  HRESULT hr = device_->QueryInterface(
      __uuidof(ID3D12InfoQueue1),
      reinterpret_cast<void **>(info_queue.put()));
  if (FAILED(hr)) {
    std::fprintf(stderr,
                 "[ D3D12   ] WARNING: ID3D12InfoQueue1 is unavailable "
                 "(HRESULT %#lx); debug messages will not be captured\n",
                 static_cast<unsigned long>(hr));
    return;
  }

  DWORD cookie = 0;
  hr = info_queue->RegisterMessageCallback(
      DebugMessageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr,
      &cookie);
  if (FAILED(hr)) {
    std::fprintf(stderr,
                 "[ D3D12   ] WARNING: failed to register the D3D12 debug "
                 "message callback (HRESULT %#lx)\n",
                 static_cast<unsigned long>(hr));
    return;
  }

  debug_info_queue_ = std::move(info_queue);
  debug_callback_cookie_ = cookie;
}

HRESULT D3D12TestContext::Initialize() {
  ComPtr<ID3D12Device> device;
  HRESULT hr = CreateNativeD3D12TestDevice(&device);
  if (FAILED(hr))
    return hr;

  return Initialize(device.get());
}

HRESULT D3D12TestContext::InitializeSharedDevice(std::string_view domain) {
  if (domain.empty())
    return E_INVALIDARG;

  std::scoped_lock lock(SharedDeviceMutex());
  auto &devices = SharedDevices();
  auto existing = devices.find(std::string(domain));
  if (existing == devices.end()) {
    ComPtr<ID3D12Device> device;
    const HRESULT hr = CreateNativeD3D12TestDevice(&device);
    if (FAILED(hr))
      return hr;
    existing = devices.emplace(std::string(domain), std::move(device)).first;
  }
  return Initialize(existing->second.get());
}

HRESULT D3D12TestContext::Initialize(ID3D12Device *device) {
  if (!device)
    return E_INVALIDARG;
  device->AddRef();
  device_.reset(device);
  RegisterDebugMessageCallback();

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  HRESULT hr = device_->CreateCommandQueue(
      &queue_desc, __uuidof(ID3D12CommandQueue),
      reinterpret_cast<void **>(queue_.put()));
  if (FAILED(hr))
    return hr;

  hr = device_->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(allocator_.put()));
  if (FAILED(hr))
    return hr;

  hr = device_->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.get(), nullptr,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(list_.put()));
  if (FAILED(hr))
    return hr;

  hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                            reinterpret_cast<void **>(fence_.put()));
  if (FAILED(hr))
    return hr;

  fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  return fence_event_ ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

HRESULT D3D12TestContext::ResetCommandList() {
  HRESULT hr = allocator_->Reset();
  if (FAILED(hr))
    return hr;
  return list_->Reset(allocator_.get(), nullptr);
}

HRESULT D3D12TestContext::WaitForFence(ID3D12Fence *fence, UINT64 value) {
  if (fence->GetCompletedValue() >= value)
    return S_OK;

  HRESULT hr = fence->SetEventOnCompletion(value, fence_event_);
  if (FAILED(hr))
    return hr;

  DWORD wait_result = WaitForSingleObject(fence_event_, INFINITE);
  return wait_result == WAIT_OBJECT_0
             ? S_OK
             : HRESULT_FROM_WIN32(wait_result == WAIT_FAILED ? GetLastError()
                                                              : ERROR_TIMEOUT);
}

HRESULT D3D12TestContext::SignalAndWait() {
  const UINT64 value = ++fence_value_;
  HRESULT hr = queue_->Signal(fence_.get(), value);
  return FAILED(hr) ? hr : WaitForFence(fence_.get(), value);
}

HRESULT D3D12TestContext::ExecuteAndWait() {
  HRESULT hr = list_->Close();
  if (FAILED(hr))
    return hr;

  ID3D12CommandList *lists[] = {list_.get()};
  queue_->ExecuteCommandLists(1, lists);
  return SignalAndWait();
}

ComPtr<ID3D12Resource>
D3D12TestContext::CreateBuffer(UINT64 size, D3D12_HEAP_TYPE heap_type,
                               D3D12_RESOURCE_FLAGS flags,
                               D3D12_RESOURCE_STATES state) const {
  D3D12_HEAP_PROPERTIES heap_properties = {};
  heap_properties.Type = heap_type;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;

  ComPtr<ID3D12Resource> resource;
  HRESULT hr = device_->CreateCommittedResource(
      &heap_properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(resource.put()));
  return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>();
}

ComPtr<ID3D12Resource>
D3D12TestContext::CreateUploadBuffer(UINT64 size, const void *data,
                                     std::size_t data_size) const {
  ComPtr<ID3D12Resource> buffer =
      CreateBuffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                   D3D12_RESOURCE_STATE_GENERIC_READ);
  if (!buffer || !data)
    return buffer;

  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, 0};
  if (FAILED(buffer->Map(0, &read_range, &mapped)))
    return ComPtr<ID3D12Resource>();
  std::memcpy(mapped, data, std::min<std::size_t>(data_size, size));
  D3D12_RANGE written_range = {0, std::min<SIZE_T>(data_size, size)};
  buffer->Unmap(0, &written_range);
  return buffer;
}

ComPtr<ID3D12Resource>
D3D12TestContext::CreateTexture2D(UINT64 width, UINT height, UINT16 mip_levels,
                                  DXGI_FORMAT format,
                                  D3D12_RESOURCE_FLAGS flags,
                                  D3D12_RESOURCE_STATES state) const {
  D3D12_HEAP_PROPERTIES heap_properties = {};
  heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = mip_levels;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;

  ComPtr<ID3D12Resource> resource;
  HRESULT hr = device_->CreateCommittedResource(
      &heap_properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(resource.put()));
  return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>();
}

ComPtr<ID3D12DescriptorHeap> D3D12TestContext::CreateDescriptorHeap(
    D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool shader_visible) const {
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = type;
  desc.NumDescriptors = count;
  desc.Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                              : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

  ComPtr<ID3D12DescriptorHeap> heap;
  HRESULT hr = device_->CreateDescriptorHeap(
      &desc, __uuidof(ID3D12DescriptorHeap),
      reinterpret_cast<void **>(heap.put()));
  return SUCCEEDED(hr) ? std::move(heap) : ComPtr<ID3D12DescriptorHeap>();
}

ComPtr<ID3D12RootSignature> D3D12TestContext::CreateRootSignature(
    const D3D12_ROOT_SIGNATURE_DESC &desc) const {
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  HRESULT hr = D3D12SerializeRootSignature(
      &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(), error.put());
  if (FAILED(hr))
    return {};

  ComPtr<ID3D12RootSignature> root_signature;
  hr = device_->CreateRootSignature(
      0, blob->GetBufferPointer(), blob->GetBufferSize(),
      __uuidof(ID3D12RootSignature),
      reinterpret_cast<void **>(root_signature.put()));
  return SUCCEEDED(hr) ? std::move(root_signature)
                       : ComPtr<ID3D12RootSignature>();
}

ComPtr<ID3D12PipelineState> D3D12TestContext::CreateComputePipeline(
    ID3D12RootSignature *root_signature, D3D12_SHADER_BYTECODE shader) const {
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature;
  desc.CS = shader;

  ComPtr<ID3D12PipelineState> pipeline;
  HRESULT hr = device_->CreateComputePipelineState(
      &desc, __uuidof(ID3D12PipelineState),
      reinterpret_cast<void **>(pipeline.put()));
  return SUCCEEDED(hr) ? std::move(pipeline) : ComPtr<ID3D12PipelineState>();
}

ComPtr<ID3D12PipelineState> D3D12TestContext::CreateGraphicsPipeline(
    ID3D12RootSignature *root_signature, DXGI_FORMAT render_target_format,
    D3D12_SHADER_BYTECODE pixel_shader) const {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature;
  desc.VS = FullscreenVertexShader();
  desc.PS = pixel_shader;
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  desc.SampleMask = std::numeric_limits<UINT>::max();
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = render_target_format;
  desc.SampleDesc.Count = 1;

  ComPtr<ID3D12PipelineState> pipeline;
  HRESULT hr = device_->CreateGraphicsPipelineState(
      &desc, __uuidof(ID3D12PipelineState),
      reinterpret_cast<void **>(pipeline.put()));
  return SUCCEEDED(hr) ? std::move(pipeline) : ComPtr<ID3D12PipelineState>();
}

HRESULT D3D12TestContext::UploadTextureAndReset(
    ID3D12Resource *texture, const void *data, UINT64 row_pitch,
    UINT64 slice_pitch, UINT subresource) {
  D3D12_RESOURCE_DESC texture_desc = texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  device_->GetCopyableFootprints(&texture_desc, subresource, 1, 0, &footprint,
                                 &row_count, &row_size, &total_size);

  ComPtr<ID3D12Resource> upload = CreateUploadBuffer(total_size);
  if (!upload)
    return E_OUTOFMEMORY;

  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, 0};
  HRESULT hr = upload->Map(0, &read_range, &mapped);
  if (FAILED(hr))
    return hr;

  auto *destination = static_cast<std::uint8_t *>(mapped) + footprint.Offset;
  const auto *source = static_cast<const std::uint8_t *>(data);
  const UINT depth = footprint.Footprint.Depth;
  for (UINT z = 0; z < depth; ++z) {
    for (UINT y = 0; y < row_count; ++y) {
      std::memcpy(destination + z * footprint.Footprint.RowPitch * row_count +
                      y * footprint.Footprint.RowPitch,
                  source + z * slice_pitch + y * row_pitch,
                  static_cast<std::size_t>(row_size));
    }
  }
  D3D12_RANGE written_range = {static_cast<SIZE_T>(footprint.Offset),
                               static_cast<SIZE_T>(total_size)};
  upload->Unmap(0, &written_range);

  D3D12_TEXTURE_COPY_LOCATION source_location = {};
  source_location.pResource = upload.get();
  source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source_location.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION destination_location = {};
  destination_location.pResource = texture;
  destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  destination_location.SubresourceIndex = subresource;
  list_->CopyTextureRegion(&destination_location, 0, 0, 0, &source_location,
                           nullptr);

  hr = ExecuteAndWait();
  return FAILED(hr) ? hr : ResetCommandList();
}

HRESULT D3D12TestContext::ReadbackTexture(ID3D12Resource *texture,
                                          TextureReadback *readback,
                                          UINT subresource) {
  D3D12_RESOURCE_DESC desc = texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  device_->GetCopyableFootprints(&desc, subresource, 1, 0, &footprint,
                                 &row_count, &row_size, &total_size);

  ComPtr<ID3D12Resource> buffer =
      CreateBuffer(total_size, D3D12_HEAP_TYPE_READBACK,
                   D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  if (!buffer)
    return E_OUTOFMEMORY;

  D3D12_TEXTURE_COPY_LOCATION destination = {};
  destination.pResource = buffer.get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION source = {};
  source.pResource = texture;
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  source.SubresourceIndex = subresource;
  list_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

  HRESULT hr = ExecuteAndWait();
  if (FAILED(hr))
    return hr;

  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, static_cast<SIZE_T>(total_size)};
  hr = buffer->Map(0, &read_range, &mapped);
  if (FAILED(hr))
    return hr;

  readback->data.resize(static_cast<std::size_t>(total_size));
  std::memcpy(readback->data.data(), mapped, readback->data.size());
  D3D12_RANGE written_range = {0, 0};
  buffer->Unmap(0, &written_range);
  readback->row_pitch = footprint.Footprint.RowPitch;
  readback->width = footprint.Footprint.Width;
  readback->height = footprint.Footprint.Height;
  return S_OK;
}

HRESULT D3D12TestContext::ReadbackBuffer(
    ID3D12Resource *buffer, UINT64 size,
    std::vector<std::uint8_t> *data) {
  ComPtr<ID3D12Resource> readback =
      CreateBuffer(size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                   D3D12_RESOURCE_STATE_COPY_DEST);
  if (!readback)
    return E_OUTOFMEMORY;

  list_->CopyBufferRegion(readback.get(), 0, buffer, 0, size);
  HRESULT hr = ExecuteAndWait();
  if (FAILED(hr))
    return hr;

  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, static_cast<SIZE_T>(size)};
  hr = readback->Map(0, &read_range, &mapped);
  if (FAILED(hr))
    return hr;
  data->resize(static_cast<std::size_t>(size));
  std::memcpy(data->data(), mapped, data->size());
  D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
  return S_OK;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12TestContext::CpuDescriptorHandle(
    ID3D12DescriptorHeap *heap, UINT index) const {
  D3D12_CPU_DESCRIPTOR_HANDLE handle =
      heap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(index) *
                device_->GetDescriptorHandleIncrementSize(heap->GetDesc().Type);
  return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12TestContext::GpuDescriptorHandle(
    ID3D12DescriptorHeap *heap, UINT index) const {
  D3D12_GPU_DESCRIPTOR_HANDLE handle =
      heap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<UINT64>(index) *
                device_->GetDescriptorHandleIncrementSize(heap->GetDesc().Type);
  return handle;
}

void D3D12TestContext::Transition(ID3D12GraphicsCommandList *list,
                                  ID3D12Resource *resource,
                                  D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  list->ResourceBarrier(1, &barrier);
}

void D3D12TestContext::UavBarrier(ID3D12GraphicsCommandList *list,
                                  ID3D12Resource *resource) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = resource;
  list->ResourceBarrier(1, &barrier);
}

bool ColorsMatch(std::uint32_t actual, std::uint32_t expected,
                 unsigned int max_channel_difference) {
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    const int actual_channel = (actual >> shift) & 0xff;
    const int expected_channel = (expected >> shift) & 0xff;
    if (std::abs(actual_channel - expected_channel) >
        static_cast<int>(max_channel_difference))
      return false;
  }
  return true;
}

} // namespace dxmt::test
