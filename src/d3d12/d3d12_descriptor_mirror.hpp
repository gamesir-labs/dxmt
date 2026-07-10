#pragma once

#include "Metal.hpp"
#include "winemetal.h"
#include "dxmt_descriptor_mirror.hpp"
#include "rc/util_rc_ptr.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dxmt {
class Sampler;
class Texture;
class TextureAllocation;
struct TextureViewKey;
}

namespace dxmt::d3d12 {

enum class DescriptorBackendSlotKind : uint32_t {
  Empty,
  Buffer,
  Texture,
  Sampler,
};

enum BufferDescriptorRecordFlag : uint32_t {
  BufferDescriptorRecordFlagValid = 1u << 0,
  BufferDescriptorRecordFlagCBV = 1u << 1,
  BufferDescriptorRecordFlagSRV = 1u << 2,
  BufferDescriptorRecordFlagUAV = 1u << 3,
  BufferDescriptorRecordFlagRaw = 1u << 4,
  BufferDescriptorRecordFlagTyped = 1u << 5,
  BufferDescriptorRecordFlagStructured = 1u << 6,
  BufferDescriptorRecordFlagCounter = 1u << 7,
  BufferDescriptorRecordFlagTextureView = 1u << 8,
};

inline constexpr uint32_t kNullDescriptorResourceIndex = 0;

struct DescriptorTableEntry {
  uint64_t gpu_va = 0;
  uint64_t texture_view_id = 0;
  uint64_t metadata = 0;
};

static_assert(sizeof(DescriptorTableEntry) == sizeof(uint64_t) * 3);

struct BufferDescriptorRecord {
  uint32_t resource_index = kNullDescriptorResourceIndex;
  uint32_t flags = 0;
  uint64_t byte_offset = 0;
  uint64_t byte_size = 0;
  uint32_t stride = 0;
  uint32_t format = 0;
  uint32_t counter_resource_index = kNullDescriptorResourceIndex;
  uint64_t counter_offset = 0;
};

static_assert(sizeof(BufferDescriptorRecord) == 48);

struct BufferResourceTableEntry {
  uint64_t gpu_address = 0;
  uint64_t byte_size = 0;
  uint64_t allocation_handle = 0;
  uint64_t generation = 0;
};

static_assert(sizeof(BufferResourceTableEntry) == 32);

struct DescriptorBackendResourceRecord {
  WMT::Reference<WMT::Resource> allocation;
  uint64_t resource_identity = 0;
  uint64_t gpu_address = 0;
  uint64_t byte_size = 0;
  uint64_t allocation_handle = 0;
  uint64_t generation = 0;
};

struct DescriptorSlotMeta {
  DescriptorBackendSlotKind kind = DescriptorBackendSlotKind::Empty;
  uint32_t flags = 0;
  uint64_t generation = 0;
};

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
 *   - CBV_SRV_UAV heap -> a planar TEXTURE mirror for the legacy fallback plus
 *     the native three-qword descriptor table, buffer descriptor records, and
 *     resource table. Native buffer descriptors are materialized on write and
 *     resolve allocation identity through the resource table.
 *   - SAMPLER heap -> a planar SAMPLER mirror: typed sampler handles, cube
 *     sampler handles, then uint64 lod-bias metadata.
 *
 * The buffer is Metal shared-storage so the CPU writes the payload and the GPU reads it
 * indirectly as an argument buffer (bound at slot 30 by sub-step ③). It is sized to the
 * heap's real NumDescriptors (NOT kBindlessMirrorCapacity, which is an AIR GEP-typing
 * artifact only).
 *
 * THREADING:
 *   - Sampler slots are filled synchronously at CreateSampler/CopyDescriptors on the
 *     app thread (Sampler handles are a pure function of the D3D12_SAMPLER_DESC and are
 *     immutable once created).
 *   - Texture and texture-buffer SRV/UAV slots backed by a Metal4
 *     texture-view-pool descriptor are filled synchronously at descriptor
 *     write/copy time using the pool slot resource ID.
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

  WMT::TextureViewPool textureViewPool() const { return texture_view_pool_; }
  uint64_t textureViewPoolBaseResourceID() const {
    return texture_view_pool_base_resource_id_;
  }
  uint64_t textureViewPoolSlotResourceID(uint32_t index) const {
    return texture_view_pool_base_resource_id_ && index < num_descriptors_
               ? texture_view_pool_base_resource_id_ + index
               : 0;
  }

  uint64_t descriptorTableGpuAddress() const { return table_gpu_address_; }

  WMT::Buffer descriptorTableBuffer() const { return table_buffer_; }

  uint64_t bufferDescriptorRecordGpuAddress() const {
    return buffer_record_gpu_address_;
  }

  WMT::Buffer bufferDescriptorRecordBuffer() const {
    return buffer_record_buffer_;
  }

  uint64_t bufferResourceTableGpuAddress() const {
    return buffer_resource_table_gpu_address_;
  }

  WMT::Buffer bufferResourceTableBuffer() const {
    return buffer_resource_table_buffer_;
  }

  uint32_t numDescriptors() const { return num_descriptors_; }
  bool isSamplerHeap() const { return sampler_heap_; }

  bool descriptorTableBackendReady() const {
    if (!table_buffer_ || !table_gpu_address_)
      return false;
    return sampler_heap_ || texture_view_pool_base_resource_id_;
  }

  bool nativeDescriptorRecordStorageReady() const {
    return descriptorTableBackendReady() &&
           (sampler_heap_ || (buffer_record_buffer_ &&
                              buffer_record_gpu_address_ &&
                              buffer_record_mapped_ &&
                              buffer_resource_table_buffer_ &&
                              buffer_resource_table_gpu_address_ &&
                              buffer_resource_table_mapped_));
  }

  const DescriptorTableEntry *descriptorTableEntry(uint32_t index) const {
    return index < table_entries_.size() ? &table_entries_[index] : nullptr;
  }

  const BufferDescriptorRecord *bufferDescriptorRecord(uint32_t index) const {
    return buffer_record_mapped_ && index < num_descriptors_
               ? &buffer_record_mapped_[index]
               : nullptr;
  }

  const DescriptorSlotMeta *slotMeta(uint32_t index) const {
    return index < slot_meta_.size() ? &slot_meta_[index] : nullptr;
  }

  uint32_t RegisterBufferResource(uint64_t resource_identity,
                                  WMT::Resource allocation,
                                  uint64_t gpu_address,
                                  uint64_t byte_size);
  bool RefreshBufferResource(uint32_t resource_index,
                             WMT::Resource allocation,
                             uint64_t gpu_address,
                             uint64_t byte_size);
  const DescriptorBackendResourceRecord *
  backendResourceRecord(uint32_t index) const {
    return index < buffer_resources_.size() ? &buffer_resources_[index]
                                            : nullptr;
  }
  uint32_t backendResourceCount() const {
    return static_cast<uint32_t>(buffer_resources_.size());
  }
  uint64_t backendResourceTableGeneration() const {
    return buffer_resource_table_generation_;
  }

  void WriteNullTableEntry(uint32_t index);
  void WriteBufferTableEntry(uint32_t index, uint64_t gpu_va, uint64_t size,
                             bool typed, uint32_t texture_view_offset = 0);
  void WriteBufferTextureTableEntry(uint32_t index, uint64_t gpu_va,
                                    uint64_t size, uint64_t texture_view_id,
                                    uint32_t element_count,
                                    uint32_t first_element, uint32_t flags);
  void WriteBufferDescriptorRecord(uint32_t index,
                                   const BufferDescriptorRecord &record);
  void WriteTextureTableEntry(uint32_t index, uint64_t gpu_resource_id,
                              uint32_t array_length,
                              float min_lod = 0.0f);
  void WriteTexturePoolTableEntry(uint32_t index, uint32_t array_length,
                                  float min_lod = 0.0f);
  void WriteSamplerTableEntry(uint32_t index, const Sampler *sampler);
  uint64_t SetTexturePoolSlot(uint32_t index, dxmt::Texture *texture,
                              dxmt::TextureViewKey view,
                              dxmt::TextureAllocation *allocation);
  uint64_t SetTexturePoolBufferSlot(
      uint32_t index, WMT::Buffer buffer,
      const WMTTextureBufferViewDescriptor &descriptor, uint64_t offset,
      uint64_t bytes_per_row);
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
   * case the heap-owned null sampler payload is written. Byte-identical to encodeShader
   * resources via the shared writer.
   */
  void FillSamplerSlot(uint32_t index, const Sampler *sampler, uint64_t null_handle);

  /**
   * Fill a TEXTURE slot with an already-resolved (gpuResourceID, arrayLength, minLOD).
   * Texture pool resource IDs may be written on the app thread; typed buffer view
   * payloads are still resolved on the encode thread. Byte-identical to the legacy DXBC
   * resource writer.
   */
  void FillTextureSlot(uint32_t index, uint64_t gpu_resource_id,
                       uint32_t array_length, float min_lod = 0.0f);

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
  void WriteBufferResourceTableEntry(uint32_t index,
                                     const DescriptorBackendResourceRecord &record);
  void WriteSlotMeta(uint32_t index, DescriptorBackendSlotKind kind,
                     uint32_t flags = 0);

  WMT::Reference<WMT::Buffer> buffer_;
  WMT::Reference<WMT::Buffer> table_buffer_;
  WMT::Reference<WMT::Buffer> buffer_record_buffer_;
  WMT::Reference<WMT::Buffer> buffer_resource_table_buffer_;
  WMT::Reference<WMT::TextureViewPool> texture_view_pool_;
  WMT::Reference<WMT::SamplerState> null_sampler_;
  uint64_t *mapped_ = nullptr;
  DescriptorTableEntry *table_mapped_ = nullptr;
  BufferDescriptorRecord *buffer_record_mapped_ = nullptr;
  BufferResourceTableEntry *buffer_resource_table_mapped_ = nullptr;
  uint64_t gpu_address_ = 0;
  uint64_t table_gpu_address_ = 0;
  uint64_t buffer_record_gpu_address_ = 0;
  uint64_t buffer_resource_table_gpu_address_ = 0;
  uint64_t texture_view_pool_base_resource_id_ = 0;
  uint64_t null_sampler_handle_ = 0;
  uint64_t buffer_resource_table_generation_ = 0;
  uint32_t buffer_resource_table_capacity_ = 0;
  uint32_t num_descriptors_ = 0;
  bool sampler_heap_ = false;
  std::vector<DescriptorTableEntry> table_entries_;
  std::vector<DescriptorBackendResourceRecord> buffer_resources_;
  std::unordered_map<uint64_t, uint32_t> buffer_resource_indices_;
  std::vector<DescriptorSlotMeta> slot_meta_;
  std::vector<DescriptorResidencyTarget> residency_targets_;
  // Per-slot generation bookkeeping. stale = bumped by Create*View (app thread);
  // filled = updated when the encode thread writes the slot. A slot needs re-resolve
  // when stale_generation_[i] != filled_generation_[i].
  std::vector<uint64_t> stale_generation_;
  std::vector<uint64_t> filled_generation_;
};

} // namespace dxmt::d3d12
