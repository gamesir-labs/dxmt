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

    if (use_warp &&
        !Check(factory_->EnumWarpAdapter(
                   __uuidof(IDXGIAdapter1),
                   reinterpret_cast<void **>(adapter_.put())),
               "EnumWarpAdapter"))
      return false;

    if (!Check(D3D12CreateDevice(
                   adapter_.get(), D3D_FEATURE_LEVEL_11_0,
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
        !Check(device_->CreateFence(
                   0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
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

  ComPtr<ID3D12Resource> CreateUpload(
      const std::array<std::uint32_t, kValueCount> &values) const {
    auto upload =
        CreateBuffer(sizeof(values), D3D12_HEAP_TYPE_UPLOAD,
                     D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
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
                D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
}

bool RunCopyCase(OracleContext &context,
                 const std::array<std::uint32_t, kValueCount> &input,
                 std::vector<std::uint32_t> *result) {
  auto upload = context.CreateUpload(input);
  auto gpu = context.CreateBuffer(
      sizeof(input), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context.CreateBuffer(
      sizeof(input), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
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
  auto readback = context.CreateBuffer(
      sizeof(input), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
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

bool RunComputeCase(OracleContext &context,
                    const std::array<std::uint32_t, kValueCount> &input,
                    std::vector<std::uint32_t> *result) {
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
      std::cerr.write(static_cast<const char *>(diagnostics->GetBufferPointer()),
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
  if (!Check(D3D12SerializeRootSignature(
                 &root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, root_blob.put(),
                 diagnostics.put()),
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
  auto output = context.CreateBuffer(
      sizeof(input), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context.CreateBuffer(
      sizeof(input), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  if (!upload || !output || !readback)
    return false;
  context.list()->SetPipelineState(pipeline.get());
  context.list()->SetComputeRootSignature(root.get());
  context.list()->SetComputeRootShaderResourceView(
      0, upload->GetGPUVirtualAddress());
  context.list()->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress());
  context.list()->Dispatch(1, 1, 1);
  Transition(context.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                   sizeof(input));
  if (!context.ExecuteAndReset())
    return false;
  *result = context.Readback(readback.get(), kValueCount);
  return result->size() == kValueCount;
}

bool RunDescriptorTableCase(
    OracleContext &context,
    const std::array<std::uint32_t, kValueCount> &input,
    std::vector<std::uint32_t> *result) {
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
  if (!Check(D3D12SerializeRootSignature(
                 &root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, root_blob.put(),
                 diagnostics.put()),
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
  auto output = context.CreateBuffer(
      sizeof(input), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto readback = context.CreateBuffer(
      sizeof(input), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 2;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> heap;
  if (!upload || !output || !readback ||
      !Check(context.device()->CreateDescriptorHeap(
                 &heap_desc, IID_PPV_ARGS(heap.put())),
             "CreateDescriptorHeap(descriptor table)"))
    return false;

  const UINT increment = context.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
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
  context.device()->CreateUnorderedAccessView(output.get(), nullptr, &uav,
                                               cpu);

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context.list()->SetDescriptorHeaps(1, heaps);
  context.list()->SetComputeRootSignature(root.get());
  context.list()->SetPipelineState(pipeline.get());
  context.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context.list()->Dispatch(1, 1, 1);
  Transition(context.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.list()->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                   sizeof(input));
  if (!context.ExecuteAndReset())
    return false;
  *result = context.Readback(readback.get(), kValueCount);
  return result->size() == kValueCount;
}

bool RunClearCase(OracleContext &context,
                  std::vector<std::uint32_t> *result, bool use_rect) {
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
  auto readback = context.CreateBuffer(
      total_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
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

void WriteValues(std::ostream &output, std::string_view name,
                 const std::vector<std::uint32_t> &values, bool trailing) {
  output << "    \"" << name
         << "\": {\"kind\": \"u32-array\", \"hash_fnv1a64\": \"0x"
         << std::hex << std::setw(16) << std::setfill('0') << Hash(values)
         << std::dec << "\", \"values\": [";
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
  std::vector<std::uint32_t> compute;
  std::vector<std::uint32_t> descriptor_table;
  std::vector<std::uint32_t> clear;
  std::vector<std::uint32_t> clear_rect;
  if (!RunCopyCase(context, input, &copy)) {
    std::cerr << "buffer_copy case failed\n";
    return 1;
  }
  if (!RunOffsetCopyCase(context, input, &offset_copy)) {
    std::cerr << "buffer_copy_offset case failed\n";
    return 1;
  }
  if (!RunComputeCase(context, input, &compute)) {
    std::cerr << "compute_u32 case failed\n";
    return 1;
  }
  if (!RunDescriptorTableCase(context, input, &descriptor_table)) {
    std::cerr << "descriptor_table_compute case failed\n";
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
          << "  \"adapter\": {\"mode\": \""
          << (use_warp ? "warp" : "default") << "\", \"vendor_id\": "
          << context.vendor_id() << ", \"device_id\": "
          << context.device_id() << "},\n"
          << "  \"cases\": {\n";
  WriteValues(*output, "buffer_copy", copy, true);
  WriteValues(*output, "buffer_copy_offset", offset_copy, true);
  WriteValues(*output, "compute_u32", compute, true);
  WriteValues(*output, "descriptor_table_compute", descriptor_table, true);
  WriteValues(*output, "clear_rgba8", clear, true);
  WriteValues(*output, "clear_rect_rgba8", clear_rect, false);
  *output << "  }\n}\n";
  return *output ? 0 : 1;
}
