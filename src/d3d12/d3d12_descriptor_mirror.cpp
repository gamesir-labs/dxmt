#include "d3d12_descriptor_mirror.hpp"

#include "dxmt_sampler.hpp"
#include "dxmt_texture.hpp"
#include "log/log.hpp"
#include <algorithm>
#include <bit>
#include <cstring>

namespace dxmt::d3d12 {
namespace {

constexpr uint32_t kIRDescriptorHeapBindPoint = 0;
constexpr uint32_t kIRSamplerHeapBindPoint = 1;
constexpr uint32_t kDescriptorTableEntryQwords = 3;
constexpr uint64_t kDescriptorTableTypedBufferBit = 1ull << 63;

uint64_t
BufferDescriptorMetadata(uint64_t size, bool typed, uint32_t texture_view_offset) {
  return std::min<uint64_t>(size, UINT32_MAX) |
         (uint64_t(texture_view_offset & 0xffu) << 32) |
         (typed ? kDescriptorTableTypedBufferBit : 0);
}

uint64_t
FloatMetadata(float value) {
  return uint64_t(std::bit_cast<uint32_t>(value));
}

} // namespace

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

  WMTBufferInfo table_info = {};
  table_info.length = num_descriptors_
                          ? uint64_t(num_descriptors_) *
                                kDescriptorTableEntryQwords * sizeof(uint64_t)
                          : sizeof(DescriptorTableEntry);
  table_info.options = static_cast<WMTResourceOptions>(
      WMTResourceStorageModeShared | WMTResourceHazardTrackingModeUntracked);
  table_info.memory.set(nullptr);
  table_buffer_ = device.newBuffer(table_info);
  if (!table_buffer_) {
    ERR("DescriptorHeapMirror: failed to allocate descriptor table buffer length=",
        table_info.length, " sampler=", sampler_heap_);
  } else {
    table_gpu_address_ = table_info.gpu_address;
    table_mapped_ =
        reinterpret_cast<DescriptorTableEntry *>(table_info.memory.get_accessible_or_null());
    if (!table_mapped_) {
      ERR("DescriptorHeapMirror: descriptor table buffer is not CPU-accessible length=",
          table_info.length);
    } else {
      auto *qwords = reinterpret_cast<uint64_t *>(table_mapped_);
      std::fill(qwords, qwords + table_info.length / sizeof(uint64_t), 0);
    }
  }
  table_entries_.assign(num_descriptors_, {});
  residency_targets_.resize(num_descriptors_);

  WMTArgumentTableInfo table = {};
  table.max_buffer_bind_count =
      sampler_heap_ ? kIRSamplerHeapBindPoint + 1 : kIRDescriptorHeapBindPoint + 1;
  table.max_texture_bind_count = 0;
  table.max_sampler_state_bind_count = 0;
  table.initialize_bindings = true;
  argument_table_ = device.newArgumentTable(table);
  if (!argument_table_) {
    ERR("DescriptorHeapMirror: failed to allocate Metal4 argument table sampler=",
        sampler_heap_);
  } else if (table_gpu_address_) {
    argument_table_.setAddress(
        table_gpu_address_,
        sampler_heap_ ? kIRSamplerHeapBindPoint : kIRDescriptorHeapBindPoint);
  }

  if (!sampler_heap_) {
    WMTTextureViewPoolInfo pool_info = {};
    pool_info.initial_count = num_descriptors_;
    WMT::Error error;
    texture_view_pool_ = device.newTextureViewPool(pool_info, error);
    if (!texture_view_pool_) {
      ERR("DescriptorHeapMirror: failed to allocate texture view pool descriptors=",
          num_descriptors_);
    } else {
      texture_view_pool_base_resource_id_ =
          texture_view_pool_.baseResourceID();
      if (!texture_view_pool_base_resource_id_)
        ERR("DescriptorHeapMirror: texture view pool returned null baseResourceID descriptors=",
            num_descriptors_);
    }
  }

  stale_generation_.assign(num_descriptors_, 0);
  filled_generation_.assign(num_descriptors_, UINT64_MAX);
}

void
DescriptorHeapMirror::WriteTableEntry(uint32_t index, const DescriptorTableEntry &entry) {
  if (index >= table_entries_.size())
    return;
  table_entries_[index] = entry;
  if (table_mapped_)
    table_mapped_[index] = entry;
}

void
DescriptorHeapMirror::WriteNullTableEntry(uint32_t index) {
  WriteTableEntry(index, {});
}

void
DescriptorHeapMirror::WriteBufferTableEntry(uint32_t index, uint64_t gpu_va,
                                            uint64_t size, bool typed,
                                            uint32_t texture_view_offset) {
  WriteTableEntry(index, {gpu_va, 0,
                          BufferDescriptorMetadata(size, typed,
                                                   texture_view_offset)});
}

void
DescriptorHeapMirror::WriteTextureTableEntry(uint32_t index,
                                             uint64_t gpu_resource_id,
                                             float min_lod) {
  WriteTableEntry(index, {0, gpu_resource_id, FloatMetadata(min_lod)});
}

void
DescriptorHeapMirror::WriteTexturePoolTableEntry(uint32_t index,
                                                 float min_lod) {
  WriteTextureTableEntry(index, textureViewPoolSlotResourceID(index), min_lod);
}

void
DescriptorHeapMirror::WriteSamplerTableEntry(uint32_t index,
                                             const Sampler *sampler) {
  const uint64_t sampler_handle = sampler ? sampler->sampler_state_handle : 0;
  const float lod_bias = sampler ? sampler->lod_bias : 0.0f;
  WriteTableEntry(index, {sampler_handle, 0, FloatMetadata(lod_bias)});
}

uint64_t
DescriptorHeapMirror::SetTexturePoolSlot(uint32_t index, Texture *texture,
                                         TextureViewKey view,
                                         TextureAllocation *allocation) {
  if (sampler_heap_ || !texture_view_pool_ || index >= num_descriptors_ ||
      !texture || !allocation)
    return 0;
  return texture->setViewPoolSlot(texture_view_pool_, view, allocation, index);
}

uint64_t
DescriptorHeapMirror::CopyTexturePoolSlotFrom(uint32_t dst_index,
                                              const DescriptorHeapMirror &src,
                                              uint32_t src_index) {
  if (sampler_heap_ || src.sampler_heap_ || !texture_view_pool_ ||
      !src.texture_view_pool_ || dst_index >= num_descriptors_ ||
      src_index >= src.num_descriptors_)
    return 0;
  return texture_view_pool_.copyResourceViews(src.texture_view_pool_,
                                              src_index, 1, dst_index);
}

DescriptorResidencyTarget
DescriptorHeapMirror::ReplaceResidencyTarget(
    uint32_t index, DescriptorResidencyTarget target) {
  if (index >= residency_targets_.size())
    return {};
  auto previous = std::move(residency_targets_[index]);
  residency_targets_[index] = std::move(target);
  return previous;
}

std::vector<DescriptorResidencyTarget>
DescriptorHeapMirror::DrainResidencyTargets() {
  std::vector<DescriptorResidencyTarget> drained;
  drained.swap(residency_targets_);
  return drained;
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
