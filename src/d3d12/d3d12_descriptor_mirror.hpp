#pragma once

#include "Metal.hpp"
#include "winemetal.h"
#include "dxmt_descriptor_mirror.hpp"
#include "rc/util_rc_ptr.hpp"
#include <cstdint>
#include <vector>

namespace dxmt {
class Sampler;
class Texture;
class TextureAllocation;
struct TextureViewKey;
}

namespace dxmt::d3d12 {

enum class DescriptorArgumentTableKind {
  Resource,
  Sampler,
};

struct DescriptorTableEntry {
  uint64_t gpu_va = 0;
  uint64_t texture_view_id = 0;
  uint64_t metadata = 0;
};

static_assert(sizeof(DescriptorTableEntry) == sizeof(uint64_t) * 3);

struct DescriptorResidencyTarget {
  WMT::Reference<WMT::Resource> allocation;
  WMT::Reference<WMT::Resource> secondary_allocation;
  Rc<dxmt::Sampler> sampler;
};

/**
 * Persistent descriptor mirror for one shader-visible D3D12 descriptor heap.
 *
 * A D3D12 descriptor heap has a single type, so each shader-visible heap backs exactly
 * one mirror array:
 *   - CBV_SRV_UAV heap -> a planar TEXTURE mirror: typed texture handles first,
 *     followed by uint64 metadata. Buffers (CBV/SRV/UAV buffer) are NOT stored
 *     here (hybrid ABI: their gpuAddress churns per-draw via ring-buffer
 *     sub-allocation, so they go to a per-draw buffer table owned by sub-step ③).
 *   - SAMPLER heap -> a planar SAMPLER mirror: typed sampler handles, cube
 *     sampler handles, then uint64 lod-bias metadata.
 *
 * The buffer is Metal shared-storage so the CPU writes the payload and the GPU reads it
 * indirectly as an argument buffer (bound at slot 30 by sub-step ③). It is sized to the
 * heap's real NumDescriptors (NOT kBindlessMirrorCapacity, which is an AIR GEP-typing
 * artifact only).
 *
 * THREADING (see [[bindless-mirror-texture-fill-needs-encode-thread]]):
 *   - Sampler slots are filled synchronously at CreateSampler/CopyDescriptors on the
 *     app thread (Sampler handles are a pure function of the D3D12_SAMPLER_DESC and are
 *     immutable once created).
 *   - Texture slots CANNOT be resolved for the DXBC mirror on the app thread
 *     (texture->current()->gpuResourceID is only safe on the dxmt-encode-thread).
 *     CreateShaderResourceView / CopyDescriptors mark the slot stale; the
 *     resolve+write happens on the encode thread via FillTextureSlot().
 *     The Metal4 descriptor-table entry is materialized separately at descriptor
 *     write time and must not be overwritten by the DXBC mirror fill.
 *
 * The runtime allocates this for shader-visible CBV/SRV/UAV and SAMPLER heaps.
 * Unsupported shader paths may still fall back to legacy bindings, but descriptor
 * heap mirroring itself is not selected by an environment variable.
 */
class DescriptorHeapMirror {
public:
  DescriptorHeapMirror(WMT::Device device, uint32_t num_descriptors, bool sampler_heap);

  /** GPU virtual address of the mirror buffer (for binding at slot 30 by ③). */
  uint64_t gpuAddress() const { return gpu_address_; }

  /** The Metal buffer handle (for residency / binding by ③). */
  WMT::Buffer buffer() const { return buffer_; }

  WMT::ArgumentTable argumentTable() const { return argument_table_; }
  WMT::TextureViewPool textureViewPool() const { return texture_view_pool_; }
  uint64_t textureViewPoolBaseResourceID() const {
    return texture_view_pool_base_resource_id_;
  }
  uint64_t textureViewPoolSlotResourceID(uint32_t index) const {
    return texture_view_pool_base_resource_id_ && index < num_descriptors_
               ? texture_view_pool_base_resource_id_ + index
               : 0;
  }

  uint32_t argumentTableBindPoint() const { return sampler_heap_ ? 1 : 0; }

  uint64_t descriptorTableGpuAddress() const { return table_gpu_address_; }

  WMT::Buffer descriptorTableBuffer() const { return table_buffer_; }

  uint32_t numDescriptors() const { return num_descriptors_; }
  bool isSamplerHeap() const { return sampler_heap_; }

  DescriptorArgumentTableKind argumentTableKind() const {
    return sampler_heap_ ? DescriptorArgumentTableKind::Sampler
                         : DescriptorArgumentTableKind::Resource;
  }

  const DescriptorTableEntry *descriptorTableEntry(uint32_t index) const {
    return index < table_entries_.size() ? &table_entries_[index] : nullptr;
  }

  void WriteNullTableEntry(uint32_t index);
  void WriteBufferTableEntry(uint32_t index, uint64_t gpu_va, uint64_t size,
                             bool typed, uint32_t texture_view_offset = 0);
  void WriteTextureTableEntry(uint32_t index, uint64_t gpu_resource_id,
                              float min_lod = 0.0f);
  void WriteTexturePoolTableEntry(uint32_t index, float min_lod = 0.0f);
  void WriteSamplerTableEntry(uint32_t index, const Sampler *sampler);
  uint64_t SetTexturePoolSlot(uint32_t index, dxmt::Texture *texture,
                              dxmt::TextureViewKey view,
                              dxmt::TextureAllocation *allocation);
  uint64_t CopyTexturePoolSlotFrom(uint32_t dst_index,
                                   const DescriptorHeapMirror &src,
                                   uint32_t src_index);

  DescriptorResidencyTarget ReplaceResidencyTarget(
      uint32_t index, DescriptorResidencyTarget target);
  std::vector<DescriptorResidencyTarget> DrainResidencyTargets();
  const DescriptorResidencyTarget *residencyTarget(uint32_t index) const {
    return index < residency_targets_.size() ? &residency_targets_[index]
                                             : nullptr;
  }

  /** Pointer to a texture slot's handle qword. */
  uint64_t *textureHandlePtr(uint32_t index) {
    if (sampler_heap_ || !mapped_ || index >= num_descriptors_)
      return nullptr;
    return mapped_ + index;
  }

  /** Pointer to a texture slot's metadata qword. */
  uint64_t *textureMetadataPtr(uint32_t index) {
    if (sampler_heap_ || !mapped_ || index >= num_descriptors_)
      return nullptr;
    return mapped_ + num_descriptors_ + index;
  }

  /** Pointer to a sampler slot's primary sampler qword. */
  uint64_t *samplerHandlePtr(uint32_t index) {
    if (!sampler_heap_ || !mapped_ || index >= num_descriptors_)
      return nullptr;
    return mapped_ + index;
  }

  /** Pointer to a sampler slot's cube sampler qword. */
  uint64_t *samplerCubeHandlePtr(uint32_t index) {
    if (!sampler_heap_ || !mapped_ || index >= num_descriptors_)
      return nullptr;
    return mapped_ + num_descriptors_ + index;
  }

  /** Pointer to a sampler slot's lod-bias qword. */
  uint64_t *samplerLodBiasPtr(uint32_t index) {
    if (!mapped_ || index >= num_descriptors_)
      return nullptr;
    return mapped_ + uint64_t(num_descriptors_) * 2 + index;
  }

  /**
   * Fill a SAMPLER slot synchronously (app-thread safe). `sampler` may be null, in which
   * case the dummy/null sampler payload is written. Byte-identical to encodeShader
   * resources via the shared writer.
   */
  void FillSamplerSlot(uint32_t index, const Sampler *sampler, uint64_t dummy_handle);

  /**
   * Fill a TEXTURE slot with an already-resolved (gpuResourceID, arrayLength). MUST be
   * called on the encode thread (the caller resolves the handle from the bound heap's
   * record there). Byte-identical to the legacy DXBC resource writer.
   */
  void FillTextureSlot(uint32_t index, uint64_t gpu_resource_id, uint32_t array_length);

  /** Fill a TEXTURE slot with an already-encoded handle/metadata pair. */
  void FillTextureSlotPayload(uint32_t index, uint64_t handle, uint64_t metadata);

  /** Clear a TEXTURE slot and mark the current stale generation as handled. */
  void ClearTextureSlot(uint32_t index);

  /**
   * Mark a slot stale (app thread). The slot's content generation is recorded so the
   * encode-thread fill can decide whether a re-resolve is needed. Applies to both
   * texture and sampler mirrors (it is pure per-slot generation bookkeeping). Returns
   * false if out of range. Lightweight; no Metal access.
   */
  bool MarkSlotStale(uint32_t index, uint64_t content_generation);

  /** Content generation last FILLED into a slot (encode thread updates it). */
  uint64_t slotFilledGeneration(uint32_t index) const {
    return index < filled_generation_.size() ? filled_generation_[index] : 0;
  }

  /** Content generation last MARKED stale for a slot (app thread). */
  uint64_t slotStaleGeneration(uint32_t index) const {
    return index < stale_generation_.size() ? stale_generation_[index] : 0;
  }

  bool SlotNeedsFill(uint32_t index) const {
    return index < stale_generation_.size() &&
           stale_generation_[index] != filled_generation_[index];
  }

private:
  void WriteTableEntry(uint32_t index, const DescriptorTableEntry &entry);

  WMT::Reference<WMT::Buffer> buffer_;
  WMT::Reference<WMT::Buffer> table_buffer_;
  WMT::Reference<WMT::ArgumentTable> argument_table_;
  WMT::Reference<WMT::TextureViewPool> texture_view_pool_;
  uint64_t *mapped_ = nullptr;
  DescriptorTableEntry *table_mapped_ = nullptr;
  uint64_t gpu_address_ = 0;
  uint64_t table_gpu_address_ = 0;
  uint64_t texture_view_pool_base_resource_id_ = 0;
  uint32_t num_descriptors_ = 0;
  bool sampler_heap_ = false;
  std::vector<DescriptorTableEntry> table_entries_;
  std::vector<DescriptorResidencyTarget> residency_targets_;
  // Per-slot generation bookkeeping. stale = bumped by Create*View (app thread);
  // filled = updated when the encode thread writes the slot. A slot needs re-resolve
  // when stale_generation_[i] != filled_generation_[i].
  std::vector<uint64_t> stale_generation_;
  std::vector<uint64_t> filled_generation_;
};

} // namespace dxmt::d3d12
