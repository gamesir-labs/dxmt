#pragma once

#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dxmt::test {

struct TextureReadback {
  std::vector<std::uint8_t> data;
  UINT64 row_pitch = 0;
  UINT width = 0;
  UINT height = 0;
};

class D3D12TestContext {
public:
  D3D12TestContext() = default;
  ~D3D12TestContext();

  D3D12TestContext(const D3D12TestContext &) = delete;
  D3D12TestContext &operator=(const D3D12TestContext &) = delete;

  HRESULT Initialize();
  HRESULT Initialize(ID3D12Device *device);
  HRESULT ResetCommandList();
  HRESULT ExecuteAndWait();
  HRESULT SignalAndWait();
  HRESULT WaitForFence(ID3D12Fence *fence, UINT64 value);

  ComPtr<ID3D12Resource> CreateBuffer(UINT64 size, D3D12_HEAP_TYPE heap_type,
                                      D3D12_RESOURCE_FLAGS flags,
                                      D3D12_RESOURCE_STATES state) const;
  ComPtr<ID3D12Resource> CreateUploadBuffer(UINT64 size,
                                            const void *data = nullptr,
                                            std::size_t data_size = 0) const;
  ComPtr<ID3D12Resource> CreateTexture2D(UINT64 width, UINT height,
                                         UINT16 mip_levels, DXGI_FORMAT format,
                                         D3D12_RESOURCE_FLAGS flags,
                                         D3D12_RESOURCE_STATES state) const;
  ComPtr<ID3D12DescriptorHeap>
  CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count,
                       bool shader_visible) const;
  ComPtr<ID3D12RootSignature>
  CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC &desc) const;
  ComPtr<ID3D12PipelineState>
  CreateComputePipeline(ID3D12RootSignature *root_signature,
                        D3D12_SHADER_BYTECODE shader) const;
  ComPtr<ID3D12PipelineState>
  CreateGraphicsPipeline(ID3D12RootSignature *root_signature,
                         DXGI_FORMAT render_target_format,
                         D3D12_SHADER_BYTECODE pixel_shader) const;

  HRESULT UploadTextureAndReset(ID3D12Resource *texture, const void *data,
                                UINT64 row_pitch, UINT64 slice_pitch,
                                UINT subresource = 0);
  HRESULT ReadbackTexture(ID3D12Resource *texture, TextureReadback *readback,
                          UINT subresource = 0);
  HRESULT ReadbackBuffer(ID3D12Resource *buffer, UINT64 size,
                         std::vector<std::uint8_t> *data);

  D3D12_CPU_DESCRIPTOR_HANDLE
  CpuDescriptorHandle(ID3D12DescriptorHeap *heap, UINT index) const;
  D3D12_GPU_DESCRIPTOR_HANDLE
  GpuDescriptorHandle(ID3D12DescriptorHeap *heap, UINT index) const;

  static void Transition(ID3D12GraphicsCommandList *list,
                         ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                         D3D12_RESOURCE_STATES after);
  static void UavBarrier(ID3D12GraphicsCommandList *list,
                         ID3D12Resource *resource);

  ID3D12Device *device() const { return device_.get(); }
  ID3D12CommandQueue *queue() const { return queue_.get(); }
  ID3D12CommandAllocator *allocator() const { return allocator_.get(); }
  ID3D12GraphicsCommandList *list() const { return list_.get(); }

private:
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<ID3D12CommandAllocator> allocator_;
  ComPtr<ID3D12GraphicsCommandList> list_;
  ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  UINT64 fence_value_ = 0;
};

bool ColorsMatch(std::uint32_t actual, std::uint32_t expected,
                 unsigned int max_channel_difference);

ComPtr<ID3D12Device> CreateIsolatedD3D12Device();

} // namespace dxmt::test
