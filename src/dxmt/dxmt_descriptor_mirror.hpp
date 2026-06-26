#pragma once

#include "dxmt_sampler.hpp"
#include <bit>
#include <cstdint>

/**
 * Bindless-mirror (Stage-1) shared per-slot descriptor encoding.
 *
 * The bindless-mirror rearchitecture (see fh4-debug/BINDLESS-ABI.md) replaces the
 * per-draw packed argument buffer with a PERSISTENT typed mirror indexed by absolute
 * heap slot. For the persistent mirror to be byte-identical to the legacy per-draw
 * encode (ArgumentEncodingContext::encodeShaderResources, dxmt_context.cpp), the
 * per-slot payload writer must be SHARED between the two paths. These inline writers
 * are that single source of truth.
 *
 * HYBRID ABI (see [[bindless-mirror-buffer-gpuaddr-not-static]]): only TEXTURES and
 * SAMPLERS are persisted in the mirror. Their payloads are stable across draws (they
 * do not go through the ring-buffer per-draw sub-allocation that makes buffer
 * gpuAddress churn). BUFFERS (CBV + SRV/UAV buffer) are NOT mirrored here; they go to
 * a small per-draw buffer table handled by sub-step ③. Consequently there is no
 * buffer writer in this header by design.
 *
 * The qword layout below mirrors exactly what encodeShaderResources writes into
 * typed Metal argument-buffer entries via arg.StructurePtrOffset:
 *   - texture: [0]=gpuResourceID, [1]=TextureMetadata(arrayLength, minLOD=0)
 *   - sampler: [0]=sampler_state_handle, [1]=sampler_state_cube_handle,
 *              [2]=lod_bias (f32 bits in low 32)
 */

namespace dxmt {

/**
 * TextureMetadata packs the texture array length and min-LOD clamp into one qword,
 * exactly as the legacy encoder does (dxmt_context.cpp). Kept here so both the encode
 * path and the mirror-fill path produce identical bytes.
 */
inline uint64_t
MirrorTextureMetadata(uint32_t array_length, float min_lod) {
  return ((uint64_t)array_length << 32) | (uint64_t)std::bit_cast<uint32_t>(min_lod);
}

/**
 * Write a texture descriptor's two-qword payload (handle + metadata) to dst.
 * dst points at the first qword of the slot (== encoded_buffer + StructurePtrOffset
 * in the legacy path, or mirror.textures + slot*kMirrorTextureQwords in the mirror).
 */
inline void
EncodeMirrorTextureSlot(uint64_t *dst, uint64_t gpu_resource_id, uint32_t array_length) {
  dst[0] = gpu_resource_id;
  dst[1] = MirrorTextureMetadata(array_length, 0);
}

/**
 * Write a sampler descriptor's three-qword payload to dst (handle, cube handle,
 * lod_bias bits). Identical to the legacy sampler branch in encodeShaderResources.
 */
inline void
EncodeMirrorSamplerSlot(uint64_t *dst, const Sampler &sampler) {
  dst[0] = sampler.sampler_state_handle;
  dst[1] = sampler.sampler_state_cube_handle;
  dst[2] = (uint64_t)std::bit_cast<uint32_t>(sampler.lod_bias);
}

/** Null/dummy sampler payload (no sampler bound) — matches the legacy null branch. */
inline void
EncodeMirrorSamplerSlotNull(uint64_t *dst, uint64_t dummy_handle) {
  dst[0] = dummy_handle;
  dst[1] = dummy_handle;
  dst[2] = (uint64_t)std::bit_cast<uint32_t>(0.0f);
}

/** Per-slot qword strides of the typed mirror arrays. */
static constexpr uint32_t kMirrorTextureQwords = 2; // handle, meta
static constexpr uint32_t kMirrorSamplerQwords = 3; // handle, cube_handle, lod_bias

} // namespace dxmt
