#pragma once

#include "Metal.hpp"
#include "winemetal.h"
#include "dxmt_descriptor_mirror.hpp"
#include <cstdint>
#include <vector>

namespace dxmt {
class Sampler;
}

namespace dxmt::d3d12 {

/**
 * Persistent typed descriptor mirror for ONE shader-visible D3D12 descriptor heap
 * (bindless-mirror Stage-1, sub-step ②). See fh4-debug/BINDLESS-ABI.md.
 *
 * A D3D12 descriptor heap has a single type, so each shader-visible heap backs exactly
 * one mirror array:
 *   - CBV_SRV_UAV heap -> a TEXTURE mirror: [NumDescriptors x kMirrorTextureQwords] u64,
 *     element = (gpuResourceID, TextureMetadata). Buffers (CBV/SRV/UAV buffer) are NOT
 *     stored here (hybrid ABI: their gpuAddress churns per-draw via ring-buffer
 *     sub-allocation, so they go to a per-draw buffer table owned by sub-step ③).
 *   - SAMPLER heap -> a SAMPLER mirror: [NumDescriptors x kMirrorSamplerQwords] u64,
 *     element = (handle, cube_handle, lod_bias_bits).
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
 *   - Texture slots CANNOT be resolved on the app thread (texture->current()->
 *     gpuResourceID is only safe on the dxmt-encode-thread). CreateShaderResourceView /
 *     CopyDescriptors only MARK the slot stale (bump its generation); the actual
 *     resolve+write happens on the encode thread via FillTextureSlot() — wired by ③.
 *
 * This whole type is dormant behind DXMT_BINDLESS_MIRROR; legacy runs never allocate it.
 */
class DescriptorHeapMirror {
public:
  DescriptorHeapMirror(WMT::Device device, uint32_t num_descriptors, bool sampler_heap);

  /** GPU virtual address of the mirror buffer (for binding at slot 30 by ③). */
  uint64_t gpuAddress() const { return gpu_address_; }

  /** The Metal buffer handle (for residency / binding by ③). */
  WMT::Buffer buffer() const { return buffer_; }

  uint32_t numDescriptors() const { return num_descriptors_; }
  bool isSamplerHeap() const { return sampler_heap_; }

  /** Per-slot qword stride of this mirror's array. */
  uint32_t qwordStride() const {
    return sampler_heap_ ? kMirrorSamplerQwords : kMirrorTextureQwords;
  }

  /** Pointer to the first qword of slot `index` in the CPU-mapped mirror, or null. */
  uint64_t *slotPtr(uint32_t index) {
    if (!mapped_ || index >= num_descriptors_)
      return nullptr;
    return mapped_ + (uint64_t)index * qwordStride();
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
   * record there). Byte-identical via the shared writer.
   */
  void FillTextureSlot(uint32_t index, uint64_t gpu_resource_id, uint32_t array_length);

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

private:
  WMT::Reference<WMT::Buffer> buffer_;
  uint64_t *mapped_ = nullptr;
  uint64_t gpu_address_ = 0;
  uint32_t num_descriptors_ = 0;
  bool sampler_heap_ = false;
  // Per-slot generation bookkeeping. stale = bumped by Create*View (app thread);
  // filled = updated when the encode thread writes the slot. A slot needs re-resolve
  // when stale_generation_[i] != filled_generation_[i].
  std::vector<uint64_t> stale_generation_;
  std::vector<uint64_t> filled_generation_;
};

} // namespace dxmt::d3d12
