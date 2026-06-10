#pragma once

#include "util_flags.hpp"
#include <array>
#include <chrono>

namespace dxmt {

using clock = std::chrono::high_resolution_clock;

enum class FeatureCompatibility {
    UnsupportedGeometryDraw,
    UnsupportedTessellationOutputPrimitive,
    UnsupportedIndirectTessellationDraw,
    UnsupportedGeometryTessellationDraw,
    UnsupportedDrawAuto,
    UnsupportedPredication,
    UnsupportedStreamOutputAppending,
    UnsupportedMultipleStreamOutput,
  };

enum class ScalerType {
  None,
  Spatial,
  Temporal,
};

struct ScalerInfo {
  ScalerType type = ScalerType::None;
  uint32_t input_width;
  uint32_t input_height;
  uint32_t output_width;
  uint32_t output_height;
  bool auto_exposure;
  bool motion_vector_highres;
};

struct FrameStatistics {
  clock::time_point begin_time{};
  Flags<FeatureCompatibility> compatibility_flags;
  uint32_t command_buffer_count = 0;
  uint32_t sync_count = 0;
  clock::duration sync_interval{};
  clock::duration commit_interval{};
  uint32_t render_pass_count = 0;
  uint32_t render_pass_optimized = 0;
  uint32_t render_command_count = 0;
  uint32_t render_pso_bind_count = 0;
  uint32_t render_draw_count = 0;
  uint32_t render_indexed_draw_count = 0;
  uint32_t render_indirect_draw_count = 0;
  uint32_t render_mesh_draw_count = 0;
  uint32_t render_tile_dispatch_count = 0;
  uint32_t render_color_load_count = 0;
  uint32_t render_color_store_count = 0;
  uint32_t render_depth_load_count = 0;
  uint32_t render_depth_store_count = 0;
  uint32_t render_stencil_load_count = 0;
  uint32_t render_stencil_store_count = 0;
  uint32_t render_attachment_count = 0;
  uint32_t render_attachment_reuse_count = 0;
  uint32_t render_attachment_reload_count = 0;
  uint32_t render_attachment_store_then_load_count = 0;
  uint32_t encoder_switch_render_compute_count = 0;
  uint32_t encoder_switch_compute_render_count = 0;
  uint32_t encoder_switch_to_other_count = 0;
  uint32_t present_pass_count = 0;
  uint32_t clear_pass_count = 0;
  uint32_t clear_pass_optimized = 0;
  uint32_t resolve_pass_optimized = 0;
  uint32_t compute_pass_count = 0;
  uint32_t compute_pass_with_dispatch_count = 0;
  uint32_t compute_pass_without_dispatch_count = 0;
  uint32_t compute_command_count = 0;
  uint32_t compute_pso_bind_count = 0;
  uint32_t compute_set_buffer_count = 0;
  uint32_t compute_set_texture_count = 0;
  uint32_t compute_set_bytes_count = 0;
  uint32_t compute_use_resource_count = 0;
  uint32_t compute_dispatch_count = 0;
  uint32_t compute_dispatch_indirect_count = 0;
  uint32_t compute_memory_barrier_count = 0;
  uint32_t compute_fence_wait_count = 0;
  uint32_t compute_fence_update_count = 0;
  uint32_t blit_pass_count = 0;
  uint32_t blit_pass_optimized = 0;
  uint32_t blit_pass_with_commands_count = 0;
  uint32_t blit_pass_empty_count = 0;
  uint32_t blit_pass_with_fence_wait_count = 0;
  uint32_t blit_pass_with_fence_update_count = 0;
  uint32_t blit_fence_wait_entry_count = 0;
  uint32_t blit_fence_update_entry_count = 0;
  uint32_t blit_command_count = 0;
  uint32_t blit_copy_buffer_to_buffer_count = 0;
  uint32_t blit_copy_buffer_to_texture_count = 0;
  uint32_t blit_copy_texture_to_buffer_count = 0;
  uint32_t blit_copy_texture_to_texture_count = 0;
  uint32_t blit_generate_mipmaps_count = 0;
  uint32_t blit_fence_wait_count = 0;
  uint32_t blit_fence_update_count = 0;
  uint32_t blit_fill_buffer_count = 0;
  uint32_t blit_resolve_counters_count = 0;
  uint32_t event_stall = 0;
  uint32_t latency = 0;
  uint32_t flush_chunk_count = 0;
  uint32_t flush_empty_chunk_count = 0;
  uint32_t flush_event_only_chunk_count = 0;
  uint32_t flush_signal_chunk_count = 0;
  uint32_t flush_wait_chunk_count = 0;
  uint32_t flush_present_chunk_count = 0;
  uint32_t flush_encoder_count = 0;
  uint32_t flush_null_encoder_count = 0;
  uint32_t flush_render_encoder_count = 0;
  uint32_t flush_compute_encoder_count = 0;
  uint32_t flush_blit_encoder_count = 0;
  uint32_t flush_timestamp_encoder_count = 0;
  clock::duration flush_total_interval{};
  clock::duration flush_collect_interval{};
  clock::duration flush_relation_interval{};
  clock::duration flush_query_interval{};
  clock::duration flush_timestamp_interval{};
  clock::duration flush_render_interval{};
  clock::duration flush_compute_interval{};
  clock::duration flush_blit_interval{};
  clock::duration flush_present_interval{};
  clock::duration flush_event_interval{};
  clock::duration flush_sample_interval{};
  clock::duration flush_pending_fence_interval{};
  clock::duration flush_signal_interval{};
  clock::duration flush_cleanup_interval{};
  clock::duration flush_max_chunk_interval{};
  uint32_t shader_binding_upload_count = 0;
  uint32_t shader_binding_dirty_cbuffer_count = 0;
  uint32_t shader_binding_dirty_sampler_count = 0;
  uint32_t shader_binding_dirty_srv_count = 0;
  uint32_t shader_binding_dirty_uav_count = 0;
  uint32_t shader_binding_clean_uav_count = 0;
  clock::duration encode_prepare_interval{};
  clock::duration encode_flush_interval{};
  clock::duration drawable_blocking_interval{};
  clock::duration present_latency_interval{};
  ScalerInfo last_scaler_info{};

  void
  reset() {
    begin_time = {};
    compatibility_flags.clrAll();
    command_buffer_count = 0;
    sync_count = 0;
    sync_interval = {};
    commit_interval = {};
    render_pass_count = 0;
    render_pass_optimized = 0;
    render_command_count = 0;
    render_pso_bind_count = 0;
    render_draw_count = 0;
    render_indexed_draw_count = 0;
    render_indirect_draw_count = 0;
    render_mesh_draw_count = 0;
    render_tile_dispatch_count = 0;
    render_color_load_count = 0;
    render_color_store_count = 0;
    render_depth_load_count = 0;
    render_depth_store_count = 0;
    render_stencil_load_count = 0;
    render_stencil_store_count = 0;
    render_attachment_count = 0;
    render_attachment_reuse_count = 0;
    render_attachment_reload_count = 0;
    render_attachment_store_then_load_count = 0;
    encoder_switch_render_compute_count = 0;
    encoder_switch_compute_render_count = 0;
    encoder_switch_to_other_count = 0;
    present_pass_count = 0;
    clear_pass_count = 0;
    clear_pass_optimized = 0;
    resolve_pass_optimized = 0;
    compute_pass_count = 0;
    compute_pass_with_dispatch_count = 0;
    compute_pass_without_dispatch_count = 0;
    compute_command_count = 0;
    compute_pso_bind_count = 0;
    compute_set_buffer_count = 0;
    compute_set_texture_count = 0;
    compute_set_bytes_count = 0;
    compute_use_resource_count = 0;
    compute_dispatch_count = 0;
    compute_dispatch_indirect_count = 0;
    compute_memory_barrier_count = 0;
    compute_fence_wait_count = 0;
    compute_fence_update_count = 0;
    blit_pass_count = 0;
    blit_pass_optimized = 0;
    blit_pass_with_commands_count = 0;
    blit_pass_empty_count = 0;
    blit_pass_with_fence_wait_count = 0;
    blit_pass_with_fence_update_count = 0;
    blit_fence_wait_entry_count = 0;
    blit_fence_update_entry_count = 0;
    blit_command_count = 0;
    blit_copy_buffer_to_buffer_count = 0;
    blit_copy_buffer_to_texture_count = 0;
    blit_copy_texture_to_buffer_count = 0;
    blit_copy_texture_to_texture_count = 0;
    blit_generate_mipmaps_count = 0;
    blit_fence_wait_count = 0;
    blit_fence_update_count = 0;
    blit_fill_buffer_count = 0;
    blit_resolve_counters_count = 0;
    event_stall = 0;
    latency = 0;
    flush_chunk_count = 0;
    flush_empty_chunk_count = 0;
    flush_event_only_chunk_count = 0;
    flush_signal_chunk_count = 0;
    flush_wait_chunk_count = 0;
    flush_present_chunk_count = 0;
    flush_encoder_count = 0;
    flush_null_encoder_count = 0;
    flush_render_encoder_count = 0;
    flush_compute_encoder_count = 0;
    flush_blit_encoder_count = 0;
    flush_timestamp_encoder_count = 0;
    flush_total_interval = {};
    flush_collect_interval = {};
    flush_relation_interval = {};
    flush_query_interval = {};
    flush_timestamp_interval = {};
    flush_render_interval = {};
    flush_compute_interval = {};
    flush_blit_interval = {};
    flush_present_interval = {};
    flush_event_interval = {};
    flush_sample_interval = {};
    flush_pending_fence_interval = {};
    flush_signal_interval = {};
    flush_cleanup_interval = {};
    flush_max_chunk_interval = {};
    shader_binding_upload_count = 0;
    shader_binding_dirty_cbuffer_count = 0;
    shader_binding_dirty_sampler_count = 0;
    shader_binding_dirty_srv_count = 0;
    shader_binding_dirty_uav_count = 0;
    shader_binding_clean_uav_count = 0;
    encode_prepare_interval = {};
    encode_flush_interval = {};
    drawable_blocking_interval = {};
    present_latency_interval = {};
    last_scaler_info.type = {};
  };
};

constexpr size_t kFrameStatisticsCount = 16;

class FrameStatisticsContainer {
  std::array<FrameStatistics, kFrameStatisticsCount> frames_;
  FrameStatistics min_;
  FrameStatistics max_;
  FrameStatistics average_;

public:
  FrameStatistics &
  at(uint64_t frame) {
    return frames_[frame % kFrameStatisticsCount];
  }
  const FrameStatistics &
  at(uint64_t frame) const {
    return frames_.at(frame % kFrameStatisticsCount);
  }

  const FrameStatistics &
  min() const {
    return min_;
  }

  const FrameStatistics &
  max() const {
    return max_;
  }

  const FrameStatistics &
  average() const {
    return average_;
  }

  void
  compute(uint64_t current_frame) {
    min_.reset();
    max_.reset();
    average_.reset();
    current_frame = current_frame % kFrameStatisticsCount;
    for(unsigned i = 0; i < kFrameStatisticsCount; i++) {
      if (i == current_frame)
        continue; // deliberately exclude current frame since it
      min_.command_buffer_count = std::min(min_.command_buffer_count, frames_[i].command_buffer_count);
      min_.sync_count = std::min(min_.sync_count, frames_[i].sync_count);
      min_.event_stall = std::min(min_.sync_count, frames_[i].event_stall);
      min_.commit_interval = std::min(min_.commit_interval, frames_[i].commit_interval);
      min_.sync_interval = std::min(min_.sync_interval, frames_[i].sync_interval);
      min_.encode_prepare_interval = std::min(min_.encode_prepare_interval, frames_[i].encode_prepare_interval);
      min_.encode_flush_interval = std::min(min_.encode_flush_interval, frames_[i].encode_flush_interval);
      min_.drawable_blocking_interval =
          std::min(min_.drawable_blocking_interval, frames_[i].drawable_blocking_interval);
      min_.present_latency_interval = std::min(min_.present_latency_interval, frames_[i].present_latency_interval);

      max_.command_buffer_count = std::max(max_.command_buffer_count, frames_[i].command_buffer_count);
      max_.sync_count = std::max(max_.sync_count, frames_[i].sync_count);
      max_.event_stall = std::max(max_.event_stall, frames_[i].event_stall);
      max_.commit_interval = std::max(max_.commit_interval, frames_[i].commit_interval);
      max_.sync_interval = std::max(max_.sync_interval, frames_[i].sync_interval);
      max_.encode_prepare_interval = std::max(max_.encode_prepare_interval, frames_[i].encode_prepare_interval);
      max_.encode_flush_interval = std::max(max_.encode_flush_interval, frames_[i].encode_flush_interval);
      max_.drawable_blocking_interval =
          std::max(max_.drawable_blocking_interval, frames_[i].drawable_blocking_interval);
      max_.present_latency_interval = std::max(max_.present_latency_interval, frames_[i].present_latency_interval);

      average_.command_buffer_count += frames_[i].command_buffer_count;
      average_.render_pass_count += frames_[i].render_pass_count;
      average_.render_pass_optimized += frames_[i].render_pass_optimized;
      average_.render_command_count += frames_[i].render_command_count;
      average_.render_pso_bind_count += frames_[i].render_pso_bind_count;
      average_.render_draw_count += frames_[i].render_draw_count;
      average_.render_indexed_draw_count += frames_[i].render_indexed_draw_count;
      average_.render_indirect_draw_count += frames_[i].render_indirect_draw_count;
      average_.render_mesh_draw_count += frames_[i].render_mesh_draw_count;
      average_.render_tile_dispatch_count += frames_[i].render_tile_dispatch_count;
      average_.render_color_load_count += frames_[i].render_color_load_count;
      average_.render_color_store_count += frames_[i].render_color_store_count;
      average_.render_depth_load_count += frames_[i].render_depth_load_count;
      average_.render_depth_store_count += frames_[i].render_depth_store_count;
      average_.render_stencil_load_count += frames_[i].render_stencil_load_count;
      average_.render_stencil_store_count += frames_[i].render_stencil_store_count;
      average_.render_attachment_count += frames_[i].render_attachment_count;
      average_.render_attachment_reuse_count += frames_[i].render_attachment_reuse_count;
      average_.render_attachment_reload_count += frames_[i].render_attachment_reload_count;
      average_.render_attachment_store_then_load_count += frames_[i].render_attachment_store_then_load_count;
      average_.encoder_switch_render_compute_count += frames_[i].encoder_switch_render_compute_count;
      average_.encoder_switch_compute_render_count += frames_[i].encoder_switch_compute_render_count;
      average_.encoder_switch_to_other_count += frames_[i].encoder_switch_to_other_count;
      average_.compute_pass_with_dispatch_count += frames_[i].compute_pass_with_dispatch_count;
      average_.compute_pass_without_dispatch_count += frames_[i].compute_pass_without_dispatch_count;
      average_.compute_command_count += frames_[i].compute_command_count;
      average_.compute_pso_bind_count += frames_[i].compute_pso_bind_count;
      average_.compute_set_buffer_count += frames_[i].compute_set_buffer_count;
      average_.compute_set_texture_count += frames_[i].compute_set_texture_count;
      average_.compute_set_bytes_count += frames_[i].compute_set_bytes_count;
      average_.compute_use_resource_count += frames_[i].compute_use_resource_count;
      average_.compute_dispatch_count += frames_[i].compute_dispatch_count;
      average_.compute_dispatch_indirect_count += frames_[i].compute_dispatch_indirect_count;
      average_.compute_memory_barrier_count += frames_[i].compute_memory_barrier_count;
      average_.compute_fence_wait_count += frames_[i].compute_fence_wait_count;
      average_.compute_fence_update_count += frames_[i].compute_fence_update_count;
      average_.blit_pass_optimized += frames_[i].blit_pass_optimized;
      average_.blit_pass_with_commands_count += frames_[i].blit_pass_with_commands_count;
      average_.blit_pass_empty_count += frames_[i].blit_pass_empty_count;
      average_.blit_pass_with_fence_wait_count += frames_[i].blit_pass_with_fence_wait_count;
      average_.blit_pass_with_fence_update_count += frames_[i].blit_pass_with_fence_update_count;
      average_.blit_fence_wait_entry_count += frames_[i].blit_fence_wait_entry_count;
      average_.blit_fence_update_entry_count += frames_[i].blit_fence_update_entry_count;
      average_.blit_command_count += frames_[i].blit_command_count;
      average_.blit_copy_buffer_to_buffer_count += frames_[i].blit_copy_buffer_to_buffer_count;
      average_.blit_copy_buffer_to_texture_count += frames_[i].blit_copy_buffer_to_texture_count;
      average_.blit_copy_texture_to_buffer_count += frames_[i].blit_copy_texture_to_buffer_count;
      average_.blit_copy_texture_to_texture_count += frames_[i].blit_copy_texture_to_texture_count;
      average_.blit_generate_mipmaps_count += frames_[i].blit_generate_mipmaps_count;
      average_.blit_fence_wait_count += frames_[i].blit_fence_wait_count;
      average_.blit_fence_update_count += frames_[i].blit_fence_update_count;
      average_.blit_fill_buffer_count += frames_[i].blit_fill_buffer_count;
      average_.blit_resolve_counters_count += frames_[i].blit_resolve_counters_count;
      average_.sync_count += frames_[i].sync_count;
      average_.event_stall += frames_[i].event_stall;
      average_.flush_chunk_count += frames_[i].flush_chunk_count;
      average_.flush_empty_chunk_count += frames_[i].flush_empty_chunk_count;
      average_.flush_event_only_chunk_count += frames_[i].flush_event_only_chunk_count;
      average_.flush_signal_chunk_count += frames_[i].flush_signal_chunk_count;
      average_.flush_wait_chunk_count += frames_[i].flush_wait_chunk_count;
      average_.flush_present_chunk_count += frames_[i].flush_present_chunk_count;
      average_.flush_encoder_count += frames_[i].flush_encoder_count;
      average_.flush_null_encoder_count += frames_[i].flush_null_encoder_count;
      average_.flush_render_encoder_count += frames_[i].flush_render_encoder_count;
      average_.flush_compute_encoder_count += frames_[i].flush_compute_encoder_count;
      average_.flush_blit_encoder_count += frames_[i].flush_blit_encoder_count;
      average_.flush_timestamp_encoder_count += frames_[i].flush_timestamp_encoder_count;
      average_.flush_total_interval += frames_[i].flush_total_interval;
      average_.flush_collect_interval += frames_[i].flush_collect_interval;
      average_.flush_relation_interval += frames_[i].flush_relation_interval;
      average_.flush_query_interval += frames_[i].flush_query_interval;
      average_.flush_timestamp_interval += frames_[i].flush_timestamp_interval;
      average_.flush_render_interval += frames_[i].flush_render_interval;
      average_.flush_compute_interval += frames_[i].flush_compute_interval;
      average_.flush_blit_interval += frames_[i].flush_blit_interval;
      average_.flush_present_interval += frames_[i].flush_present_interval;
      average_.flush_event_interval += frames_[i].flush_event_interval;
      average_.flush_sample_interval += frames_[i].flush_sample_interval;
      average_.flush_pending_fence_interval += frames_[i].flush_pending_fence_interval;
      average_.flush_signal_interval += frames_[i].flush_signal_interval;
      average_.flush_cleanup_interval += frames_[i].flush_cleanup_interval;
      average_.flush_max_chunk_interval =
          std::max(average_.flush_max_chunk_interval, frames_[i].flush_max_chunk_interval);
      average_.commit_interval += frames_[i].commit_interval;
      average_.sync_interval += frames_[i].sync_interval;
      average_.encode_prepare_interval += frames_[i].encode_prepare_interval;
      average_.encode_flush_interval += frames_[i].encode_flush_interval;
      average_.drawable_blocking_interval += frames_[i].drawable_blocking_interval;
      average_.present_latency_interval += frames_[i].present_latency_interval;
    }
    average_.command_buffer_count /= (kFrameStatisticsCount - 1);
    average_.render_pass_count /= (kFrameStatisticsCount - 1);
    average_.render_pass_optimized /= (kFrameStatisticsCount - 1);
    average_.render_command_count /= (kFrameStatisticsCount - 1);
    average_.render_pso_bind_count /= (kFrameStatisticsCount - 1);
    average_.render_draw_count /= (kFrameStatisticsCount - 1);
    average_.render_indexed_draw_count /= (kFrameStatisticsCount - 1);
    average_.render_indirect_draw_count /= (kFrameStatisticsCount - 1);
    average_.render_mesh_draw_count /= (kFrameStatisticsCount - 1);
    average_.render_tile_dispatch_count /= (kFrameStatisticsCount - 1);
    average_.render_color_load_count /= (kFrameStatisticsCount - 1);
    average_.render_color_store_count /= (kFrameStatisticsCount - 1);
    average_.render_depth_load_count /= (kFrameStatisticsCount - 1);
    average_.render_depth_store_count /= (kFrameStatisticsCount - 1);
    average_.render_stencil_load_count /= (kFrameStatisticsCount - 1);
    average_.render_stencil_store_count /= (kFrameStatisticsCount - 1);
    average_.render_attachment_count /= (kFrameStatisticsCount - 1);
    average_.render_attachment_reuse_count /= (kFrameStatisticsCount - 1);
    average_.render_attachment_reload_count /= (kFrameStatisticsCount - 1);
    average_.render_attachment_store_then_load_count /= (kFrameStatisticsCount - 1);
    average_.encoder_switch_render_compute_count /= (kFrameStatisticsCount - 1);
    average_.encoder_switch_compute_render_count /= (kFrameStatisticsCount - 1);
    average_.encoder_switch_to_other_count /= (kFrameStatisticsCount - 1);
    average_.compute_pass_with_dispatch_count /= (kFrameStatisticsCount - 1);
    average_.compute_pass_without_dispatch_count /= (kFrameStatisticsCount - 1);
    average_.compute_command_count /= (kFrameStatisticsCount - 1);
    average_.compute_pso_bind_count /= (kFrameStatisticsCount - 1);
    average_.compute_set_buffer_count /= (kFrameStatisticsCount - 1);
    average_.compute_set_texture_count /= (kFrameStatisticsCount - 1);
    average_.compute_set_bytes_count /= (kFrameStatisticsCount - 1);
    average_.compute_use_resource_count /= (kFrameStatisticsCount - 1);
    average_.compute_dispatch_count /= (kFrameStatisticsCount - 1);
    average_.compute_dispatch_indirect_count /= (kFrameStatisticsCount - 1);
    average_.compute_memory_barrier_count /= (kFrameStatisticsCount - 1);
    average_.compute_fence_wait_count /= (kFrameStatisticsCount - 1);
    average_.compute_fence_update_count /= (kFrameStatisticsCount - 1);
    average_.blit_pass_optimized /= (kFrameStatisticsCount - 1);
    average_.blit_pass_with_commands_count /= (kFrameStatisticsCount - 1);
    average_.blit_pass_empty_count /= (kFrameStatisticsCount - 1);
    average_.blit_pass_with_fence_wait_count /= (kFrameStatisticsCount - 1);
    average_.blit_pass_with_fence_update_count /= (kFrameStatisticsCount - 1);
    average_.blit_fence_wait_entry_count /= (kFrameStatisticsCount - 1);
    average_.blit_fence_update_entry_count /= (kFrameStatisticsCount - 1);
    average_.blit_command_count /= (kFrameStatisticsCount - 1);
    average_.blit_copy_buffer_to_buffer_count /= (kFrameStatisticsCount - 1);
    average_.blit_copy_buffer_to_texture_count /= (kFrameStatisticsCount - 1);
    average_.blit_copy_texture_to_buffer_count /= (kFrameStatisticsCount - 1);
    average_.blit_copy_texture_to_texture_count /= (kFrameStatisticsCount - 1);
    average_.blit_generate_mipmaps_count /= (kFrameStatisticsCount - 1);
    average_.blit_fence_wait_count /= (kFrameStatisticsCount - 1);
    average_.blit_fence_update_count /= (kFrameStatisticsCount - 1);
    average_.blit_fill_buffer_count /= (kFrameStatisticsCount - 1);
    average_.blit_resolve_counters_count /= (kFrameStatisticsCount - 1);
    average_.sync_count /= (kFrameStatisticsCount - 1);
    average_.event_stall /= (kFrameStatisticsCount - 1);
    average_.flush_chunk_count /= (kFrameStatisticsCount - 1);
    average_.flush_empty_chunk_count /= (kFrameStatisticsCount - 1);
    average_.flush_event_only_chunk_count /= (kFrameStatisticsCount - 1);
    average_.flush_signal_chunk_count /= (kFrameStatisticsCount - 1);
    average_.flush_wait_chunk_count /= (kFrameStatisticsCount - 1);
    average_.flush_present_chunk_count /= (kFrameStatisticsCount - 1);
    average_.flush_encoder_count /= (kFrameStatisticsCount - 1);
    average_.flush_null_encoder_count /= (kFrameStatisticsCount - 1);
    average_.flush_render_encoder_count /= (kFrameStatisticsCount - 1);
    average_.flush_compute_encoder_count /= (kFrameStatisticsCount - 1);
    average_.flush_blit_encoder_count /= (kFrameStatisticsCount - 1);
    average_.flush_timestamp_encoder_count /= (kFrameStatisticsCount - 1);
    average_.flush_total_interval /= (kFrameStatisticsCount - 1);
    average_.flush_collect_interval /= (kFrameStatisticsCount - 1);
    average_.flush_relation_interval /= (kFrameStatisticsCount - 1);
    average_.flush_query_interval /= (kFrameStatisticsCount - 1);
    average_.flush_timestamp_interval /= (kFrameStatisticsCount - 1);
    average_.flush_render_interval /= (kFrameStatisticsCount - 1);
    average_.flush_compute_interval /= (kFrameStatisticsCount - 1);
    average_.flush_blit_interval /= (kFrameStatisticsCount - 1);
    average_.flush_present_interval /= (kFrameStatisticsCount - 1);
    average_.flush_event_interval /= (kFrameStatisticsCount - 1);
    average_.flush_sample_interval /= (kFrameStatisticsCount - 1);
    average_.flush_pending_fence_interval /= (kFrameStatisticsCount - 1);
    average_.flush_signal_interval /= (kFrameStatisticsCount - 1);
    average_.flush_cleanup_interval /= (kFrameStatisticsCount - 1);
    average_.commit_interval /= (kFrameStatisticsCount - 1);
    average_.sync_interval /= (kFrameStatisticsCount - 1);
    average_.encode_prepare_interval /= (kFrameStatisticsCount - 1);
    average_.encode_flush_interval /= (kFrameStatisticsCount - 1);
    average_.drawable_blocking_interval /= (kFrameStatisticsCount - 1);
    average_.present_latency_interval /= (kFrameStatisticsCount - 1);
  };
};

} // namespace dxmt
