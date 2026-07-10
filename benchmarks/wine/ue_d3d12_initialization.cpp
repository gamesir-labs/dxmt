#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ComPtr;

constexpr UINT kTextureWidth = 64;
constexpr UINT kTextureHeight = 64;
constexpr UINT kViewDescriptorCount = 1'000'000;
constexpr UINT kSamplerDescriptorCount = 2'048;
constexpr UINT kOfflineViewDescriptorCount = 4'096;
constexpr UINT kOfflineSamplerDescriptorCount = 256;
constexpr UINT kOfflineRenderTargetDescriptorCount = 256;

bool Fail(const char *stage, const char *detail) {
  std::fprintf(stderr, "ue_d3d12_initialization: %s failed: %s\n", stage,
               detail);
  return false;
}

bool CheckHResult(const char *stage, HRESULT hr) {
  if (SUCCEEDED(hr))
    return true;

  std::fprintf(stderr,
               "ue_d3d12_initialization: %s failed with HRESULT 0x%08lx\n",
               stage, static_cast<unsigned long>(hr));
  return false;
}

bool CheckWin32(const char *stage, BOOL result) {
  if (result)
    return true;

  std::fprintf(stderr,
               "ue_d3d12_initialization: %s failed with Win32 error %lu\n",
               stage, static_cast<unsigned long>(GetLastError()));
  return false;
}

HRESULT EnumerateAdapter(IDXGIFactory4 *factory, IDXGIFactory6 *factory6,
                         UINT index, IDXGIAdapter1 **adapter) {
  if (factory6) {
    return factory6->EnumAdapterByGpuPreference(
        index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1),
        reinterpret_cast<void **>(adapter));
  }
  return factory->EnumAdapters1(index, adapter);
}

struct ProbeResult {
  UINT enumeration_index = 0;
  bool used_gpu_preference = false;
  DXGI_ADAPTER_DESC1 description = {};
  D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL(0);
  D3D_SHADER_MODEL shader_model = D3D_SHADER_MODEL_5_1;
  D3D12_RESOURCE_BINDING_TIER resource_binding_tier =
      D3D12_RESOURCE_BINDING_TIER(0);
  bool supports_wave_ops = false;
  bool supports_typed_atomic64 = false;
  bool supports_emulated_atomic64 = false;
  bool uses_sm6 = false;
  bool unified_memory = false;
};

bool SupportsUnrealSm6(const ProbeResult &result, bool fully_bindless) {
  const auto required_binding_tier = fully_bindless
                                         ? D3D12_RESOURCE_BINDING_TIER_3
                                         : D3D12_RESOURCE_BINDING_TIER_2;
  return result.feature_level >= D3D_FEATURE_LEVEL_12_0 &&
         result.shader_model >= D3D_SHADER_MODEL_6_6 &&
         result.supports_wave_ops &&
         result.resource_binding_tier >= required_binding_tier &&
         (result.supports_typed_atomic64 || result.supports_emulated_atomic64);
}

bool ProbeDevice(ID3D12Device *device, ProbeResult *result) {
  constexpr std::array feature_levels = {
      D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
  };
  D3D12_FEATURE_DATA_FEATURE_LEVELS feature_data = {};
  feature_data.NumFeatureLevels = static_cast<UINT>(feature_levels.size());
  feature_data.pFeatureLevelsRequested = feature_levels.data();
  const HRESULT feature_level_hr = device->CheckFeatureSupport(
      D3D12_FEATURE_FEATURE_LEVELS, &feature_data, sizeof(feature_data));
  result->feature_level = SUCCEEDED(feature_level_hr)
                              ? feature_data.MaxSupportedFeatureLevel
                              : D3D_FEATURE_LEVEL_11_0;
  if (result->feature_level < D3D_FEATURE_LEVEL_11_0)
    return Fail("probe feature levels", "feature level 11_0 is required");

  constexpr std::array shader_models = {
      D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5,
      D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2,
      D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0,
  };
  bool found_shader_model = false;
  for (const D3D_SHADER_MODEL requested : shader_models) {
    D3D12_FEATURE_DATA_SHADER_MODEL shader_data = {};
    shader_data.HighestShaderModel = requested;
    const HRESULT hr = device->CheckFeatureSupport(
        D3D12_FEATURE_SHADER_MODEL, &shader_data, sizeof(shader_data));
    if (SUCCEEDED(hr)) {
      result->shader_model = shader_data.HighestShaderModel;
      found_shader_model = true;
      break;
    }
  }
  if (!found_shader_model)
    result->shader_model = D3D_SHADER_MODEL_5_1;

  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  const HRESULT options_hr = device->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
  if (SUCCEEDED(options_hr) &&
      (options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_1 ||
       options.ResourceHeapTier < D3D12_RESOURCE_HEAP_TIER_1)) {
    return Fail("probe D3D12_OPTIONS", "invalid resource capability tier");
  }
  if (SUCCEEDED(options_hr))
    result->resource_binding_tier = options.ResourceBindingTier;

  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
  const HRESULT options1_hr = device->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));
  if (SUCCEEDED(options1_hr) && options1.WaveOps &&
      (options1.WaveLaneCountMin == 0 ||
       options1.WaveLaneCountMax < options1.WaveLaneCountMin)) {
    return Fail("probe D3D12_OPTIONS1", "invalid wave lane range");
  }
  result->supports_wave_ops = SUCCEEDED(options1_hr) && options1.WaveOps;

  D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9 = {};
  const HRESULT options9_hr = device->CheckFeatureSupport(
      D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));
  result->supports_typed_atomic64 =
      SUCCEEDED(options9_hr) && options9.AtomicInt64OnTypedResourceSupported;
  // UE also accepts Intel's vendor-extension emulation. DXMT's macOS Wine
  // runtime has no Intel Windows driver-extension provider, so that probe is
  // deterministically unavailable while the OR policy remains explicit.
  result->supports_emulated_atomic64 = false;

  // This integration workload models UE's normal (non-fully-bindless) SM6
  // gate. A failed gate is not a D3D12 failure: UE continues with SM5 when
  // feature level 11_0 remains available.
  result->uses_sm6 = SupportsUnrealSm6(*result, false);

  D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
  architecture.NodeIndex = 0;
  const HRESULT architecture_hr = device->CheckFeatureSupport(
      D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
  if (SUCCEEDED(architecture_hr) && !architecture.UMA &&
      architecture.CacheCoherentUMA)
    return Fail("probe D3D12_ARCHITECTURE", "non-UMA device reported CCUMA");
  result->unified_memory = SUCCEEDED(architecture_hr) && architecture.UMA;
  return true;
}

bool FindAdapter(IDXGIFactory4 *factory, ProbeResult *result) {
  ComPtr<IDXGIFactory6> factory6;
  if (FAILED(
          factory->QueryInterface(__uuidof(IDXGIFactory6),
                                  reinterpret_cast<void **>(factory6.put())))) {
    factory6.reset();
  }

  std::vector<ProbeResult> candidates;
  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIAdapter1> adapter;
    const HRESULT enum_hr =
        EnumerateAdapter(factory, factory6.get(), index, adapter.put());
    if (enum_hr == DXGI_ERROR_NOT_FOUND)
      break;
    if (!CheckHResult("adapter enumeration", enum_hr))
      return false;
    if (!adapter)
      return Fail("adapter enumeration", "successful call returned null");

    DXGI_ADAPTER_DESC1 description = {};
    if (!CheckHResult("adapter description", adapter->GetDesc1(&description)))
      return false;
    if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue;

    ComPtr<ID3D12Device> probe_device;
    const HRESULT create_hr = D3D12CreateDevice(
        adapter.get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
        reinterpret_cast<void **>(probe_device.put()));
    if (FAILED(create_hr))
      continue;
    if (!probe_device)
      return Fail("probe D3D12CreateDevice", "successful call returned null");
    if (probe_device->GetNodeCount() == 0)
      return Fail("probe node count", "device reported no GPU nodes");

    ProbeResult candidate = {};
    candidate.enumeration_index = index;
    candidate.used_gpu_preference = factory6.get() != nullptr;
    candidate.description = description;
    if (!ProbeDevice(probe_device.get(), &candidate))
      return false;

    // The temporary device is destroyed before the formal factory/device pass.
    probe_device.reset();
    candidates.push_back(candidate);
  }

  if (candidates.empty())
    return Fail("adapter selection", "no feature-level-11_0 adapter was found");

  const ProbeResult *best_discrete = nullptr;
  const ProbeResult *first_discrete = nullptr;
  for (const ProbeResult &candidate : candidates) {
    if (candidate.unified_memory)
      continue;
    if (!first_discrete)
      first_discrete = &candidate;
    if (!best_discrete || candidate.description.DedicatedVideoMemory >
                              best_discrete->description.DedicatedVideoMemory) {
      best_discrete = &candidate;
    }
  }
  *result = best_discrete    ? *best_discrete
            : first_discrete ? *first_discrete
                             : candidates.front();
  return true;
}

bool RecreateSelectedAdapter(const ProbeResult &probe,
                             ComPtr<IDXGIFactory4> *factory,
                             ComPtr<IDXGIAdapter1> *adapter) {
  if (!CheckHResult(
          "formal CreateDXGIFactory2",
          CreateDXGIFactory2(0, __uuidof(IDXGIFactory4),
                             reinterpret_cast<void **>(factory->put()))))
    return false;

  ComPtr<IDXGIFactory6> factory6;
  if (probe.used_gpu_preference) {
    if (!CheckHResult("formal IDXGIFactory6",
                      (*factory)->QueryInterface(
                          __uuidof(IDXGIFactory6),
                          reinterpret_cast<void **>(factory6.put()))))
      return false;
  }

  const HRESULT enum_hr = EnumerateAdapter(
      factory->get(), factory6.get(), probe.enumeration_index, adapter->put());
  if (!CheckHResult("formal adapter enumeration", enum_hr))
    return false;
  if (!*adapter)
    return Fail("formal adapter enumeration", "successful call returned null");

  DXGI_ADAPTER_DESC1 description = {};
  if (!CheckHResult("formal adapter description",
                    (*adapter)->GetDesc1(&description)))
    return false;
  // UE carries the preference and enumeration index into the formal pass. It
  // does not require the factory-local LUID value to round-trip.
  if (description.VendorId != probe.description.VendorId ||
      description.DeviceId != probe.description.DeviceId ||
      description.SubSysId != probe.description.SubSysId ||
      description.Revision != probe.description.Revision) {
    return Fail("formal adapter identity",
                "adapter index resolved to different hardware");
  }
  return true;
}

class MemoryReservation {
public:
  ~MemoryReservation() {
    if (changed_) {
      const HRESULT hr = adapter_->SetVideoMemoryReservation(
          0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, original_reservation_);
      if (FAILED(hr)) {
        std::fprintf(stderr,
                     "ue_d3d12_initialization: best-effort reservation "
                     "restore failed with HRESULT 0x%08lx\n",
                     static_cast<unsigned long>(hr));
      }
    }
  }

  bool Initialize(IDXGIAdapter1 *adapter) {
    if (!CheckHResult(
            "IDXGIAdapter3",
            adapter->QueryInterface(__uuidof(IDXGIAdapter3),
                                    reinterpret_cast<void **>(adapter_.put()))))
      return false;

    DXGI_QUERY_VIDEO_MEMORY_INFO local = {};
    if (!CheckHResult("local video memory budget",
                      adapter_->QueryVideoMemoryInfo(
                          0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)))
      return false;
    if (local.Budget == 0)
      return Fail("local video memory budget", "adapter reported zero budget");
    original_reservation_ = local.CurrentReservation;

    DXGI_QUERY_VIDEO_MEMORY_INFO non_local = {};
    if (!CheckHResult("non-local video memory budget",
                      adapter_->QueryVideoMemoryInfo(
                          0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &non_local)))
      return false;
    const UINT64 target_reservation =
        std::min(local.AvailableForReservation, local.Budget * 9 / 10);
    if (!CheckHResult(
            "set video memory reservation",
            adapter_->SetVideoMemoryReservation(
                0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, target_reservation)))
      return false;
    changed_ = true;

    DXGI_QUERY_VIDEO_MEMORY_INFO updated = {};
    if (!CheckHResult("verify video memory reservation",
                      adapter_->QueryVideoMemoryInfo(
                          0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &updated)))
      return false;
    if (updated.CurrentReservation != target_reservation)
      return Fail("verify video memory reservation",
                  "reservation did not round-trip");
    return true;
  }

  bool Restore() {
    if (!changed_)
      return true;
    if (!CheckHResult(
            "restore video memory reservation",
            adapter_->SetVideoMemoryReservation(
                0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, original_reservation_)))
      return false;

    DXGI_QUERY_VIDEO_MEMORY_INFO restored = {};
    if (!CheckHResult("verify restored video memory reservation",
                      adapter_->QueryVideoMemoryInfo(
                          0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &restored)))
      return false;
    if (restored.CurrentReservation != original_reservation_)
      return Fail("verify restored video memory reservation",
                  "reservation did not return to its original value");
    changed_ = false;
    return true;
  }

private:
  ComPtr<IDXGIAdapter3> adapter_;
  UINT64 original_reservation_ = 0;
  bool changed_ = false;
};

struct QueueState {
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Fence> fence;
  UINT64 fence_value = 0;
};

bool CreateQueueState(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type,
                      const char *stage, QueueState *state) {
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = type;
  queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  queue_desc.NodeMask = 1;
  if (!CheckHResult(stage, device->CreateCommandQueue(
                               &queue_desc, __uuidof(ID3D12CommandQueue),
                               reinterpret_cast<void **>(state->queue.put()))))
    return false;
  if (!state->queue)
    return Fail(stage, "successful queue creation returned null");
  return CheckHResult(
      stage,
      device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                          reinterpret_cast<void **>(state->fence.put())));
}

bool WaitForCurrentFence(QueueState *state, HANDLE event, const char *stage) {
  if (state->fence_value == 0 ||
      state->fence->GetCompletedValue() >= state->fence_value)
    return true;
  if (!CheckHResult(
          stage, state->fence->SetEventOnCompletion(state->fence_value, event)))
    return false;
  const DWORD wait_result = WaitForSingleObject(event, 30000);
  if (wait_result != WAIT_OBJECT_0) {
    std::fprintf(stderr,
                 "ue_d3d12_initialization: %s wait failed with result %lu\n",
                 stage, static_cast<unsigned long>(wait_result));
    return false;
  }
  return true;
}

bool SignalAndWait(QueueState *state, HANDLE event, const char *stage) {
  const UINT64 fence_value = state->fence_value + 1;
  if (!CheckHResult(stage,
                    state->queue->Signal(state->fence.get(), fence_value)))
    return false;
  state->fence_value = fence_value;
  return WaitForCurrentFence(state, event, stage);
}

class SubmissionDrain {
public:
  SubmissionDrain(QueueState *copy, QueueState *compute, QueueState *direct,
                  HANDLE event)
      : states_{copy, compute, direct}, event_(event) {}

  ~SubmissionDrain() {
    if (active_)
      Drain();
  }

  bool Drain() {
    bool succeeded = true;
    for (QueueState *state : states_) {
      if (!WaitForCurrentFence(state, event_, "error-path queue drain"))
        succeeded = false;
    }
    active_ = false;
    return succeeded;
  }

  void Dismiss() { active_ = false; }

private:
  std::array<QueueState *, 3> states_;
  HANDLE event_ = nullptr;
  bool active_ = true;
};

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device *device, UINT64 size,
                                    D3D12_HEAP_TYPE heap_type,
                                    D3D12_RESOURCE_FLAGS flags,
                                    D3D12_RESOURCE_STATES state) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = heap_type;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;

  ComPtr<ID3D12Resource> resource;
  const HRESULT hr = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(resource.put()));
  return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>();
}

ComPtr<ID3D12Resource> CreateTexture2D(ID3D12Device *device, UINT64 width,
                                       UINT height, DXGI_FORMAT format,
                                       D3D12_RESOURCE_FLAGS flags,
                                       D3D12_RESOURCE_STATES state) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = flags;

  ComPtr<ID3D12Resource> resource;
  const HRESULT hr = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(resource.put()));
  return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>();
}

D3D12_CPU_DESCRIPTOR_HANDLE
DescriptorHandle(ID3D12Device *device, ID3D12DescriptorHeap *heap, UINT index) {
  D3D12_CPU_DESCRIPTOR_HANDLE handle =
      heap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(index) *
                device->GetDescriptorHandleIncrementSize(heap->GetDesc().Type);
  return handle;
}

void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  list->ResourceBarrier(1, &barrier);
}

bool CreateDescriptorHeap(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                          UINT count, bool shader_visible, const char *stage,
                          ComPtr<ID3D12DescriptorHeap> *heap) {
  if (device->GetDescriptorHandleIncrementSize(type) == 0)
    return Fail(stage, "descriptor increment size was zero");
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = type;
  desc.NumDescriptors = count;
  desc.Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                              : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  return CheckHResult(stage, device->CreateDescriptorHeap(
                                 &desc, __uuidof(ID3D12DescriptorHeap),
                                 reinterpret_cast<void **>(heap->put())));
}

bool DiscoverFormalCapabilities(ID3D12Device *device) {
  ComPtr<ID3D12Device1> device1;
  ComPtr<ID3D12Device2> device2;
  if (!CheckHResult(
          "formal ID3D12Device1",
          device->QueryInterface(__uuidof(ID3D12Device1),
                                 reinterpret_cast<void **>(device1.put()))))
    return false;
  if (!CheckHResult(
          "formal ID3D12Device2",
          device->QueryInterface(__uuidof(ID3D12Device2),
                                 reinterpret_cast<void **>(device2.put()))))
    return false;

  D3D12_FEATURE_DATA_ROOT_SIGNATURE root_signature = {};
  root_signature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
  HRESULT root_signature_hr = device->CheckFeatureSupport(
      D3D12_FEATURE_ROOT_SIGNATURE, &root_signature, sizeof(root_signature));
  if (FAILED(root_signature_hr)) {
    root_signature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    root_signature_hr = device->CheckFeatureSupport(
        D3D12_FEATURE_ROOT_SIGNATURE, &root_signature, sizeof(root_signature));
  }
  if (!CheckHResult("root signature feature query", root_signature_hr))
    return false;

  constexpr std::array formats = {
      DXGI_FORMAT_R8G8B8A8_UNORM,     DXGI_FORMAT_B8G8R8A8_UNORM,
      DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT,
      DXGI_FORMAT_R32_UINT,
  };
  for (const DXGI_FORMAT format : formats) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    if (!CheckHResult("format support sweep",
                      device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                                  &support, sizeof(support))))
      return false;
    if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D))
      return Fail("format support sweep", "required Texture2D format missing");
  }

  for (const UINT sample_count : {1u, 2u, 4u, 8u}) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaa = {};
    msaa.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msaa.SampleCount = sample_count;
    if (!CheckHResult(
            "MSAA capability sweep",
            device->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaa, sizeof(msaa))))
      return false;
    if (sample_count == 1 && msaa.NumQualityLevels == 0)
      return Fail("MSAA capability sweep", "single-sample target missing");
  }
  return true;
}

bool CreateRootAndCommandSignatures(
    ID3D12Device *device, ComPtr<ID3D12RootSignature> *root_signature,
    std::array<ComPtr<ID3D12CommandSignature>, 3> *command_signatures) {
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  if (!CheckHResult("serialize root signature",
                    D3D12SerializeRootSignature(&root_desc,
                                                D3D_ROOT_SIGNATURE_VERSION_1_0,
                                                blob.put(), error.put())))
    return false;
  if (!CheckHResult("create root signature",
                    device->CreateRootSignature(
                        0, blob->GetBufferPointer(), blob->GetBufferSize(),
                        __uuidof(ID3D12RootSignature),
                        reinterpret_cast<void **>(root_signature->put()))))
    return false;

  constexpr std::array argument_types = {
      D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,
      D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,
      D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,
  };
  constexpr std::array<UINT, 3> strides = {
      sizeof(D3D12_DRAW_ARGUMENTS),
      sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
      sizeof(D3D12_DISPATCH_ARGUMENTS),
  };
  for (std::size_t index = 0; index < argument_types.size(); ++index) {
    D3D12_INDIRECT_ARGUMENT_DESC argument = {};
    argument.Type = argument_types[index];
    D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
    signature_desc.ByteStride = strides[index];
    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument;
    if (!CheckHResult(
            "create command signature",
            device->CreateCommandSignature(
                &signature_desc, nullptr, __uuidof(ID3D12CommandSignature),
                reinterpret_cast<void **>((*command_signatures)[index].put()))))
      return false;
  }
  return true;
}

bool ValidateBufferReadback(ID3D12Resource *readback,
                            const std::array<std::uint32_t, 64> &expected) {
  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, sizeof(expected)};
  if (!CheckHResult("buffer readback map",
                    readback->Map(0, &read_range, &mapped)))
    return false;
  const bool matches =
      std::memcmp(mapped, expected.data(), sizeof(expected)) == 0;
  D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
  return matches ? true : Fail("buffer readback", "copied bytes did not match");
}

bool ValidateTextureReadback(ID3D12Resource *readback,
                             const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &layout,
                             const std::array<float, 4> &clear_color,
                             UINT64 total_size) {
  void *mapped = nullptr;
  D3D12_RANGE read_range = {0, static_cast<SIZE_T>(total_size)};
  if (!CheckHResult("texture readback map",
                    readback->Map(0, &read_range, &mapped)))
    return false;

  std::array<int, 4> expected = {};
  for (std::size_t channel = 0; channel < expected.size(); ++channel)
    expected[channel] = static_cast<int>(clear_color[channel] * 255.0f + 0.5f);

  bool matches = true;
  const auto *base = static_cast<const std::uint8_t *>(mapped) + layout.Offset;
  for (UINT y = 0; y < kTextureHeight && matches; ++y) {
    const auto *row =
        base + static_cast<std::size_t>(y) * layout.Footprint.RowPitch;
    for (UINT x = 0; x < kTextureWidth && matches; ++x) {
      const auto *pixel = row + x * 4;
      for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        const int difference =
            static_cast<int>(pixel[channel]) - expected[channel];
        if (difference < -1 || difference > 1) {
          matches = false;
          break;
        }
      }
    }
  }
  D3D12_RANGE written_range = {0, 0};
  readback->Unmap(0, &written_range);
  return matches ? true
                 : Fail("texture readback", "cleared color did not match");
}

class UniqueEvent {
public:
  ~UniqueEvent() {
    if (handle_)
      CloseHandle(handle_);
  }

  bool Initialize() {
    handle_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return handle_ != nullptr;
  }

  HANDLE get() const { return handle_; }

private:
  HANDLE handle_ = nullptr;
};

struct BootstrapState {
  // Declared first so the event outlives every fence and queue member.
  UniqueEvent fence_event;
  QueueState direct;
  QueueState compute;
  QueueState copy;
  ComPtr<ID3D12CommandAllocator> direct_allocator;
  ComPtr<ID3D12GraphicsCommandList> direct_list;
  ComPtr<ID3D12DescriptorHeap> resource_heap;
  ComPtr<ID3D12DescriptorHeap> sampler_heap;
  ComPtr<ID3D12DescriptorHeap> offline_resource_heap;
  ComPtr<ID3D12DescriptorHeap> offline_sampler_heap;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  ComPtr<ID3D12DescriptorHeap> dsv_heap;
};

bool BootstrapDevice(ID3D12Device *device, BootstrapState *state) {
  if (!CreateQueueState(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                        "create direct queue and fence", &state->direct) ||
      !CreateQueueState(device, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                        "create compute queue and fence", &state->compute) ||
      !CreateQueueState(device, D3D12_COMMAND_LIST_TYPE_COPY,
                        "create copy queue and fence", &state->copy))
    return false;

  if (!state->fence_event.Initialize())
    return CheckWin32("CreateEventW", FALSE);

  if (!CreateDescriptorHeap(
          device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kViewDescriptorCount,
          true, "create shader-visible resource heap", &state->resource_heap) ||
      !CreateDescriptorHeap(
          device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerDescriptorCount,
          true, "create shader-visible sampler heap", &state->sampler_heap) ||
      !CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                            kOfflineViewDescriptorCount, false,
                            "create CPU-only resource heap",
                            &state->offline_resource_heap) ||
      !CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                            kOfflineSamplerDescriptorCount, false,
                            "create CPU-only sampler heap",
                            &state->offline_sampler_heap) ||
      !CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                            kOfflineRenderTargetDescriptorCount, false,
                            "create RTV heap", &state->rtv_heap) ||
      !CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                            kOfflineRenderTargetDescriptorCount, false,
                            "create DSV heap", &state->dsv_heap))
    return false;

  ComPtr<ID3D12RootSignature> root_signature;
  std::array<ComPtr<ID3D12CommandSignature>, 3> command_signatures;
  if (!CreateRootAndCommandSignatures(device, &root_signature,
                                      &command_signatures))
    return false;

  std::array<std::uint32_t, 64> expected = {};
  for (std::size_t index = 0; index < expected.size(); ++index)
    expected[index] = 0x10203040u + static_cast<std::uint32_t>(index);

  ComPtr<ID3D12Resource> upload =
      CreateBuffer(device, 4096, D3D12_HEAP_TYPE_UPLOAD,
                   D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  ComPtr<ID3D12Resource> gpu_buffer =
      CreateBuffer(device, 4096, D3D12_HEAP_TYPE_DEFAULT,
                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12Resource> buffer_readback =
      CreateBuffer(device, 4096, D3D12_HEAP_TYPE_READBACK,
                   D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  if (!upload || !gpu_buffer || !buffer_readback)
    return Fail("bootstrap buffers", "resource creation returned null");

  void *upload_data = nullptr;
  D3D12_RANGE no_read = {0, 0};
  if (!CheckHResult("upload buffer map",
                    upload->Map(0, &no_read, &upload_data)))
    return false;
  std::memcpy(upload_data, expected.data(), sizeof(expected));
  D3D12_RANGE upload_written = {0, sizeof(expected)};
  upload->Unmap(0, &upload_written);

  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
  cbv.BufferLocation = upload->GetGPUVirtualAddress();
  cbv.SizeInBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
  device->CreateConstantBufferView(
      &cbv, DescriptorHandle(device, state->resource_heap.get(), 0));
  device->CreateConstantBufferView(
      &cbv, DescriptorHandle(device, state->offline_resource_heap.get(), 0));
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = static_cast<UINT>(expected.size());
  device->CreateShaderResourceView(
      gpu_buffer.get(), &srv,
      DescriptorHandle(device, state->resource_heap.get(), 1));
  device->CreateShaderResourceView(
      gpu_buffer.get(), &srv,
      DescriptorHandle(device, state->offline_resource_heap.get(), 1));
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = static_cast<UINT>(expected.size());
  device->CreateUnorderedAccessView(
      gpu_buffer.get(), nullptr, &uav,
      DescriptorHandle(device, state->resource_heap.get(), 2));
  device->CreateUnorderedAccessView(
      gpu_buffer.get(), nullptr, &uav,
      DescriptorHandle(device, state->offline_resource_heap.get(), 2));
  D3D12_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  device->CreateSampler(
      &sampler, state->sampler_heap->GetCPUDescriptorHandleForHeapStart());
  device->CreateSampler(
      &sampler,
      state->offline_sampler_heap->GetCPUDescriptorHandleForHeapStart());

  ComPtr<ID3D12CommandAllocator> copy_allocator;
  ComPtr<ID3D12GraphicsCommandList> copy_list;
  if (!CheckHResult("create copy allocator",
                    device->CreateCommandAllocator(
                        D3D12_COMMAND_LIST_TYPE_COPY,
                        __uuidof(ID3D12CommandAllocator),
                        reinterpret_cast<void **>(copy_allocator.put()))) ||
      !CheckHResult("create copy command list",
                    device->CreateCommandList(
                        0, D3D12_COMMAND_LIST_TYPE_COPY, copy_allocator.get(),
                        nullptr, __uuidof(ID3D12GraphicsCommandList),
                        reinterpret_cast<void **>(copy_list.put()))))
    return false;
  copy_list->CopyBufferRegion(gpu_buffer.get(), 0, upload.get(), 0,
                              sizeof(expected));
  if (!CheckHResult("close copy command list", copy_list->Close()))
    return false;

  ComPtr<ID3D12CommandAllocator> compute_allocator;
  ComPtr<ID3D12GraphicsCommandList> compute_list;
  if (!CheckHResult("create compute allocator",
                    device->CreateCommandAllocator(
                        D3D12_COMMAND_LIST_TYPE_COMPUTE,
                        __uuidof(ID3D12CommandAllocator),
                        reinterpret_cast<void **>(compute_allocator.put()))) ||
      !CheckHResult("create compute command list",
                    device->CreateCommandList(
                        0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                        compute_allocator.get(), nullptr,
                        __uuidof(ID3D12GraphicsCommandList),
                        reinterpret_cast<void **>(compute_list.put()))) ||
      !CheckHResult("close compute command list", compute_list->Close()))
    return false;

  if (!CheckHResult(
          "create direct allocator",
          device->CreateCommandAllocator(
              D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
              reinterpret_cast<void **>(state->direct_allocator.put()))) ||
      !CheckHResult("create direct command list",
                    device->CreateCommandList(
                        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                        state->direct_allocator.get(), nullptr,
                        __uuidof(ID3D12GraphicsCommandList),
                        reinterpret_cast<void **>(state->direct_list.put()))))
    return false;

  ComPtr<ID3D12Resource> color = CreateTexture2D(
      device, kTextureWidth, kTextureHeight, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  ComPtr<ID3D12Resource> depth = CreateTexture2D(
      device, kTextureWidth, kTextureHeight, DXGI_FORMAT_D32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
      D3D12_RESOURCE_STATE_DEPTH_WRITE);
  if (!color || !depth)
    return Fail("bootstrap textures", "resource creation returned null");

  const auto color_rtv = DescriptorHandle(device, state->rtv_heap.get(), 0);
  const auto depth_dsv = DescriptorHandle(device, state->dsv_heap.get(), 0);
  device->CreateRenderTargetView(color.get(), nullptr, color_rtv);
  device->CreateDepthStencilView(depth.get(), nullptr, depth_dsv);

  const D3D12_RESOURCE_DESC color_desc = color->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT color_layout = {};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 texture_readback_size = 0;
  device->GetCopyableFootprints(&color_desc, 0, 1, 0, &color_layout, &row_count,
                                &row_size, &texture_readback_size);
  ComPtr<ID3D12Resource> texture_readback =
      CreateBuffer(device, texture_readback_size, D3D12_HEAP_TYPE_READBACK,
                   D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  if (!texture_readback)
    return Fail("texture readback", "resource creation returned null");

  ID3D12DescriptorHeap *shader_heaps[] = {state->resource_heap.get(),
                                          state->sampler_heap.get()};
  state->direct_list->SetDescriptorHeaps(2, shader_heaps);
  state->direct_list->SetGraphicsRootSignature(root_signature.get());
  Transition(state->direct_list.get(), gpu_buffer.get(),
             D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
  state->direct_list->CopyBufferRegion(buffer_readback.get(), 0,
                                       gpu_buffer.get(), 0, sizeof(expected));

  constexpr std::array<float, 4> clear_color = {0.2f, 0.4f, 0.6f, 1.0f};
  state->direct_list->ClearRenderTargetView(color_rtv, clear_color.data(), 0,
                                            nullptr);
  state->direct_list->ClearDepthStencilView(depth_dsv, D3D12_CLEAR_FLAG_DEPTH,
                                            0.375f, 0, 0, nullptr);
  Transition(state->direct_list.get(), color.get(),
             D3D12_RESOURCE_STATE_RENDER_TARGET,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION texture_source = {};
  texture_source.pResource = color.get();
  texture_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION texture_destination = {};
  texture_destination.pResource = texture_readback.get();
  texture_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  texture_destination.PlacedFootprint = color_layout;
  state->direct_list->CopyTextureRegion(&texture_destination, 0, 0, 0,
                                        &texture_source, nullptr);
  if (!CheckHResult("close direct command list", state->direct_list->Close()))
    return false;

  SubmissionDrain drain(&state->copy, &state->compute, &state->direct,
                        state->fence_event.get());
  ID3D12CommandList *copy_lists[] = {copy_list.get()};
  state->copy.queue->ExecuteCommandLists(1, copy_lists);
  const UINT64 copy_fence_value = state->copy.fence_value + 1;
  if (!CheckHResult(
          "signal copy queue",
          state->copy.queue->Signal(state->copy.fence.get(), copy_fence_value)))
    return false;
  state->copy.fence_value = copy_fence_value;

  if (!CheckHResult("compute waits for copy",
                    state->compute.queue->Wait(state->copy.fence.get(),
                                               state->copy.fence_value)))
    return false;
  ID3D12CommandList *compute_lists[] = {compute_list.get()};
  state->compute.queue->ExecuteCommandLists(1, compute_lists);
  const UINT64 compute_fence_value = state->compute.fence_value + 1;
  if (!CheckHResult("signal compute queue",
                    state->compute.queue->Signal(state->compute.fence.get(),
                                                 compute_fence_value)))
    return false;
  state->compute.fence_value = compute_fence_value;

  if (!CheckHResult("direct waits for compute",
                    state->direct.queue->Wait(state->compute.fence.get(),
                                              state->compute.fence_value)))
    return false;
  ID3D12CommandList *direct_lists[] = {state->direct_list.get()};
  state->direct.queue->ExecuteCommandLists(1, direct_lists);
  if (!SignalAndWait(&state->direct, state->fence_event.get(),
                     "bootstrap direct queue completion"))
    return false;
  drain.Dismiss();

  if (!ValidateBufferReadback(buffer_readback.get(), expected) ||
      !ValidateTextureReadback(texture_readback.get(), color_layout,
                               clear_color, texture_readback_size))
    return false;
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
        CreateWindowExW(0, kClassName, L"DXMT Unreal D3D12 Initialization",
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
      L"DXMTUnrealD3D12InitializationWindow";
  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  bool class_registered_ = false;
};

bool ClearAndPresentBackBuffer(ID3D12Device *device, BootstrapState *state,
                               IDXGISwapChain3 *swap_chain, UINT expected_width,
                               UINT expected_height,
                               const std::array<float, 4> &clear_color,
                               const char *stage) {
  ComPtr<ID3D12Resource> back_buffer;
  const UINT back_buffer_index = swap_chain->GetCurrentBackBufferIndex();
  if (!CheckHResult(stage, swap_chain->GetBuffer(
                               back_buffer_index, __uuidof(ID3D12Resource),
                               reinterpret_cast<void **>(back_buffer.put()))))
    return false;
  const D3D12_RESOURCE_DESC back_buffer_desc = back_buffer->GetDesc();
  if (back_buffer_desc.Width != expected_width ||
      back_buffer_desc.Height != expected_height) {
    return Fail(stage, "back-buffer dimensions did not match");
  }

  ComPtr<ID3D12DescriptorHeap> viewport_rtv_heap;
  if (!CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false,
                            stage, &viewport_rtv_heap))
    return false;
  const auto rtv = viewport_rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(back_buffer.get(), nullptr, rtv);
  if (!CheckHResult(stage, state->direct_allocator->Reset()) ||
      !CheckHResult(stage, state->direct_list->Reset(
                               state->direct_allocator.get(), nullptr)))
    return false;
  Transition(state->direct_list.get(), back_buffer.get(),
             D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  state->direct_list->ClearRenderTargetView(rtv, clear_color.data(), 0,
                                            nullptr);
  Transition(state->direct_list.get(), back_buffer.get(),
             D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  if (!CheckHResult(stage, state->direct_list->Close()))
    return false;

  ID3D12CommandList *lists[] = {state->direct_list.get()};
  state->direct.queue->ExecuteCommandLists(1, lists);
  const UINT64 fence_value = state->direct.fence_value + 1;
  if (!CheckHResult(stage, state->direct.queue->Signal(
                               state->direct.fence.get(), fence_value)))
    return false;
  state->direct.fence_value = fence_value;
  const HRESULT present_hr = swap_chain->Present(0, 0);
  const bool queue_completed =
      WaitForCurrentFence(&state->direct, state->fence_event.get(), stage);
  if (!CheckHResult(stage, present_hr) || !queue_completed)
    return false;

  if (!CheckHResult(stage, state->direct_allocator->Reset()) ||
      !CheckHResult(stage, state->direct_list->Reset(
                               state->direct_allocator.get(), nullptr)) ||
      !CheckHResult(stage, state->direct_list->Close()))
    return false;
  back_buffer.reset();
  viewport_rtv_heap.reset();
  return true;
}

bool BootstrapViewport(IDXGIFactory4 *factory, ID3D12Device *device,
                       BootstrapState *state) {
  constexpr UINT initial_width = 320;
  constexpr UINT initial_height = 180;
  constexpr UINT resized_width = 400;
  constexpr UINT resized_height = 240;

  HiddenInitializationWindow window;
  if (!window.Initialize())
    return false;

  DXGI_SWAP_CHAIN_DESC1 swap_desc = {};
  swap_desc.Width = initial_width;
  swap_desc.Height = initial_height;
  swap_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swap_desc.SampleDesc.Count = 1;
  swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_desc.BufferCount = 3;
  swap_desc.Scaling = DXGI_SCALING_STRETCH;
  swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
  DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc = {};
  fullscreen_desc.Windowed = TRUE;

  ComPtr<IDXGISwapChain1> swap_chain1;
  if (!CheckHResult("CreateSwapChainForHwnd",
                    factory->CreateSwapChainForHwnd(
                        state->direct.queue.get(), window.get(), &swap_desc,
                        &fullscreen_desc, nullptr, swap_chain1.put())))
    return false;
  ComPtr<IDXGISwapChain3> swap_chain;
  if (!CheckHResult("IDXGISwapChain3",
                    swap_chain1->QueryInterface(
                        __uuidof(IDXGISwapChain3),
                        reinterpret_cast<void **>(swap_chain.put()))))
    return false;
  if (!CheckHResult("MakeWindowAssociation",
                    factory->MakeWindowAssociation(window.get(),
                                                   DXGI_MWA_NO_WINDOW_CHANGES)))
    return false;

  constexpr std::array<float, 4> first_clear = {0.1f, 0.3f, 0.5f, 1.0f};
  if (!ClearAndPresentBackBuffer(device, state, swap_chain.get(), initial_width,
                                 initial_height, first_clear,
                                 "initial viewport clear and Present"))
    return false;

  if (!CheckHResult("ResizeBuffers",
                    swap_chain->ResizeBuffers(3, resized_width, resized_height,
                                              DXGI_FORMAT_B8G8R8A8_UNORM, 0)))
    return false;
  constexpr std::array<float, 4> second_clear = {0.7f, 0.4f, 0.2f, 1.0f};
  if (!ClearAndPresentBackBuffer(device, state, swap_chain.get(), resized_width,
                                 resized_height, second_clear,
                                 "resized viewport clear and Present"))
    return false;

  swap_chain.reset();
  swap_chain1.reset();
  return window.Destroy();
}

bool RunUnrealD3D12Initialization() {
  ComPtr<IDXGIFactory4> probe_factory;
  if (!CheckHResult(
          "probe CreateDXGIFactory1",
          CreateDXGIFactory1(__uuidof(IDXGIFactory4),
                             reinterpret_cast<void **>(probe_factory.put()))))
    return false;

  ProbeResult probe = {};
  if (!FindAdapter(probe_factory.get(), &probe))
    return false;
  std::fprintf(
      stdout,
      "ue_d3d12_initialization: selected %s (FL 0x%x, SM %u.%u, "
      "binding tier %u, wave ops %s, atomic64 %s)\n",
      probe.uses_sm6 ? "SM6" : "SM5",
      static_cast<unsigned>(probe.feature_level),
      static_cast<unsigned>(probe.shader_model >> 4),
      static_cast<unsigned>(probe.shader_model & 0xf),
      static_cast<unsigned>(probe.resource_binding_tier),
      probe.supports_wave_ops ? "yes" : "no",
      (probe.supports_typed_atomic64 || probe.supports_emulated_atomic64)
          ? "yes"
          : "no");
  probe_factory.reset();

  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter1> adapter;
  if (!RecreateSelectedAdapter(probe, &factory, &adapter))
    return false;

  ComPtr<ID3D12Device> device;
  if (!CheckHResult("formal D3D12CreateDevice",
                    D3D12CreateDevice(adapter.get(), probe.feature_level,
                                      __uuidof(ID3D12Device),
                                      reinterpret_cast<void **>(device.put()))))
    return false;
  if (!device || device->GetNodeCount() == 0)
    return Fail("formal D3D12CreateDevice", "invalid device or node count");
  if (!DiscoverFormalCapabilities(device.get()))
    return false;

  MemoryReservation reservation;
  if (!reservation.Initialize(adapter.get()))
    return false;

  BootstrapState bootstrap;
  if (!BootstrapDevice(device.get(), &bootstrap))
    return false;
  if (!BootstrapViewport(factory.get(), device.get(), &bootstrap))
    return false;
  if (!CheckHResult("final device status", device->GetDeviceRemovedReason()))
    return false;
  if (!reservation.Restore())
    return false;

  // Reverse teardown is explicit for the objects that own GPU submission.
  bootstrap.direct_list.reset();
  bootstrap.direct_allocator.reset();
  bootstrap.dsv_heap.reset();
  bootstrap.rtv_heap.reset();
  bootstrap.offline_sampler_heap.reset();
  bootstrap.offline_resource_heap.reset();
  bootstrap.sampler_heap.reset();
  bootstrap.resource_heap.reset();
  bootstrap.copy.fence.reset();
  bootstrap.copy.queue.reset();
  bootstrap.compute.fence.reset();
  bootstrap.compute.queue.reset();
  bootstrap.direct.fence.reset();
  bootstrap.direct.queue.reset();
  device.reset();
  adapter.reset();
  factory.reset();
  return true;
}

} // namespace

int main() { return RunUnrealD3D12Initialization() ? 0 : 1; }
