#pragma once

#include "d3d12_resource.hpp"
#include "d3d12_device.hpp"
#include "com/com_pointer.hpp"
#include "rc/util_rc_ptr.hpp"
#include <d3d12.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dxmt::d3d12 {

class DescriptorHeap;
class DescriptorHeapMirror;

} // namespace dxmt::d3d12

namespace dxmt {
class Sampler;
}

namespace dxmt::d3d12 {

/**
 * Whether the bindless-mirror path (Stage-1) is enabled (env DXMT_BINDLESS_MIRROR).
 * Cached. When false, no mirror buffers are ever allocated and the legacy packed
 * argument-buffer path is fully intact.
 */
bool IsBindlessMirrorEnabled();

enum class DescriptorRecordType {
  Empty,
  ConstantBufferView,
  ShaderResourceView,
  UnorderedAccessView,
  RenderTargetView,
  DepthStencilView,
  Sampler,
};

struct DescriptorRecord {
  static constexpr uint32_t kMagic = 0x44584d54;

  uint32_t magic = kMagic;
  D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
  bool shader_visible = false;
  DescriptorRecordType type = DescriptorRecordType::Empty;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = {};
  UINT heap_index = 0;
  UINT heap_count = 0;
  // Non-owning back-pointer to the owning heap's bindless-mirror (or nullptr when the
  // mirror is disabled / heap not shader-visible). The heap owns the mirror and the
  // records, so this is lifetime-safe. Part of the heap-identity set preserved across
  // ResetDescriptorRecord / CopyDescriptorRecord (a slot keeps its own heap's mirror).
  DescriptorHeapMirror *mirror = nullptr;
  Com<ID3D12Resource> resource;
  Com<ID3D12Resource> counter_resource;
  // Descriptor-write-time sampler materialization stores resource IDs in the
  // mirror buffer, but those IDs are only valid while the Metal sampler objects
  // remain alive. Keep the object with the descriptor slot until overwritten.
  Rc<dxmt::Sampler> materialized_sampler;
  union {
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
    D3D12_RENDER_TARGET_VIEW_DESC rtv;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv;
    D3D12_SAMPLER_DESC sampler;
  } desc = {};
  bool has_desc = false;
};

class DescriptorHeap {
public:
  virtual const D3D12_DESCRIPTOR_HEAP_DESC &GetDescriptorHeapDesc() const = 0;
  virtual DescriptorRecord *GetDescriptorRecord(D3D12_CPU_DESCRIPTOR_HANDLE handle) = 0;
  virtual const DescriptorRecord *GetDescriptorRecord(D3D12_CPU_DESCRIPTOR_HANDLE handle) const = 0;
  virtual const DescriptorRecord *GetDescriptorRecord(D3D12_GPU_DESCRIPTOR_HANDLE handle) const = 0;
  /**
   * The persistent bindless-mirror buffer for this heap, or nullptr when bindless-mirror
   * is disabled or the heap is not shader-visible. Lazily allocated. Sub-step ③ binds
   * this at Metal slot 30 and fills texture slots on the encode thread; sub-step ② fills
   * sampler slots synchronously at descriptor-write time.
   */
  virtual DescriptorHeapMirror *GetMirror() = 0;
};

Com<ID3D12DescriptorHeap>
CreateDescriptorHeap(IMTLD3D12Device *device,
                     const D3D12_DESCRIPTOR_HEAP_DESC *desc);

DescriptorRecord *GetDescriptorRecordFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle);
DescriptorRecord *GetDescriptorRecordFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                                   D3D12_DESCRIPTOR_HEAP_TYPE expected_type);
DescriptorRecord *GetDescriptorRecordRangeFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                                        D3D12_DESCRIPTOR_HEAP_TYPE expected_type,
                                                        UINT descriptor_count,
                                                        const char *context);
const DescriptorRecord *GetDescriptorRecordFromGpuHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle,
                                                         D3D12_DESCRIPTOR_HEAP_TYPE expected_type);

void BumpDescriptorContentGeneration();
uint64_t GetDescriptorContentGeneration();

} // namespace dxmt::d3d12
