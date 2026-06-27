#include "d3d12_descriptor_mirror.hpp"

#include "dxmt_sampler.hpp"
#include "log/log.hpp"
#include <cstring>

namespace dxmt::d3d12 {

DescriptorHeapMirror::DescriptorHeapMirror(WMT::Device device, uint32_t num_descriptors, bool sampler_heap)
    : num_descriptors_(num_descriptors), sampler_heap_(sampler_heap) {
  const uint32_t plane_count = sampler_heap_ ? 3 : 2;
  const uint64_t length =
      uint64_t(num_descriptors_) * plane_count * sizeof(uint64_t);

  WMTBufferInfo info = {};
  info.length = length ? length : sizeof(uint64_t);
  info.options = static_cast<WMTResourceOptions>(
      WMTResourceStorageModeShared | WMTResourceHazardTrackingModeUntracked);
  info.memory.set(nullptr);
  buffer_ = device.newBuffer(info);
  if (!buffer_) {
    ERR("DescriptorHeapMirror: failed to allocate mirror buffer length=", info.length,
        " sampler=", sampler_heap_);
    return;
  }
  gpu_address_ = info.gpu_address;
  mapped_ = reinterpret_cast<uint64_t *>(info.memory.get_accessible_or_null());
  if (!mapped_) {
    ERR("DescriptorHeapMirror: mirror buffer is not CPU-accessible length=", info.length);
    return;
  }
  std::memset(mapped_, 0, info.length);

  stale_generation_.assign(num_descriptors_, 0);
  filled_generation_.assign(num_descriptors_, UINT64_MAX);
}

void
DescriptorHeapMirror::FillSamplerSlot(uint32_t index, const Sampler *sampler, uint64_t dummy_handle) {
  auto *handle = samplerHandlePtr(index);
  auto *cube = samplerCubeHandlePtr(index);
  auto *lod_bias = samplerLodBiasPtr(index);
  if (!handle || !cube || !lod_bias)
    return;
  uint64_t encoded[kMirrorSamplerQwords] = {};
  if (sampler)
    EncodeMirrorSamplerSlot(encoded, *sampler);
  else
    EncodeMirrorSamplerSlotNull(encoded, dummy_handle);
  *handle = encoded[0];
  *cube = encoded[1];
  *lod_bias = encoded[2];
  if (index < filled_generation_.size())
    filled_generation_[index] = stale_generation_[index];
}

void
DescriptorHeapMirror::FillTextureSlot(uint32_t index, uint64_t gpu_resource_id, uint32_t array_length) {
  auto *handle = textureHandlePtr(index);
  auto *metadata = textureMetadataPtr(index);
  if (!handle || !metadata)
    return;
  uint64_t encoded[kMirrorTextureQwords] = {};
  EncodeMirrorTextureSlot(encoded, gpu_resource_id, array_length);
  *handle = encoded[0];
  *metadata = encoded[1];
  if (index < filled_generation_.size())
    filled_generation_[index] = stale_generation_[index];
}

void
DescriptorHeapMirror::FillTextureSlotPayload(uint32_t index, uint64_t handle_payload, uint64_t metadata_payload) {
  auto *handle = textureHandlePtr(index);
  auto *metadata = textureMetadataPtr(index);
  if (!handle || !metadata)
    return;
  *handle = handle_payload;
  *metadata = metadata_payload;
  if (index < filled_generation_.size())
    filled_generation_[index] = stale_generation_[index];
}

void
DescriptorHeapMirror::ClearTextureSlot(uint32_t index) {
  auto *handle = textureHandlePtr(index);
  auto *metadata = textureMetadataPtr(index);
  if (!handle || !metadata)
    return;
  *handle = 0;
  *metadata = 0;
  if (index < filled_generation_.size())
    filled_generation_[index] = stale_generation_[index];
}

bool
DescriptorHeapMirror::MarkSlotStale(uint32_t index, uint64_t content_generation) {
  if (index >= stale_generation_.size())
    return false;
  stale_generation_[index] = content_generation;
  return true;
}

} // namespace dxmt::d3d12
