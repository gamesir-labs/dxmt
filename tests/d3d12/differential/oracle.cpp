#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using dxmt::test::ComPtr;

constexpr UINT kValueCount = 16;

bool Check(HRESULT result, std::string_view operation) {
  if (SUCCEEDED(result))
    return true;
  std::cerr << operation << " failed with HRESULT 0x" << std::hex
            << static_cast<std::uint32_t>(result) << std::dec << '\n';
  return false;
}

std::uint64_t Hash(const std::vector<std::uint32_t> &values) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const std::uint32_t value : values) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      hash ^= (value >> shift) & 0xffu;
      hash *= 1099511628211ull;
    }
  }
  return hash;
}

class OracleContext {
public:
  ~OracleContext() {
    if (fence_event_)
      CloseHandle(fence_event_);
  }

  bool Initialize(bool use_warp) {
    if (!Check(CreateDXGIFactory1(__uuidof(IDXGIFactory4),
                                  reinterpret_cast<void **>(factory_.put())),
               "CreateDXGIFactory1"))
      return false;

    if (use_warp && !Check(factory_->EnumWarpAdapter(
                               __uuidof(IDXGIAdapter1),
                               reinterpret_cast<void **>(adapter_.put())),
                           "EnumWarpAdapter"))
      return false;

    if (!Check(D3D12CreateDevice(adapter_.get(), D3D_FEATURE_LEVEL_11_0,
                                 __uuidof(ID3D12Device),
                                 reinterpret_cast<void **>(device_.put())),
               "D3D12CreateDevice"))
      return false;

    if (!adapter_) {
      const LUID luid = device_->GetAdapterLuid();
      if (!Check(factory_->EnumAdapterByLuid(
                     luid, __uuidof(IDXGIAdapter1),
                     reinterpret_cast<void **>(adapter_.put())),
                 "EnumAdapterByLuid"))
        return false;
    }
    DXGI_ADAPTER_DESC1 adapter_desc = {};
    if (!Check(adapter_->GetDesc1(&adapter_desc), "GetDesc1"))
      return false;
    vendor_id_ = adapter_desc.VendorId;
    device_id_ = adapter_desc.DeviceId;

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!Check(device_->CreateCommandQueue(
                   &queue_desc, __uuidof(ID3D12CommandQueue),
                   reinterpret_cast<void **>(queue_.put())),
               "CreateCommandQueue") ||
        !Check(device_->CreateCommandAllocator(
                   D3D12_COMMAND_LIST_TYPE_DIRECT,
                   __uuidof(ID3D12CommandAllocator),
                   reinterpret_cast<void **>(allocator_.put())),
               "CreateCommandAllocator") ||
        !Check(device_->CreateCommandList(
                   0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.get(), nullptr,
                   __uuidof(ID3D12GraphicsCommandList),
                   reinterpret_cast<void **>(list_.put())),
               "CreateCommandList") ||
        !Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                    __uuidof(ID3D12Fence),
                                    reinterpret_cast<void **>(fence_.put())),
               "CreateFence"))
      return false;

    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return fence_event_ != nullptr;
  }

  ComPtr<ID3D12Resource> CreateBuffer(UINT64 size, D3D12_HEAP_TYPE heap_type,
                                      D3D12_RESOURCE_FLAGS flags,
                                      D3D12_RESOURCE_STATES state) const {
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
    if (!Check(device_->CreateCommittedResource(
                   &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                   __uuidof(ID3D12Resource),
                   reinterpret_cast<void **>(resource.put())),
               "CreateCommittedResource(buffer)"))
      return {};
    return resource;
  }

  ComPtr<ID3D12Resource>
  CreateUpload(const std::array<std::uint32_t, kValueCount> &values) const {
    auto upload = CreateBuffer(sizeof(values), D3D12_HEAP_TYPE_UPLOAD,
                               D3D12_RESOURCE_FLAG_NONE,
                               D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!upload)
      return {};
    void *mapping = nullptr;
    const D3D12_RANGE no_read = {};
    if (!Check(upload->Map(0, &no_read, &mapping), "Map(upload)"))
      return {};
    std::memcpy(mapping, values.data(), sizeof(values));
    const D3D12_RANGE written = {0, sizeof(values)};
    upload->Unmap(0, &written);
    return upload;
  }

  bool ExecuteAndReset() {
    if (!Check(list_->Close(), "Close"))
      return false;
    ID3D12CommandList *lists[] = {list_.get()};
    queue_->ExecuteCommandLists(1, lists);
    const UINT64 value = ++fence_value_;
    if (!Check(queue_->Signal(fence_.get(), value), "Signal"))
      return false;
    if (fence_->GetCompletedValue() < value) {
      if (!Check(fence_->SetEventOnCompletion(value, fence_event_),
                 "SetEventOnCompletion") ||
          WaitForSingleObject(fence_event_, 30000) != WAIT_OBJECT_0)
        return false;
    }
    return Check(allocator_->Reset(), "Allocator Reset") &&
           Check(list_->Reset(allocator_.get(), nullptr), "CommandList Reset");
  }

  std::vector<std::uint32_t> Readback(ID3D12Resource *resource,
                                      UINT count) const {
    std::vector<std::uint32_t> values(count);
    void *mapping = nullptr;
    const D3D12_RANGE read = {0, count * sizeof(std::uint32_t)};
    if (!Check(resource->Map(0, &read, &mapping), "Map(readback)"))
      return {};
    std::memcpy(values.data(), mapping, count * sizeof(std::uint32_t));
    const D3D12_RANGE no_write = {};
    resource->Unmap(0, &no_write);
    return values;
  }

  ID3D12Device *device() const { return device_.get(); }
  ID3D12GraphicsCommandList *list() const { return list_.get(); }
  ID3D12CommandQueue *queue() const { return queue_.get(); }
  UINT vendor_id() const { return vendor_id_; }
  UINT device_id() const { return device_id_; }

private:
  ComPtr<IDXGIFactory4> factory_;
  ComPtr<IDXGIAdapter1> adapter_;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<ID3D12CommandAllocator> allocator_;
  ComPtr<ID3D12GraphicsCommandList> list_;
  ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  UINT64 fence_value_ = 0;
  UINT vendor_id_ = 0;
  UINT device_id_ = 0;
};

void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
}

std::uint32_t FloatBits(float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

bool ReadbackRgba8(OracleContext &context, ID3D12Resource *texture,
                   D3D12_RESOURCE_STATES state,
                   std::vector<std::uint32_t> *result,
                   std::string_view operation) {
  const D3D12_RESOURCE_DESC desc = texture->GetDesc();
  if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || desc.SampleDesc.Count != 1 ||
      desc.DepthOrArraySize != 1)
    return false;

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT64 total_size = 0;
  context.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr,
                                          nullptr, &total_size);
  auto readback = context.CreateBuffer(total_size, D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  if (!readback)
    return false;

  Transition(context.list(), texture, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION source = {};
  source.pResource = texture;
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION destination = {};
  destination.pResource = readback.get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = footprint;
  context.list()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  if (!context.ExecuteAndReset())
    return false;

  void *mapping = nullptr;
  const D3D12_RANGE read = {0, static_cast<SIZE_T>(total_size)};
  if (!Check(readback->Map(0, &read, &mapping), operation))
    return false;
  result->resize(static_cast<std::size_t>(desc.Width) * desc.Height);
  for (UINT row = 0; row < desc.Height; ++row) {
    std::memcpy(result->data() + row * desc.Width,
                static_cast<const std::uint8_t *>(mapping) + footprint.Offset +
                    row * footprint.Footprint.RowPitch,
                desc.Width * sizeof(std::uint32_t));
  }
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
  return true;
}

bool CompileShader(std::string_view source, const char *target,
                   std::string_view operation, ComPtr<ID3DBlob> *shader) {
  ComPtr<ID3DBlob> diagnostics;
  if (Check(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
                       "main", target,
                       D3DCOMPILE_ENABLE_STRICTNESS |
                           D3DCOMPILE_OPTIMIZATION_LEVEL3,
                       0, shader->put(), diagnostics.put()),
            operation))
    return true;
  if (diagnostics)
    std::cerr.write(static_cast<const char *>(diagnostics->GetBufferPointer()),
                    diagnostics->GetBufferSize());
  return false;
}

bool RunCopyCase(OracleContext &context,
                 const std::array<std::uint32_t, kValueCount> &input,
                 std::vector<std::uint32_t> *result) {
  auto upload = context.CreateUpload(input);
  auto gpu = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_DEFAULT,
                                  D3D12_RESOURCE_FLAG_NONE,
                                  D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  if (!upload || !gpu || !readback)
    return false;
  context.list()->CopyBufferRegion(gpu.get(), 0, upload.get(), 0,
                                   sizeof(input));
  Transition(context.list(), gpu.get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(readback.get(), 0, gpu.get(), 0,
                                   sizeof(input));
  if (!context.ExecuteAndReset())
    return false;
  *result = context.Readback(readback.get(), kValueCount);
  return result->size() == kValueCount;
}

bool RunOffsetCopyCase(OracleContext &context,
                       const std::array<std::uint32_t, kValueCount> &input,
                       std::vector<std::uint32_t> *result) {
  std::array<std::uint32_t, kValueCount> initial = {};
  initial.fill(0xdeadbeefu);
  auto initial_upload = context.CreateUpload(initial);
  auto input_upload = context.CreateUpload(input);
  auto readback = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  if (!initial_upload || !input_upload || !readback)
    return false;
  context.list()->CopyBufferRegion(readback.get(), 0, initial_upload.get(), 0,
                                   sizeof(initial));
  constexpr UINT first = 3;
  constexpr UINT count = 9;
  context.list()->CopyBufferRegion(
      readback.get(), first * sizeof(std::uint32_t), input_upload.get(),
      first * sizeof(std::uint32_t), count * sizeof(std::uint32_t));
  if (!context.ExecuteAndReset())
    return false;
  *result = context.Readback(readback.get(), kValueCount);
  return result->size() == kValueCount;
}

bool RunBarrierChainCase(OracleContext &context,
                         const std::array<std::uint32_t, kValueCount> &input,
                         std::vector<std::uint32_t> *result) {
  auto upload = context.CreateUpload(input);
  auto intermediate = context.CreateBuffer(
      sizeof(input), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto destination = context.CreateBuffer(
      sizeof(input), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  if (!upload || !intermediate || !destination || !readback)
    return false;
  context.list()->CopyBufferRegion(intermediate.get(), 0, upload.get(), 0,
                                   sizeof(input));
  Transition(context.list(), intermediate.get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(destination.get(), 0, intermediate.get(), 0,
                                   sizeof(input));
  Transition(context.list(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(readback.get(), 0, destination.get(), 0,
                                   sizeof(input));
  if (!context.ExecuteAndReset())
    return false;
  *result = context.Readback(readback.get(), kValueCount);
  return result->size() == kValueCount;
}

bool RunCrossQueueFenceCase(OracleContext &context,
                            const std::array<std::uint32_t, kValueCount> &input,
                            std::vector<std::uint32_t> *result) {
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
  ComPtr<ID3D12CommandQueue> copy_queue;
  ComPtr<ID3D12CommandAllocator> copy_allocator;
  ComPtr<ID3D12GraphicsCommandList> copy_list;
  ComPtr<ID3D12CommandAllocator> direct_allocator;
  ComPtr<ID3D12GraphicsCommandList> direct_list;
  ComPtr<ID3D12Fence> producer_gate;
  ComPtr<ID3D12Fence> copy_fence;
  ComPtr<ID3D12Fence> direct_fence;
  if (!Check(context.device()->CreateCommandQueue(
                 &queue_desc, IID_PPV_ARGS(copy_queue.put())),
             "CreateCommandQueue(cross queue copy)") ||
      !Check(
          context.device()->CreateCommandAllocator(
              D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(copy_allocator.put())),
          "CreateCommandAllocator(cross queue copy)") ||
      !Check(context.device()->CreateCommandList(
                 0, D3D12_COMMAND_LIST_TYPE_COPY, copy_allocator.get(), nullptr,
                 IID_PPV_ARGS(copy_list.put())),
             "CreateCommandList(cross queue copy)") ||
      !Check(context.device()->CreateCommandAllocator(
                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                 IID_PPV_ARGS(direct_allocator.put())),
             "CreateCommandAllocator(cross queue direct)") ||
      !Check(context.device()->CreateCommandList(
                 0, D3D12_COMMAND_LIST_TYPE_DIRECT, direct_allocator.get(),
                 nullptr, IID_PPV_ARGS(direct_list.put())),
             "CreateCommandList(cross queue direct)") ||
      !Check(context.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(producer_gate.put())),
             "CreateFence(cross queue producer gate)") ||
      !Check(context.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(copy_fence.put())),
             "CreateFence(cross queue copy completion)") ||
      !Check(context.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(direct_fence.put())),
             "CreateFence(cross queue direct completion)"))
    return false;

  auto upload = context.CreateUpload(input);
  auto gpu = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_DEFAULT,
                                  D3D12_RESOURCE_FLAG_NONE,
                                  D3D12_RESOURCE_STATE_COMMON);
  auto readback = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  if (!upload || !gpu || !readback)
    return false;

  copy_list->CopyBufferRegion(gpu.get(), 0, upload.get(), 0, sizeof(input));
  direct_list->CopyBufferRegion(readback.get(), 0, gpu.get(), 0,
                                sizeof(input));
  if (!Check(copy_list->Close(), "Close(cross queue copy list)") ||
      !Check(direct_list->Close(), "Close(cross queue direct list)"))
    return false;

  // Keep the producer blocked until the consumer and its queue wait have both
  // been submitted. A broken no-op Queue::Wait then completes the consumer
  // while the producer is still gated, instead of winning a timing race.
  if (!Check(copy_queue->Wait(producer_gate.get(), 1),
             "Wait(cross queue producer gate)"))
    return false;
  ID3D12CommandList *copy_lists[] = {copy_list.get()};
  copy_queue->ExecuteCommandLists(1, copy_lists);
  constexpr UINT64 copy_complete = 1;
  if (!Check(copy_queue->Signal(copy_fence.get(), copy_complete),
             "Signal(cross queue copy)") ||
      !Check(context.queue()->Wait(copy_fence.get(), copy_complete),
             "Wait(cross queue direct)"))
    return false;
  ID3D12CommandList *direct_lists[] = {direct_list.get()};
  context.queue()->ExecuteCommandLists(1, direct_lists);
  constexpr UINT64 direct_complete = 1;
  if (!Check(context.queue()->Signal(direct_fence.get(), direct_complete),
             "Signal(cross queue direct)"))
    return false;

  const ULONGLONG probe_deadline = GetTickCount64() + 200;
  while (direct_fence->GetCompletedValue() < direct_complete &&
         GetTickCount64() < probe_deadline)
    Sleep(1);
  const bool bypassed_wait =
      direct_fence->GetCompletedValue() >= direct_complete;

  if (!Check(producer_gate->Signal(1), "Signal(cross queue producer gate)"))
    return false;

  auto wait_for_fence = [](ID3D12Fence *fence, UINT64 value,
                           const char *label) {
    if (fence->GetCompletedValue() >= value)
      return true;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) {
      std::cerr << label << " CreateEvent failed\n";
      return false;
    }
    const bool scheduled = Check(fence->SetEventOnCompletion(value, event),
                                 label);
    const bool completed =
        scheduled && WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
    CloseHandle(event);
    if (!completed)
      std::cerr << label << " timed out\n";
    return completed;
  };
  if (!wait_for_fence(copy_fence.get(), copy_complete,
                      "WaitForFence(cross queue copy)") ||
      !wait_for_fence(direct_fence.get(), direct_complete,
                      "WaitForFence(cross queue direct)"))
    return false;
  if (bypassed_wait) {
    std::cerr << "cross queue direct work completed while producer was gated\n";
    return false;
  }

  *result = context.Readback(readback.get(), kValueCount);
  return result->size() == kValueCount;
}

enum class ComputePredication {
  None,
  NotEqualZeroExecute,
  NotEqualZeroSkip,
  EqualZeroExecute,
  EqualZeroSkip,
  DisableBeforeDispatch,
};

bool RunComputeCase(OracleContext &context,
                    const std::array<std::uint32_t, kValueCount> &input,
                    std::vector<std::uint32_t> *result,
                    ComputePredication predication = ComputePredication::None) {
  constexpr std::string_view source = R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);

    [numthreads(16, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      uint value = input.Load(id.x * 4);
      output.Store(id.x * 4, (value ^ 0xa5a5a5a5) + id.x * 17);
    }
  )";
  ComPtr<ID3DBlob> shader;
  ComPtr<ID3DBlob> diagnostics;
  if (!Check(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
                        "main", "cs_5_0",
                        D3DCOMPILE_ENABLE_STRICTNESS |
                            D3DCOMPILE_OPTIMIZATION_LEVEL3,
                        0, shader.put(), diagnostics.put()),
             "D3DCompile")) {
    if (diagnostics)
      std::cerr.write(
          static_cast<const char *>(diagnostics->GetBufferPointer()),
          diagnostics->GetBufferSize());
    return false;
  }

  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].Descriptor.ShaderRegister = 0;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  ComPtr<ID3DBlob> root_blob;
  if (!Check(D3D12SerializeRootSignature(&root_desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         root_blob.put(), diagnostics.put()),
             "D3D12SerializeRootSignature"))
    return false;
  ComPtr<ID3D12RootSignature> root;
  if (!Check(context.device()->CreateRootSignature(
                 0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                 __uuidof(ID3D12RootSignature),
                 reinterpret_cast<void **>(root.put())),
             "CreateRootSignature"))
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.get();
  pipeline_desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  ComPtr<ID3D12PipelineState> pipeline;
  if (!Check(context.device()->CreateComputePipelineState(
                 &pipeline_desc, __uuidof(ID3D12PipelineState),
                 reinterpret_cast<void **>(pipeline.put())),
             "CreateComputePipelineState"))
    return false;

  auto upload = context.CreateUpload(input);
  std::array<std::uint32_t, kValueCount> initial_values = {};
  for (UINT index = 0; index < initial_values.size(); ++index)
    initial_values[index] = 0xc001d00du ^ (index * 0x01010101u);
  auto initial_upload = context.CreateUpload(initial_values);
  auto output = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                     D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  std::array<std::uint32_t, kValueCount> predicate_values = {};
  const bool predicate_nonzero =
      predication == ComputePredication::NotEqualZeroExecute ||
      predication == ComputePredication::EqualZeroSkip;
  predicate_values[0] = predicate_nonzero ? 1u : 0u;
  const bool uses_predication = predication != ComputePredication::None;
  const bool uses_equal_zero =
      predication == ComputePredication::EqualZeroExecute ||
      predication == ComputePredication::EqualZeroSkip;
  const D3D12_PREDICATION_OP predication_op =
      uses_equal_zero ? D3D12_PREDICATION_OP_EQUAL_ZERO
                      : D3D12_PREDICATION_OP_NOT_EQUAL_ZERO;
  auto predicate = uses_predication ? context.CreateUpload(predicate_values)
                                    : ComPtr<ID3D12Resource>();
  if (!upload || !initial_upload || !output || !readback ||
      (uses_predication && !predicate))
    return false;
  context.list()->CopyBufferRegion(output.get(), 0, initial_upload.get(), 0,
                                   sizeof(initial_values));
  Transition(context.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  context.list()->SetPipelineState(pipeline.get());
  context.list()->SetComputeRootSignature(root.get());
  context.list()->SetComputeRootShaderResourceView(
      0, upload->GetGPUVirtualAddress());
  context.list()->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress());
  if (uses_predication)
    context.list()->SetPredication(predicate.get(), 0, predication_op);
  if (predication == ComputePredication::DisableBeforeDispatch)
    context.list()->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
  context.list()->Dispatch(1, 1, 1);
  if (predication == ComputePredication::NotEqualZeroExecute ||
      predication == ComputePredication::NotEqualZeroSkip ||
      predication == ComputePredication::EqualZeroExecute ||
      predication == ComputePredication::EqualZeroSkip)
    context.list()->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
  Transition(context.list(), output.get(),
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                   sizeof(input));
  if (!context.ExecuteAndReset())
    return false;
  *result = context.Readback(readback.get(), kValueCount);
  return result->size() == kValueCount;
}

bool RunRootConstantsCase(OracleContext &context,
                          std::vector<std::uint32_t> *result) {
  constexpr std::string_view source = R"(
    cbuffer Constants : register(b0) { uint4 values; };
    RWByteAddressBuffer output : register(u0);

    [numthreads(4, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      output.Store(id.x * 4, values[id.x] ^ (id.x * 0x11111111u));
    }
  )";
  ComPtr<ID3DBlob> shader;
  if (!CompileShader(source, "cs_5_0", "D3DCompile(root constants)", &shader))
    return false;

  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.ShaderRegister = 0;
  parameters[0].Constants.Num32BitValues = 4;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].Descriptor.ShaderRegister = 0;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  ComPtr<ID3DBlob> root_blob;
  ComPtr<ID3DBlob> diagnostics;
  if (!Check(D3D12SerializeRootSignature(&root_desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         root_blob.put(), diagnostics.put()),
             "D3D12SerializeRootSignature(root constants)"))
    return false;
  ComPtr<ID3D12RootSignature> root;
  if (!Check(context.device()->CreateRootSignature(
                 0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                 IID_PPV_ARGS(root.put())),
             "CreateRootSignature(root constants)"))
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.get();
  pipeline_desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  ComPtr<ID3D12PipelineState> pipeline;
  if (!Check(context.device()->CreateComputePipelineState(
                 &pipeline_desc, IID_PPV_ARGS(pipeline.put())),
             "CreateComputePipelineState(root constants)"))
    return false;

  constexpr std::array<std::uint32_t, 4> constants = {0x01234567u, 0x89abcdefu,
                                                      0x13579bdfu, 0x2468ace0u};
  auto output = context.CreateBuffer(sizeof(constants), D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context.CreateBuffer(
      sizeof(constants), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  if (!output || !readback)
    return false;

  context.list()->SetPipelineState(pipeline.get());
  context.list()->SetComputeRootSignature(root.get());
  context.list()->SetComputeRoot32BitConstants(0, constants.size(),
                                               constants.data(), 0);
  context.list()->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress());
  context.list()->Dispatch(1, 1, 1);
  Transition(context.list(), output.get(),
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                   sizeof(constants));
  if (!context.ExecuteAndReset())
    return false;
  *result = context.Readback(readback.get(), constants.size());
  return result->size() == constants.size();
}

bool RunDescriptorTableCase(OracleContext &context,
                            const std::array<std::uint32_t, kValueCount> &input,
                            std::vector<std::uint32_t> *result,
                            bool overwrite_after_recording = false) {
  constexpr std::string_view source = R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);

    [numthreads(16, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      uint value = input.Load(id.x * 4);
      output.Store(id.x * 4, value * 3 + id.x);
    }
  )";
  ComPtr<ID3DBlob> shader;
  ComPtr<ID3DBlob> diagnostics;
  if (!Check(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
                        "main", "cs_5_0",
                        D3DCOMPILE_ENABLE_STRICTNESS |
                            D3DCOMPILE_OPTIMIZATION_LEVEL3,
                        0, shader.put(), diagnostics.put()),
             "D3DCompile(descriptor table)"))
    return false;

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 1;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  ComPtr<ID3DBlob> root_blob;
  if (!Check(D3D12SerializeRootSignature(&root_desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         root_blob.put(), diagnostics.put()),
             "D3D12SerializeRootSignature(descriptor table)"))
    return false;
  ComPtr<ID3D12RootSignature> root;
  if (!Check(context.device()->CreateRootSignature(
                 0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                 IID_PPV_ARGS(root.put())),
             "CreateRootSignature(descriptor table)"))
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.get();
  pipeline_desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  ComPtr<ID3D12PipelineState> pipeline;
  if (!Check(context.device()->CreateComputePipelineState(
                 &pipeline_desc, IID_PPV_ARGS(pipeline.put())),
             "CreateComputePipelineState(descriptor table)"))
    return false;

  auto upload = context.CreateUpload(input);
  std::array<std::uint32_t, kValueCount> replacement_values = {};
  for (UINT index = 0; index < replacement_values.size(); ++index)
    replacement_values[index] = input[index] ^ (0xf00d0000u + index * 31u);
  auto replacement_upload = overwrite_after_recording
                                ? context.CreateUpload(replacement_values)
                                : ComPtr<ID3D12Resource>();
  auto output = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context.CreateBuffer(sizeof(input), D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 2;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> heap;
  if (!upload || (overwrite_after_recording && !replacement_upload) ||
      !output || !readback ||
      !Check(context.device()->CreateDescriptorHeap(&heap_desc,
                                                    IID_PPV_ARGS(heap.put())),
             "CreateDescriptorHeap(descriptor table)"))
    return false;

  const UINT increment = context.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  const auto srv_cpu = cpu;
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = kValueCount;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  context.device()->CreateShaderResourceView(upload.get(), &srv, cpu);
  cpu.ptr += increment;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kValueCount;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context.device()->CreateUnorderedAccessView(output.get(), nullptr, &uav, cpu);

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context.list()->SetDescriptorHeaps(1, heaps);
  context.list()->SetComputeRootSignature(root.get());
  context.list()->SetPipelineState(pipeline.get());
  context.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context.list()->Dispatch(1, 1, 1);
  if (overwrite_after_recording)
    context.device()->CreateShaderResourceView(replacement_upload.get(), &srv,
                                               srv_cpu);
  Transition(context.list(), output.get(),
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                   sizeof(input));
  if (!context.ExecuteAndReset())
    return false;
  *result = context.Readback(readback.get(), kValueCount);
  return result->size() == kValueCount;
}

bool RunInvalidDescriptorHeapCase(OracleContext &context,
                                  std::vector<std::uint32_t> *result) {
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = 0;
  void *output = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
  const HRESULT hr = context.device()->CreateDescriptorHeap(
      &desc, __uuidof(ID3D12DescriptorHeap), &output);
  *result = {static_cast<std::uint32_t>(hr), output == nullptr ? 1u : 0u};
  return FAILED(hr) && output == nullptr;
}

bool RunInvalidResourceCase(OracleContext &context,
                            std::vector<std::uint32_t> *result) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 0;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  void *output = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
  const HRESULT hr = context.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      __uuidof(ID3D12Resource), &output);
  *result = {static_cast<std::uint32_t>(hr), output == nullptr ? 1u : 0u};
  return FAILED(hr) && output == nullptr;
}

bool RunInvalidRootSignatureCase(OracleContext &context,
                                 std::vector<std::uint32_t> *result) {
  constexpr std::array<std::uint8_t, 16> invalid_bytecode = {};
  void *output = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
  const HRESULT hr = context.device()->CreateRootSignature(
      0, invalid_bytecode.data(), invalid_bytecode.size(),
      __uuidof(ID3D12RootSignature), &output);
  *result = {static_cast<std::uint32_t>(hr), output == nullptr ? 1u : 0u};
  return FAILED(hr) && output == nullptr;
}

bool RunUnsupportedIidCase(OracleContext &context,
                           std::vector<std::uint32_t> *result) {
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = 1;
  constexpr GUID unsupported_iid = {
      0x9b3b8f8a,
      0x1468,
      0x4ea7,
      {0x92, 0xe4, 0x55, 0xe8, 0xa5, 0xf8, 0xe1, 0x3c}};
  void *output = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
  const HRESULT hr =
      context.device()->CreateDescriptorHeap(&desc, unsupported_iid, &output);
  *result = {static_cast<std::uint32_t>(hr), output == nullptr ? 1u : 0u};
  return FAILED(hr) && output == nullptr;
}

bool RunClearCase(OracleContext &context, std::vector<std::uint32_t> *result,
                  bool use_rect) {
  constexpr UINT width = 4;
  constexpr UINT height = 4;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  ComPtr<ID3D12Resource> target;
  if (!Check(context.device()->CreateCommittedResource(
                 &heap, D3D12_HEAP_FLAG_NONE, &desc,
                 D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                 __uuidof(ID3D12Resource),
                 reinterpret_cast<void **>(target.put())),
             "CreateCommittedResource(texture)"))
    return false;
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (!Check(context.device()->CreateDescriptorHeap(
                 &rtv_heap_desc, __uuidof(ID3D12DescriptorHeap),
                 reinterpret_cast<void **>(rtv_heap.put())),
             "CreateDescriptorHeap"))
    return false;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT rows = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  context.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows,
                                          &row_size, &total_size);
  auto readback = context.CreateBuffer(total_size, D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  if (!readback)
    return false;

  constexpr FLOAT black[4] = {};
  constexpr FLOAT color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  if (use_rect) {
    context.list()->ClearRenderTargetView(rtv, black, 0, nullptr);
    const D3D12_RECT rect = {1, 1, 4, 3};
    context.list()->ClearRenderTargetView(rtv, color, 1, &rect);
  } else {
    context.list()->ClearRenderTargetView(rtv, color, 0, nullptr);
  }
  Transition(context.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION source = {};
  source.pResource = target.get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION destination = {};
  destination.pResource = readback.get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = footprint;
  context.list()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  if (!context.ExecuteAndReset())
    return false;

  void *mapping = nullptr;
  const D3D12_RANGE read = {0, static_cast<SIZE_T>(total_size)};
  if (!Check(readback->Map(0, &read, &mapping), "Map(texture readback)"))
    return false;
  result->resize(width * height);
  for (UINT y = 0; y < height; ++y) {
    std::memcpy(result->data() + y * width,
                static_cast<const std::uint8_t *>(mapping) + footprint.Offset +
                    y * footprint.Footprint.RowPitch,
                width * sizeof(std::uint32_t));
  }
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
  return true;
}

bool RunTextureCopyCase(OracleContext &context,
                        std::vector<std::uint32_t> *result) {
  constexpr UINT width = 4;
  constexpr UINT height = 4;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> texture;
  if (!Check(context.device()->CreateCommittedResource(
                 &heap, D3D12_HEAP_FLAG_NONE, &desc,
                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                 IID_PPV_ARGS(texture.put())),
             "CreateCommittedResource(texture copy)"))
    return false;

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT rows = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  context.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows,
                                          &row_size, &total_size);
  auto upload = context.CreateBuffer(total_size, D3D12_HEAP_TYPE_UPLOAD,
                                     D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_GENERIC_READ);
  auto readback = context.CreateBuffer(total_size, D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  if (!upload || !readback)
    return false;
  std::array<std::uint32_t, width * height> expected = {};
  for (UINT index = 0; index < expected.size(); ++index)
    expected[index] = 0xff000000u | ((index * 37u) << 16) |
                      ((index * 19u) << 8) | (index * 11u);
  void *mapping = nullptr;
  const D3D12_RANGE no_read = {};
  if (!Check(upload->Map(0, &no_read, &mapping), "Map(texture upload)"))
    return false;
  for (UINT row = 0; row < height; ++row) {
    std::memcpy(static_cast<std::uint8_t *>(mapping) + footprint.Offset +
                    row * footprint.Footprint.RowPitch,
                expected.data() + row * width, width * sizeof(std::uint32_t));
  }
  const D3D12_RANGE written = {0, static_cast<SIZE_T>(total_size)};
  upload->Unmap(0, &written);

  D3D12_TEXTURE_COPY_LOCATION buffer_location = {};
  buffer_location.pResource = upload.get();
  buffer_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  buffer_location.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION texture_location = {};
  texture_location.pResource = texture.get();
  texture_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  context.list()->CopyTextureRegion(&texture_location, 0, 0, 0,
                                    &buffer_location, nullptr);
  Transition(context.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  buffer_location.pResource = readback.get();
  context.list()->CopyTextureRegion(&buffer_location, 0, 0, 0,
                                    &texture_location, nullptr);
  if (!context.ExecuteAndReset())
    return false;

  const D3D12_RANGE read = {0, static_cast<SIZE_T>(total_size)};
  if (!Check(readback->Map(0, &read, &mapping), "Map(texture copy readback)"))
    return false;
  result->resize(width * height);
  for (UINT row = 0; row < height; ++row) {
    std::memcpy(result->data() + row * width,
                static_cast<const std::uint8_t *>(mapping) + footprint.Offset +
                    row * footprint.Footprint.RowPitch,
                width * sizeof(std::uint32_t));
  }
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
  return *result ==
         std::vector<std::uint32_t>(expected.begin(), expected.end());
}

bool RunDrawCase(OracleContext &context, std::vector<std::uint32_t> *result,
                 std::vector<std::uint32_t> *binary_query_result = nullptr) {
  constexpr std::string_view vertex_source = R"(
    struct Output { float4 position : SV_Position; };
    Output main(uint id : SV_VertexID) {
      float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      Output output;
      output.position = float4(positions[id], 0.0, 1.0);
      return output;
    }
  )";
  constexpr std::string_view pixel_source = R"(
    float4 main() : SV_Target { return float4(0.125, 0.5, 0.875, 1.0); }
  )";
  ComPtr<ID3DBlob> vertex;
  ComPtr<ID3DBlob> pixel;
  ComPtr<ID3DBlob> diagnostics;
  if (!Check(D3DCompile(vertex_source.data(), vertex_source.size(), nullptr,
                        nullptr, nullptr, "main", "vs_5_0",
                        D3DCOMPILE_ENABLE_STRICTNESS, 0, vertex.put(),
                        diagnostics.put()),
             "D3DCompile(draw vertex)") ||
      !Check(D3DCompile(pixel_source.data(), pixel_source.size(), nullptr,
                        nullptr, nullptr, "main", "ps_5_0",
                        D3DCOMPILE_ENABLE_STRICTNESS, 0, pixel.put(),
                        diagnostics.put()),
             "D3DCompile(draw pixel)"))
    return false;

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> root_blob;
  if (!Check(D3D12SerializeRootSignature(&root_desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         root_blob.put(), diagnostics.put()),
             "D3D12SerializeRootSignature(draw)"))
    return false;
  ComPtr<ID3D12RootSignature> root;
  if (!Check(context.device()->CreateRootSignature(
                 0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                 IID_PPV_ARGS(root.put())),
             "CreateRootSignature(draw)"))
    return false;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.get();
  pipeline_desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
  pipeline_desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
  pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline_desc.SampleMask = UINT_MAX;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline_desc.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pipeline;
  if (!Check(context.device()->CreateGraphicsPipelineState(
                 &pipeline_desc, IID_PPV_ARGS(pipeline.put())),
             "CreateGraphicsPipelineState(draw)"))
    return false;

  constexpr UINT width = 4;
  constexpr UINT height = 4;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> target;
  if (!Check(context.device()->CreateCommittedResource(
                 &heap, D3D12_HEAP_FLAG_NONE, &desc,
                 D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                 IID_PPV_ARGS(target.put())),
             "CreateCommittedResource(draw target)"))
    return false;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (!Check(context.device()->CreateDescriptorHeap(
                 &heap_desc, IID_PPV_ARGS(rtv_heap.put())),
             "CreateDescriptorHeap(draw RTV)"))
    return false;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT64 total_size = 0;
  context.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr,
                                          nullptr, &total_size);
  auto readback = context.CreateBuffer(total_size, D3D12_HEAP_TYPE_READBACK,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COPY_DEST);
  if (!readback)
    return false;
  ComPtr<ID3D12QueryHeap> query_heap;
  ComPtr<ID3D12Resource> query_readback;
  if (binary_query_result) {
    const D3D12_QUERY_HEAP_DESC query_desc = {
        D3D12_QUERY_HEAP_TYPE_OCCLUSION, 1, 0};
    if (!Check(context.device()->CreateQueryHeap(
                   &query_desc, IID_PPV_ARGS(query_heap.put())),
               "CreateQueryHeap(binary occlusion)"))
      return false;
    query_readback = context.CreateBuffer(
        sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    if (!query_readback)
      return false;
  }

  constexpr FLOAT clear[4] = {};
  context.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context.list()->SetGraphicsRootSignature(root.get());
  context.list()->SetPipelineState(pipeline.get());
  context.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, width, height, 0, 1};
  const D3D12_RECT scissor = {0, 0, width, height};
  context.list()->RSSetViewports(1, &viewport);
  context.list()->RSSetScissorRects(1, &scissor);
  if (binary_query_result)
    context.list()->BeginQuery(query_heap.get(),
                               D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
  context.list()->DrawInstanced(3, 1, 0, 0);
  if (binary_query_result) {
    context.list()->EndQuery(query_heap.get(),
                             D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
    context.list()->ResolveQueryData(query_heap.get(),
                                     D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, 1,
                                     query_readback.get(), 0);
  }
  Transition(context.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION source = {};
  source.pResource = target.get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION destination = {};
  destination.pResource = readback.get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = footprint;
  context.list()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  if (!context.ExecuteAndReset())
    return false;
  void *mapping = nullptr;
  const D3D12_RANGE read = {0, static_cast<SIZE_T>(total_size)};
  if (!Check(readback->Map(0, &read, &mapping), "Map(draw readback)"))
    return false;
  result->resize(width * height);
  for (UINT row = 0; row < height; ++row) {
    std::memcpy(result->data() + row * width,
                static_cast<const std::uint8_t *>(mapping) + footprint.Offset +
                    row * footprint.Footprint.RowPitch,
                width * sizeof(std::uint32_t));
  }
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
  if (binary_query_result) {
    *binary_query_result = context.Readback(query_readback.get(), 2);
    return binary_query_result->size() == 2;
  }
  return true;
}

bool RunBlendCase(OracleContext &context, std::vector<std::uint32_t> *result) {
  constexpr std::string_view vertex_source = R"(
    float4 main(uint id : SV_VertexID) : SV_Position {
      float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      return float4(positions[id], 0.0, 1.0);
    }
  )";
  constexpr std::string_view pixel_source = R"(
    float4 main() : SV_Target {
      return float4(0.25, 0.125, 0.0625, 0.25);
    }
  )";
  ComPtr<ID3DBlob> vertex;
  ComPtr<ID3DBlob> pixel;
  if (!CompileShader(vertex_source, "vs_5_0", "D3DCompile(blend vertex)",
                     &vertex) ||
      !CompileShader(pixel_source, "ps_5_0", "D3DCompile(blend pixel)", &pixel))
    return false;

  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> root_blob;
  ComPtr<ID3DBlob> diagnostics;
  if (!Check(D3D12SerializeRootSignature(&root_desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         root_blob.put(), diagnostics.put()),
             "D3D12SerializeRootSignature(blend)"))
    return false;
  ComPtr<ID3D12RootSignature> root;
  if (!Check(context.device()->CreateRootSignature(
                 0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                 IID_PPV_ARGS(root.put())),
             "CreateRootSignature(blend)"))
    return false;

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.get();
  pipeline_desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
  pipeline_desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
  auto &blend = pipeline_desc.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_ONE;
  blend.DestBlend = D3D12_BLEND_ONE;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.DestBlendAlpha = D3D12_BLEND_ONE;
  blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend.LogicOp = D3D12_LOGIC_OP_NOOP;
  blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline_desc.SampleMask = UINT_MAX;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline_desc.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pipeline;
  if (!Check(context.device()->CreateGraphicsPipelineState(
                 &pipeline_desc, IID_PPV_ARGS(pipeline.put())),
             "CreateGraphicsPipelineState(blend)"))
    return false;

  constexpr UINT width = 4;
  constexpr UINT height = 4;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> target;
  if (!Check(context.device()->CreateCommittedResource(
                 &heap, D3D12_HEAP_FLAG_NONE, &desc,
                 D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                 IID_PPV_ARGS(target.put())),
             "CreateCommittedResource(blend target)"))
    return false;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (!Check(context.device()->CreateDescriptorHeap(
                 &heap_desc, IID_PPV_ARGS(rtv_heap.put())),
             "CreateDescriptorHeap(blend RTV)"))
    return false;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  constexpr FLOAT clear[4] = {};
  context.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context.list()->SetGraphicsRootSignature(root.get());
  context.list()->SetPipelineState(pipeline.get());
  context.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, width, height, 0, 1};
  const D3D12_RECT scissor = {0, 0, width, height};
  context.list()->RSSetViewports(1, &viewport);
  context.list()->RSSetScissorRects(1, &scissor);
  context.list()->DrawInstanced(3, 1, 0, 0);
  context.list()->DrawInstanced(3, 1, 0, 0);
  return ReadbackRgba8(context, target.get(),
                       D3D12_RESOURCE_STATE_RENDER_TARGET, result,
                       "Map(blend readback)");
}

bool RunDepthCase(OracleContext &context, std::vector<std::uint32_t> *result) {
  constexpr std::string_view vertex_source = R"(
    cbuffer Parameters : register(b0) { float depth; };
    float4 main(uint id : SV_VertexID) : SV_Position {
      float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      return float4(positions[id], depth, 1.0);
    }
  )";
  constexpr std::string_view green_source = R"(
    float4 main() : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }
  )";
  constexpr std::string_view red_source = R"(
    float4 main() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }
  )";
  ComPtr<ID3DBlob> vertex;
  ComPtr<ID3DBlob> green;
  ComPtr<ID3DBlob> red;
  if (!CompileShader(vertex_source, "vs_5_0", "D3DCompile(depth vertex)",
                     &vertex) ||
      !CompileShader(green_source, "ps_5_0", "D3DCompile(depth green)",
                     &green) ||
      !CompileShader(red_source, "ps_5_0", "D3DCompile(depth red)", &red))
    return false;

  D3D12_ROOT_PARAMETER root_parameter = {};
  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameter.Constants.Num32BitValues = 1;
  root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &root_parameter;
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> root_blob;
  ComPtr<ID3DBlob> diagnostics;
  if (!Check(D3D12SerializeRootSignature(&root_desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         root_blob.put(), diagnostics.put()),
             "D3D12SerializeRootSignature(depth)"))
    return false;
  ComPtr<ID3D12RootSignature> root;
  if (!Check(context.device()->CreateRootSignature(
                 0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                 IID_PPV_ARGS(root.put())),
             "CreateRootSignature(depth)"))
    return false;

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.get();
  pipeline_desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
  pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline_desc.SampleMask = UINT_MAX;
  pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
  pipeline_desc.DepthStencilState.DepthEnable = TRUE;
  pipeline_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  pipeline_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline_desc.NumRenderTargets = 1;
  pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pipeline_desc.SampleDesc.Count = 1;
  auto create_pipeline = [&](ID3DBlob *pixel, std::string_view operation,
                             ComPtr<ID3D12PipelineState> *pipeline) {
    pipeline_desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    return Check(context.device()->CreateGraphicsPipelineState(
                     &pipeline_desc, IID_PPV_ARGS(pipeline->put())),
                 operation);
  };
  ComPtr<ID3D12PipelineState> green_pipeline;
  ComPtr<ID3D12PipelineState> red_pipeline;
  if (!create_pipeline(green.get(), "CreateGraphicsPipelineState(depth green)",
                       &green_pipeline) ||
      !create_pipeline(red.get(), "CreateGraphicsPipelineState(depth red)",
                       &red_pipeline))
    return false;

  constexpr UINT width = 4;
  constexpr UINT height = 4;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC color_desc = {};
  color_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  color_desc.Width = width;
  color_desc.Height = height;
  color_desc.DepthOrArraySize = 1;
  color_desc.MipLevels = 1;
  color_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  color_desc.SampleDesc.Count = 1;
  color_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  ComPtr<ID3D12Resource> target;
  if (!Check(context.device()->CreateCommittedResource(
                 &heap, D3D12_HEAP_FLAG_NONE, &color_desc,
                 D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                 IID_PPV_ARGS(target.put())),
             "CreateCommittedResource(depth color target)"))
    return false;
  D3D12_RESOURCE_DESC depth_desc = color_desc;
  depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
  depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  ComPtr<ID3D12Resource> depth;
  if (!Check(context.device()->CreateCommittedResource(
                 &heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
                 D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
                 IID_PPV_ARGS(depth.put())),
             "CreateCommittedResource(depth target)"))
    return false;

  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
  dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsv_heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> dsv_heap;
  if (!Check(context.device()->CreateDescriptorHeap(
                 &rtv_heap_desc, IID_PPV_ARGS(rtv_heap.put())),
             "CreateDescriptorHeap(depth RTV)") ||
      !Check(context.device()->CreateDescriptorHeap(
                 &dsv_heap_desc, IID_PPV_ARGS(dsv_heap.put())),
             "CreateDescriptorHeap(depth DSV)"))
    return false;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  const auto dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  context.device()->CreateDepthStencilView(depth.get(), nullptr, dsv);

  constexpr FLOAT clear[4] = {};
  context.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
                                        nullptr);
  context.list()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
  context.list()->SetGraphicsRootSignature(root.get());
  context.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, width, height, 0, 1};
  const D3D12_RECT scissor = {0, 0, width, height};
  context.list()->RSSetViewports(1, &viewport);
  context.list()->RSSetScissorRects(1, &scissor);
  context.list()->SetPipelineState(green_pipeline.get());
  context.list()->SetGraphicsRoot32BitConstant(0, FloatBits(0.25f), 0);
  context.list()->DrawInstanced(3, 1, 0, 0);
  context.list()->SetPipelineState(red_pipeline.get());
  context.list()->SetGraphicsRoot32BitConstant(0, FloatBits(0.75f), 0);
  context.list()->DrawInstanced(3, 1, 0, 0);
  return ReadbackRgba8(context, target.get(),
                       D3D12_RESOURCE_STATE_RENDER_TARGET, result,
                       "Map(depth readback)");
}

bool RunMsaaResolveCase(OracleContext &context,
                        std::vector<std::uint32_t> *result) {
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
  quality.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  quality.SampleCount = 4;
  quality.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
  if (!Check(context.device()->CheckFeatureSupport(
                 D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
                 sizeof(quality)),
             "CheckFeatureSupport(4x MSAA)") ||
      quality.NumQualityLevels == 0)
    return false;

  constexpr UINT width = 4;
  constexpr UINT height = 4;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC source_desc = {};
  source_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  source_desc.Width = width;
  source_desc.Height = height;
  source_desc.DepthOrArraySize = 1;
  source_desc.MipLevels = 1;
  source_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  source_desc.SampleDesc.Count = 4;
  source_desc.SampleDesc.Quality = 0;
  source_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  ComPtr<ID3D12Resource> source;
  if (!Check(context.device()->CreateCommittedResource(
                 &heap, D3D12_HEAP_FLAG_NONE, &source_desc,
                 D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                 IID_PPV_ARGS(source.put())),
             "CreateCommittedResource(MSAA source)"))
    return false;
  D3D12_RESOURCE_DESC destination_desc = source_desc;
  destination_desc.SampleDesc.Count = 1;
  destination_desc.SampleDesc.Quality = 0;
  destination_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  ComPtr<ID3D12Resource> destination;
  if (!Check(context.device()->CreateCommittedResource(
                 &heap, D3D12_HEAP_FLAG_NONE, &destination_desc,
                 D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr,
                 IID_PPV_ARGS(destination.put())),
             "CreateCommittedResource(resolve destination)"))
    return false;

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (!Check(context.device()->CreateDescriptorHeap(
                 &heap_desc, IID_PPV_ARGS(rtv_heap.put())),
             "CreateDescriptorHeap(MSAA RTV)"))
    return false;
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context.device()->CreateRenderTargetView(source.get(), nullptr, rtv);

  constexpr FLOAT magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
  context.list()->ClearRenderTargetView(rtv, magenta, 0, nullptr);
  Transition(context.list(), source.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
             D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  context.list()->ResolveSubresource(destination.get(), 0, source.get(), 0,
                                     DXGI_FORMAT_R8G8B8A8_UNORM);
  return ReadbackRgba8(context, destination.get(),
                       D3D12_RESOURCE_STATE_RESOLVE_DEST, result,
                       "Map(MSAA resolve readback)");
}

bool RunExecuteIndirectCase(OracleContext &context,
                            std::vector<std::uint32_t> *result) {
  constexpr std::string_view source = R"(
    RWByteAddressBuffer output : register(u0);
    [numthreads(4, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      output.Store(id.x * 4, 0x13579bdfu ^ (id.x * 0x01010101u));
    }
  )";
  ComPtr<ID3DBlob> shader;
  if (!CompileShader(source, "cs_5_0", "D3DCompile(ExecuteIndirect)", &shader))
    return false;

  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameter.Descriptor.ShaderRegister = 0;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  ComPtr<ID3DBlob> root_blob;
  ComPtr<ID3DBlob> diagnostics;
  if (!Check(D3D12SerializeRootSignature(&root_desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         root_blob.put(), diagnostics.put()),
             "D3D12SerializeRootSignature(ExecuteIndirect)"))
    return false;
  ComPtr<ID3D12RootSignature> root;
  if (!Check(context.device()->CreateRootSignature(
                 0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                 IID_PPV_ARGS(root.put())),
             "CreateRootSignature(ExecuteIndirect)"))
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
  pipeline_desc.pRootSignature = root.get();
  pipeline_desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  ComPtr<ID3D12PipelineState> pipeline;
  if (!Check(context.device()->CreateComputePipelineState(
                 &pipeline_desc, IID_PPV_ARGS(pipeline.put())),
             "CreateComputePipelineState(ExecuteIndirect)"))
    return false;

  D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
  argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
  signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
  signature_desc.NumArgumentDescs = 1;
  signature_desc.pArgumentDescs = &argument_desc;
  ComPtr<ID3D12CommandSignature> signature;
  if (!Check(context.device()->CreateCommandSignature(
                 &signature_desc, nullptr, IID_PPV_ARGS(signature.put())),
             "CreateCommandSignature(ExecuteIndirect)"))
    return false;

  constexpr UINT value_count = 4;
  auto output = context.CreateBuffer(value_count * sizeof(std::uint32_t),
                                     D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context.CreateBuffer(
      value_count * sizeof(std::uint32_t), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  auto arguments = context.CreateBuffer(
      sizeof(D3D12_DISPATCH_ARGUMENTS), D3D12_HEAP_TYPE_UPLOAD,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
  if (!output || !readback || !arguments)
    return false;
  const D3D12_DISPATCH_ARGUMENTS dispatch = {1, 1, 1};
  void *mapping = nullptr;
  const D3D12_RANGE no_read = {};
  if (!Check(arguments->Map(0, &no_read, &mapping),
             "Map(ExecuteIndirect arguments)"))
    return false;
  std::memcpy(mapping, &dispatch, sizeof(dispatch));
  const D3D12_RANGE written = {0, sizeof(dispatch)};
  arguments->Unmap(0, &written);

  context.list()->SetPipelineState(pipeline.get());
  context.list()->SetComputeRootSignature(root.get());
  context.list()->SetComputeRootUnorderedAccessView(
      0, output->GetGPUVirtualAddress());
  context.list()->ExecuteIndirect(signature.get(), 1, arguments.get(), 0,
                                  nullptr, 0);
  Transition(context.list(), output.get(),
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                   value_count * sizeof(std::uint32_t));
  if (!context.ExecuteAndReset())
    return false;
  *result = context.Readback(readback.get(), value_count);
  return result->size() == value_count;
}

bool RunTimestampOrderingCase(OracleContext &context,
                              std::vector<std::uint32_t> *result) {
  constexpr UINT timestamp_count = 3;
  D3D12_QUERY_HEAP_DESC query_desc = {};
  query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  query_desc.Count = timestamp_count;
  ComPtr<ID3D12QueryHeap> query_heap;
  if (!Check(context.device()->CreateQueryHeap(&query_desc,
                                               IID_PPV_ARGS(query_heap.put())),
             "CreateQueryHeap(timestamp ordering)"))
    return false;

  std::array<std::uint32_t, kValueCount> payload = {};
  for (UINT index = 0; index < payload.size(); ++index)
    payload[index] = 0x31415926u ^ (index * 0x01020304u);
  std::array<std::uint32_t, kValueCount> query_sentinel = {};
  query_sentinel.fill(0xffffffffu);
  auto upload = context.CreateUpload(payload);
  auto sentinel_upload = context.CreateUpload(query_sentinel);
  auto intermediate = context.CreateBuffer(
      sizeof(payload), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto copy_readback = context.CreateBuffer(
      sizeof(payload), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto query_readback = context.CreateBuffer(
      timestamp_count * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  if (!upload || !sentinel_upload || !intermediate || !copy_readback ||
      !query_readback)
    return false;

  context.list()->CopyBufferRegion(query_readback.get(), 0,
                                   sentinel_upload.get(), 0,
                                   timestamp_count * sizeof(UINT64));
  context.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
  context.list()->CopyBufferRegion(intermediate.get(), 0, upload.get(), 0,
                                   sizeof(payload));
  context.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
  Transition(context.list(), intermediate.get(), D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(copy_readback.get(), 0, intermediate.get(),
                                   0, sizeof(payload));
  context.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
  context.list()->ResolveQueryData(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                   0, timestamp_count, query_readback.get(), 0);
  if (!context.ExecuteAndReset())
    return false;

  const auto copied_payload =
      context.Readback(copy_readback.get(), kValueCount);
  if (copied_payload !=
      std::vector<std::uint32_t>(payload.begin(), payload.end())) {
    std::cerr << "timestamp ordering payload copy did not execute\n";
    return false;
  }

  const auto words =
      context.Readback(query_readback.get(), timestamp_count * 2);
  if (words.size() != timestamp_count * 2)
    return false;
  auto timestamp = [&](UINT index) {
    return static_cast<UINT64>(words[index * 2]) |
           (static_cast<UINT64>(words[index * 2 + 1]) << 32);
  };
  const UINT64 first = timestamp(0);
  const UINT64 middle = timestamp(1);
  const UINT64 last = timestamp(2);
  if (first == ~UINT64{0} || middle == ~UINT64{0} || last == ~UINT64{0}) {
    std::cerr << "timestamp resolve left sentinel values in readback\n";
    return false;
  }
  *result = {timestamp_count, first <= middle ? 1u : 0u,
             middle <= last ? 1u : 0u, first <= last ? 1u : 0u};
  return true;
}

bool RunRenderPassFlattenedEquivalenceCase(OracleContext &context,
                                           std::vector<std::uint32_t> *result) {
  constexpr UINT width = 4;
  constexpr UINT height = 4;
  constexpr DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  constexpr FLOAT clear_color[4] = {0.0f, 1.0f, 1.0f, 1.0f};
  constexpr std::uint32_t expected_pixel = 0xffffff00u;

  ComPtr<ID3D12GraphicsCommandList4> list4;
  if (!Check(context.list()->QueryInterface(IID_PPV_ARGS(list4.put())),
             "QueryInterface(ID3D12GraphicsCommandList4)"))
    return false;

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
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  auto create_target = [&](std::string_view operation) {
    ComPtr<ID3D12Resource> target;
    if (!Check(context.device()->CreateCommittedResource(
                   &heap, D3D12_HEAP_FLAG_NONE, &desc,
                   D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                   IID_PPV_ARGS(target.put())),
               operation))
      return ComPtr<ID3D12Resource>();
    return target;
  };
  auto render_pass_target =
      create_target("CreateCommittedResource(render-pass equivalence target)");
  auto flattened_target =
      create_target("CreateCommittedResource(flattened equivalence target)");

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = 2;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (!render_pass_target || !flattened_target ||
      !Check(context.device()->CreateDescriptorHeap(
                 &heap_desc, IID_PPV_ARGS(rtv_heap.put())),
             "CreateDescriptorHeap(render-pass equivalence RTV)"))
    return false;
  const UINT rtv_increment = context.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  const auto render_pass_rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  auto flattened_rtv = render_pass_rtv;
  flattened_rtv.ptr += rtv_increment;
  context.device()->CreateRenderTargetView(render_pass_target.get(), nullptr,
                                           render_pass_rtv);
  context.device()->CreateRenderTargetView(flattened_target.get(), nullptr,
                                           flattened_rtv);

  D3D12_RENDER_PASS_RENDER_TARGET_DESC pass = {};
  pass.cpuDescriptor = render_pass_rtv;
  pass.BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
  pass.BeginningAccess.Clear.ClearValue.Format = format;
  std::memcpy(pass.BeginningAccess.Clear.ClearValue.Color, clear_color,
              sizeof(clear_color));
  pass.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
  list4->BeginRenderPass(1, &pass, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
  list4->EndRenderPass();
  context.list()->ClearRenderTargetView(flattened_rtv, clear_color, 0, nullptr);

  std::vector<std::uint32_t> render_pass_values;
  std::vector<std::uint32_t> flattened_values;
  if (!ReadbackRgba8(context, render_pass_target.get(),
                     D3D12_RESOURCE_STATE_RENDER_TARGET, &render_pass_values,
                     "Map(render-pass equivalence readback)") ||
      !ReadbackRgba8(context, flattened_target.get(),
                     D3D12_RESOURCE_STATE_RENDER_TARGET, &flattened_values,
                     "Map(flattened equivalence readback)"))
    return false;

  result->clear();
  result->reserve(render_pass_values.size() + flattened_values.size());
  result->insert(result->end(), render_pass_values.begin(),
                 render_pass_values.end());
  result->insert(result->end(), flattened_values.begin(),
                 flattened_values.end());
  return render_pass_values == flattened_values &&
         render_pass_values ==
             std::vector<std::uint32_t>(width * height, expected_pixel);
}

void WriteValues(std::ostream &output, std::string_view name,
                 const std::vector<std::uint32_t> &values, bool trailing) {
  output << "    \"" << name
         << "\": {\"kind\": \"u32-array\", \"hash_fnv1a64\": \"0x" << std::hex
         << std::setw(16) << std::setfill('0') << Hash(values) << std::dec
         << "\", \"values\": [";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index)
      output << ", ";
    output << values[index];
  }
  output << "]}" << (trailing ? "," : "") << '\n';
}

} // namespace

int main(int argc, char **argv) {
  bool use_warp = false;
  std::string output_path;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--adapter=warp")
      use_warp = true;
    else if (argument == "--adapter=default")
      use_warp = false;
    else if (argument.starts_with("--output="))
      output_path = argument.substr(std::string_view("--output=").size());
    else {
      std::cerr << "usage: " << argv[0]
                << " [--adapter=default|--adapter=warp] [--output=path]\n";
      return 2;
    }
  }

  OracleContext context;
  if (!context.Initialize(use_warp))
    return 1;
  std::array<std::uint32_t, kValueCount> input = {};
  for (UINT index = 0; index < input.size(); ++index)
    input[index] = 0x10203040u + index * 0x01020408u;

  std::vector<std::uint32_t> copy;
  std::vector<std::uint32_t> offset_copy;
  std::vector<std::uint32_t> barrier_chain;
  std::vector<std::uint32_t> cross_queue_fence;
  std::vector<std::uint32_t> timestamp_ordering;
  std::vector<std::uint32_t> compute;
  std::vector<std::uint32_t> predicated_compute;
  std::vector<std::uint32_t> predicated_compute_false;
  std::vector<std::uint32_t> predicated_equal_zero_execute;
  std::vector<std::uint32_t> predicated_equal_zero_skip;
  std::vector<std::uint32_t> predication_disabled_compute;
  std::vector<std::uint32_t> root_constants;
  std::vector<std::uint32_t> descriptor_table;
  std::vector<std::uint32_t> descriptor_overwrite;
  std::vector<std::uint32_t> invalid_descriptor_heap;
  std::vector<std::uint32_t> invalid_resource;
  std::vector<std::uint32_t> invalid_root_signature;
  std::vector<std::uint32_t> unsupported_iid;
  std::vector<std::uint32_t> clear;
  std::vector<std::uint32_t> clear_rect;
  std::vector<std::uint32_t> render_pass_flattened;
  std::vector<std::uint32_t> texture_copy;
  std::vector<std::uint32_t> draw;
  std::vector<std::uint32_t> binary_occlusion;
  std::vector<std::uint32_t> blend;
  std::vector<std::uint32_t> depth;
  std::vector<std::uint32_t> msaa_resolve;
  std::vector<std::uint32_t> execute_indirect;
  if (!RunCopyCase(context, input, &copy)) {
    std::cerr << "buffer_copy case failed\n";
    return 1;
  }
  if (!RunOffsetCopyCase(context, input, &offset_copy)) {
    std::cerr << "buffer_copy_offset case failed\n";
    return 1;
  }
  if (!RunBarrierChainCase(context, input, &barrier_chain)) {
    std::cerr << "barrier_copy_chain case failed\n";
    return 1;
  }
  if (!RunCrossQueueFenceCase(context, input, &cross_queue_fence)) {
    std::cerr << "cross_queue_fence_copy case failed\n";
    return 1;
  }
  if (!RunTimestampOrderingCase(context, &timestamp_ordering)) {
    std::cerr << "timestamp_query_ordering case failed\n";
    return 1;
  }
  if (!RunComputeCase(context, input, &compute)) {
    std::cerr << "compute_u32 case failed\n";
    return 1;
  }
  if (!RunComputeCase(context, input, &predicated_compute,
                      ComputePredication::NotEqualZeroExecute)) {
    std::cerr << "predicated_compute_true case failed\n";
    return 1;
  }
  if (!RunComputeCase(context, input, &predicated_compute_false,
                      ComputePredication::NotEqualZeroSkip)) {
    std::cerr << "predicated_compute_false case failed\n";
    return 1;
  }
  if (!RunComputeCase(context, input, &predicated_equal_zero_execute,
                      ComputePredication::EqualZeroExecute)) {
    std::cerr << "predicated_compute_equal_zero_execute case failed\n";
    return 1;
  }
  if (!RunComputeCase(context, input, &predicated_equal_zero_skip,
                      ComputePredication::EqualZeroSkip)) {
    std::cerr << "predicated_compute_equal_zero_skip case failed\n";
    return 1;
  }
  if (!RunComputeCase(context, input, &predication_disabled_compute,
                      ComputePredication::DisableBeforeDispatch)) {
    std::cerr << "predication_disabled_compute case failed\n";
    return 1;
  }
  if (!RunRootConstantsCase(context, &root_constants)) {
    std::cerr << "root_constants_uav case failed\n";
    return 1;
  }
  if (!RunDescriptorTableCase(context, input, &descriptor_table)) {
    std::cerr << "descriptor_table_compute case failed\n";
    return 1;
  }
  if (!RunDescriptorTableCase(context, input, &descriptor_overwrite, true)) {
    std::cerr << "descriptor_overwrite_before_submit case failed\n";
    return 1;
  }
  if (!RunInvalidDescriptorHeapCase(context, &invalid_descriptor_heap)) {
    std::cerr << "invalid_descriptor_heap case failed\n";
    return 1;
  }
  if (!RunInvalidResourceCase(context, &invalid_resource)) {
    std::cerr << "invalid_resource case failed\n";
    return 1;
  }
  if (!RunInvalidRootSignatureCase(context, &invalid_root_signature)) {
    std::cerr << "invalid_root_signature case failed\n";
    return 1;
  }
  if (!RunUnsupportedIidCase(context, &unsupported_iid)) {
    std::cerr << "unsupported_iid case failed\n";
    return 1;
  }
  if (!RunClearCase(context, &clear, false)) {
    std::cerr << "clear_rgba8 case failed\n";
    return 1;
  }
  if (!RunClearCase(context, &clear_rect, true)) {
    std::cerr << "clear_rect_rgba8 case failed\n";
    return 1;
  }
  if (!RunRenderPassFlattenedEquivalenceCase(context, &render_pass_flattened)) {
    std::cerr << "render_pass_flattened_equivalence case failed\n";
    return 1;
  }
  if (!RunTextureCopyCase(context, &texture_copy)) {
    std::cerr << "texture_copy_rgba8 case failed\n";
    return 1;
  }
  if (!RunDrawCase(context, &draw, &binary_occlusion)) {
    std::cerr << "draw_fullscreen_rgba8 case failed\n";
    return 1;
  }
  if (!RunBlendCase(context, &blend)) {
    std::cerr << "blend_additive_rgba8 case failed\n";
    return 1;
  }
  if (!RunDepthCase(context, &depth)) {
    std::cerr << "depth_reject_rgba8 case failed\n";
    return 1;
  }
  if (!RunMsaaResolveCase(context, &msaa_resolve)) {
    std::cerr << "msaa_resolve_rgba8 case failed\n";
    return 1;
  }
  if (!RunExecuteIndirectCase(context, &execute_indirect)) {
    std::cerr << "execute_indirect_dispatch case failed\n";
    return 1;
  }

  std::ofstream output_file;
  std::ostream *output = &std::cout;
  if (!output_path.empty()) {
    output_file.open(output_path, std::ios::trunc);
    if (!output_file) {
      std::cerr << "failed to open snapshot output: " << output_path << '\n';
      return 1;
    }
    output = &output_file;
  }
  *output << "{\n"
          << "  \"schema_version\": 1,\n"
          << "  \"suite\": \"d3d12-core-differential\",\n"
          << "  \"adapter\": {\"mode\": \"" << (use_warp ? "warp" : "default")
          << "\", \"vendor_id\": " << context.vendor_id()
          << ", \"device_id\": " << context.device_id() << "},\n"
          << "  \"cases\": {\n";
  WriteValues(*output, "buffer_copy", copy, true);
  WriteValues(*output, "buffer_copy_offset", offset_copy, true);
  WriteValues(*output, "barrier_copy_chain", barrier_chain, true);
  WriteValues(*output, "cross_queue_fence_copy", cross_queue_fence, true);
  WriteValues(*output, "timestamp_query_ordering", timestamp_ordering, true);
  WriteValues(*output, "compute_u32", compute, true);
  WriteValues(*output, "predicated_compute_true", predicated_compute, true);
  WriteValues(*output, "predicated_compute_false", predicated_compute_false,
              true);
  WriteValues(*output, "predicated_compute_equal_zero_execute",
              predicated_equal_zero_execute, true);
  WriteValues(*output, "predicated_compute_equal_zero_skip",
              predicated_equal_zero_skip, true);
  WriteValues(*output, "predication_disabled_compute",
              predication_disabled_compute, true);
  WriteValues(*output, "root_constants_uav", root_constants, true);
  WriteValues(*output, "descriptor_table_compute", descriptor_table, true);
  WriteValues(*output, "descriptor_overwrite_before_submit",
              descriptor_overwrite, true);
  WriteValues(*output, "invalid_descriptor_heap", invalid_descriptor_heap,
              true);
  WriteValues(*output, "invalid_resource", invalid_resource, true);
  WriteValues(*output, "invalid_root_signature", invalid_root_signature, true);
  WriteValues(*output, "unsupported_iid", unsupported_iid, true);
  WriteValues(*output, "clear_rgba8", clear, true);
  WriteValues(*output, "clear_rect_rgba8", clear_rect, true);
  WriteValues(*output, "render_pass_flattened_equivalence",
              render_pass_flattened, true);
  WriteValues(*output, "texture_copy_rgba8", texture_copy, true);
  WriteValues(*output, "draw_fullscreen_rgba8", draw, true);
  WriteValues(*output, "binary_occlusion_nonzero", binary_occlusion, true);
  WriteValues(*output, "blend_additive_rgba8", blend, true);
  WriteValues(*output, "depth_reject_rgba8", depth, true);
  WriteValues(*output, "msaa_resolve_rgba8", msaa_resolve, true);
  WriteValues(*output, "execute_indirect_dispatch", execute_indirect, false);
  *output << "  }\n}\n";
  return *output ? 0 : 1;
}
