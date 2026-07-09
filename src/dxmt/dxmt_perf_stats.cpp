#include "dxmt_perf_stats.hpp"

#include "log/log.hpp"
#include "util_env.hpp"
#include "util_string.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>

namespace dxmt::perf {
namespace {

struct Counters {
  std::atomic<uint64_t> frames = {0};
  std::atomic<uint64_t> wait_cpu_fence_count = {0};
  std::atomic<uint64_t> wait_cpu_fence_us = {0};
  std::atomic<uint64_t> wait_cpu_fence_max_us = {0};
  std::atomic<uint64_t> timestamp_gpu_runs = {0};
  std::atomic<uint64_t> timestamp_gpu_queries = {0};
  std::atomic<uint64_t> timestamp_cpu_fallbacks = {0};
  std::atomic<uint64_t> timestamp_cpu_fallback_queries = {0};
  std::atomic<uint64_t> timestamp_cpu_deferred = {0};
  std::atomic<uint64_t> timestamp_cpu_deferred_queries = {0};
  std::atomic<uint64_t> timestamp_cpu_immediate = {0};
  std::atomic<uint64_t> timestamp_cpu_unsafe = {0};
  std::atomic<uint64_t> timestamp_cpu_materialized = {0};
  std::atomic<uint64_t> timestamp_cpu_wait_us = {0};
  std::atomic<uint64_t> timestamp_cpu_wait_max_us = {0};
  std::atomic<uint64_t> query_batch_waits = {0};
  std::atomic<uint64_t> query_batch_wait_queries = {0};
  std::atomic<uint64_t> query_batch_wait_us = {0};
  std::atomic<uint64_t> query_batch_wait_max_us = {0};
  std::atomic<uint64_t> tile_mapping_calls = {0};
  std::atomic<uint64_t> tile_mapping_standard_ops = {0};
  std::atomic<uint64_t> tile_mapping_packed_ops = {0};
  std::atomic<uint64_t> tile_mapping_map_ops = {0};
  std::atomic<uint64_t> tile_mapping_unmap_ops = {0};
  std::atomic<uint64_t> tile_mapping_invalid = {0};
  std::atomic<uint64_t> tile_mapping_metal_failures = {0};
  std::atomic<uint64_t> tile_mapping_barriers = {0};
  std::atomic<uint64_t> graphics_pso_creates = {0};
  std::atomic<uint64_t> graphics_pso_create_us = {0};
  std::atomic<uint64_t> graphics_pso_create_max_us = {0};
  std::atomic<uint64_t> graphics_pso_create_failures = {0};
  std::atomic<uint64_t> compute_pso_creates = {0};
  std::atomic<uint64_t> compute_pso_create_us = {0};
  std::atomic<uint64_t> compute_pso_create_max_us = {0};
  std::atomic<uint64_t> compute_pso_create_failures = {0};
  std::atomic<uint64_t> frame_wall_us = {0};
  std::atomic<uint64_t> frame_wall_max_us = {0};
  std::atomic<uint64_t> frame_command_buffers = {0};
  std::atomic<uint64_t> frame_command_buffers_max = {0};
  std::atomic<uint64_t> frame_render_passes = {0};
  std::atomic<uint64_t> frame_render_passes_max = {0};
  std::atomic<uint64_t> frame_render_commands = {0};
  std::atomic<uint64_t> frame_render_commands_max = {0};
  std::atomic<uint64_t> frame_render_draws = {0};
  std::atomic<uint64_t> frame_render_draws_max = {0};
  std::atomic<uint64_t> frame_render_indexed = {0};
  std::atomic<uint64_t> frame_render_indexed_max = {0};
  std::atomic<uint64_t> frame_render_pso_binds = {0};
  std::atomic<uint64_t> frame_render_pso_binds_max = {0};
  std::atomic<uint64_t> frame_compute_passes = {0};
  std::atomic<uint64_t> frame_compute_passes_max = {0};
  std::atomic<uint64_t> frame_compute_commands = {0};
  std::atomic<uint64_t> frame_compute_commands_max = {0};
  std::atomic<uint64_t> frame_compute_dispatches = {0};
  std::atomic<uint64_t> frame_compute_dispatches_max = {0};
  std::atomic<uint64_t> frame_blit_passes = {0};
  std::atomic<uint64_t> frame_blit_passes_max = {0};
  std::atomic<uint64_t> frame_blit_empty_passes = {0};
  std::atomic<uint64_t> frame_blit_deferred_fence_only_passes = {0};
  std::atomic<uint64_t> frame_blit_merged_fence_only_passes = {0};
  std::atomic<uint64_t> frame_blit_barrier_only_passes = {0};
  std::atomic<uint64_t> frame_blit_separator_passes = {0};
  std::atomic<uint64_t> frame_resource_barrier_batches_merged = {0};
  std::atomic<uint64_t> frame_resource_barrier_batches_graphics_inlined = {0};
  std::atomic<uint64_t> frame_resource_barrier_batches_compute_inlined = {0};
  std::atomic<uint64_t> frame_flush_chunks = {0};
  std::atomic<uint64_t> frame_flush_chunks_max = {0};
  std::atomic<uint64_t> frame_flush_encoders = {0};
  std::atomic<uint64_t> frame_flush_encoders_max = {0};
  std::atomic<uint64_t> frame_encoded_encoders = {0};
  std::atomic<uint64_t> frame_encoded_encoders_max = {0};
  std::atomic<uint64_t> frame_flush_render = {0};
  std::atomic<uint64_t> frame_flush_render_max = {0};
  std::atomic<uint64_t> frame_flush_compute = {0};
  std::atomic<uint64_t> frame_flush_compute_max = {0};
  std::atomic<uint64_t> frame_flush_blit = {0};
  std::atomic<uint64_t> frame_flush_blit_max = {0};
  std::atomic<uint64_t> frame_encoder_switches = {0};
  std::atomic<uint64_t> frame_encoder_switches_max = {0};
  std::atomic<uint64_t> frame_flush_us = {0};
  std::atomic<uint64_t> frame_flush_max_us = {0};
  std::atomic<uint64_t> frame_commit_us = {0};
  std::atomic<uint64_t> frame_commit_max_us = {0};
  std::atomic<uint64_t> metal_commit_count = {0};
  std::atomic<uint64_t> metal_commit_wall_us = {0};
  std::atomic<uint64_t> metal_commit_wall_max_us = {0};
  std::atomic<uint64_t> drawable_acquire_count = {0};
  std::atomic<uint64_t> drawable_acquire_us = {0};
  std::atomic<uint64_t> drawable_acquire_max_us = {0};
  std::atomic<uint64_t> descriptor_content_writes = {0};
  std::atomic<uint64_t> descriptor_content_write_cbv = {0};
  std::atomic<uint64_t> descriptor_content_write_srv = {0};
  std::atomic<uint64_t> descriptor_content_write_uav = {0};
  std::atomic<uint64_t> descriptor_content_write_sampler = {0};
  std::atomic<uint64_t> descriptor_content_write_copy = {0};
  std::atomic<uint64_t> descriptor_content_write_feedback_uav = {0};
  std::atomic<uint64_t> native_descriptor_buffer_records = {0};
  std::atomic<uint64_t> native_descriptor_buffer_record_cbv = {0};
  std::atomic<uint64_t> native_descriptor_buffer_record_srv = {0};
  std::atomic<uint64_t> native_descriptor_buffer_record_uav = {0};
  std::atomic<uint64_t> native_descriptor_buffer_record_counters = {0};
  std::atomic<uint64_t> native_descriptor_buffer_record_missing_resource = {0};
  std::atomic<uint64_t> native_descriptor_resource_table_entries = {0};
  std::atomic<uint64_t> avg_command_buffers = {0};
  std::atomic<uint64_t> avg_render_passes = {0};
  std::atomic<uint64_t> avg_render_draws = {0};
  std::atomic<uint64_t> avg_render_indexed = {0};
  std::atomic<uint64_t> avg_compute_passes = {0};
  std::atomic<uint64_t> avg_compute_dispatches = {0};
  std::atomic<uint64_t> avg_blit_passes = {0};
  std::atomic<uint64_t> avg_flush_chunks = {0};
  std::atomic<uint64_t> avg_flush_encoders = {0};
  std::atomic<uint64_t> avg_encoded_encoders = {0};
  std::atomic<uint64_t> avg_flush_render = {0};
  std::atomic<uint64_t> avg_flush_compute = {0};
  std::atomic<uint64_t> avg_flush_blit = {0};
  std::atomic<uint64_t> avg_encoder_switches = {0};
  std::atomic<uint64_t> avg_flush_us = {0};
  std::atomic<uint64_t> avg_commit_us = {0};
};

Counters g_counters;
dxmt::mutex g_flush_mutex;
std::atomic<uint64_t> g_last_flush_frame = {0};
const dxmt::clock::time_point g_perf_start_time = dxmt::clock::now();
std::atomic<uint64_t> g_last_frame_log_monotonic_us = {0};
thread_local FrameStatistics *t_current_frame_stats = nullptr;
thread_local dxmt::clock::time_point t_last_timed_api_end = {};
thread_local uint32_t t_active_timed_api_depth = 0;

bool parseEnabledEnv(const char *name) {
  const auto value = env::getEnvVar(name);
  return value == "1" || value == "true" || value == "yes" ||
         value == "on";
}

uint64_t sample(std::atomic<uint64_t> &value) {
  return value.load(std::memory_order_relaxed);
}

uint64_t durationUs(dxmt::clock::duration value) {
  return std::chrono::duration_cast<std::chrono::microseconds>(value).count();
}

void addCompiledFallbackReason(FrameStatistics &stats,
                               CompiledFallbackReason reason,
                               uint64_t count) {
  const auto index = static_cast<size_t>(reason);
  if (index < stats.frame_compiled_fallback_reasons.size())
    stats.frame_compiled_fallback_reasons[index] += count;
}

void addDurationToBucket(FrameStatistics &stats, FrameTimeBucket bucket,
                         dxmt::clock::duration duration) {
  switch (bucket) {
  case FrameTimeBucket::ExecuteCommandLists:
    stats.frame_execute_command_lists_interval += duration;
    break;
  case FrameTimeBucket::Present:
    stats.frame_present_interval += duration;
    break;
  case FrameTimeBucket::QueueSignal:
    stats.frame_queue_signal_interval += duration;
    break;
  case FrameTimeBucket::QueueWait:
    stats.frame_queue_wait_interval += duration;
    break;
  case FrameTimeBucket::CreateResource:
    stats.frame_create_resource_interval += duration;
    break;
  case FrameTimeBucket::CreateReservedResource:
    stats.frame_create_reserved_resource_interval += duration;
    break;
  case FrameTimeBucket::CreateHeap:
    stats.frame_create_heap_interval += duration;
    break;
  case FrameTimeBucket::CreatePipeline:
    stats.frame_create_pipeline_interval += duration;
    break;
  case FrameTimeBucket::UnscopedD3D12Api:
    stats.frame_other_d3d12_interval += duration;
    break;
  case FrameTimeBucket::CommandListRecord:
    stats.frame_cmdlist_record_interval += duration;
    break;
  }
}

void addApiGapToBucket(FrameStatistics &stats, FrameTimeBucket bucket,
                       dxmt::clock::duration duration) {
  switch (bucket) {
  case FrameTimeBucket::ExecuteCommandLists:
    stats.frame_api_gap_before_execute_command_lists_interval += duration;
    break;
  case FrameTimeBucket::Present:
    stats.frame_api_gap_before_present_interval += duration;
    break;
  case FrameTimeBucket::QueueSignal:
    stats.frame_api_gap_before_queue_signal_interval += duration;
    break;
  case FrameTimeBucket::QueueWait:
    stats.frame_api_gap_before_queue_wait_interval += duration;
    break;
  case FrameTimeBucket::CreateResource:
    stats.frame_api_gap_before_create_resource_interval += duration;
    break;
  case FrameTimeBucket::CreateReservedResource:
    stats.frame_api_gap_before_create_reserved_resource_interval += duration;
    break;
  case FrameTimeBucket::CreateHeap:
    stats.frame_api_gap_before_create_heap_interval += duration;
    break;
  case FrameTimeBucket::CreatePipeline:
    stats.frame_api_gap_before_create_pipeline_interval += duration;
    break;
  case FrameTimeBucket::UnscopedD3D12Api:
    stats.frame_api_gap_before_unscoped_d3d12_api_interval += duration;
    break;
  case FrameTimeBucket::CommandListRecord:
    stats.frame_api_gap_before_cmdlist_record_interval += duration;
    break;
  }
}

void addDurationToBucket(FrameStatistics &stats, ExecuteTimeBucket bucket,
                         dxmt::clock::duration duration) {
  switch (bucket) {
  case ExecuteTimeBucket::Validate:
    stats.frame_execute_validate_interval += duration;
    break;
  case ExecuteTimeBucket::Collect:
    stats.frame_execute_collect_interval += duration;
    break;
  case ExecuteTimeBucket::Enqueue:
    stats.frame_execute_enqueue_interval += duration;
    break;
  case ExecuteTimeBucket::Drain:
    stats.frame_execute_drain_interval += duration;
    break;
  case ExecuteTimeBucket::DrainLock:
    stats.frame_execute_drain_lock_interval += duration;
    break;
  case ExecuteTimeBucket::Replay:
    stats.frame_execute_replay_interval += duration;
    break;
  case ExecuteTimeBucket::Decay:
    stats.frame_execute_decay_interval += duration;
    break;
  case ExecuteTimeBucket::Signal:
    stats.frame_execute_signal_interval += duration;
    break;
  case ExecuteTimeBucket::Commit:
    stats.frame_execute_commit_interval += duration;
    break;
  case ExecuteTimeBucket::WaitArm:
    stats.frame_execute_wait_arm_interval += duration;
    break;
  }
}

void updateMax(std::atomic<uint64_t> &target, uint64_t value) {
  auto current = target.load(std::memory_order_relaxed);
  while (current < value &&
         !target.compare_exchange_weak(current, value,
                                       std::memory_order_relaxed)) {
  }
}

void flushCounters(uint64_t frame, bool final) {
  if (!enabled())
    return;

  if (frame == 0)
    return;

  std::lock_guard lock(g_flush_mutex);
  const auto last = g_last_flush_frame.load(std::memory_order_relaxed);
  if (last == frame && !final)
    return;
  g_last_flush_frame.store(frame, std::memory_order_relaxed);
  Logger::logFileOnly(
      LogLevel::Info,
      str::format("DXMT perf stats:"
                  " frame=", frame,
                  " final=", final ? 1 : 0,
                  " totalFrames=", sample(g_counters.frames),
                  " waitCpuFenceCount=",
                  sample(g_counters.wait_cpu_fence_count),
                  " waitCpuFenceUs=", sample(g_counters.wait_cpu_fence_us),
                  " waitCpuFenceMaxUs=",
                  sample(g_counters.wait_cpu_fence_max_us),
                  " tsGpuRuns=", sample(g_counters.timestamp_gpu_runs),
                  " tsGpuQueries=", sample(g_counters.timestamp_gpu_queries),
                  " tsCpuFallbacks=",
                  sample(g_counters.timestamp_cpu_fallbacks),
                  " tsCpuFallbackQueries=",
                  sample(g_counters.timestamp_cpu_fallback_queries),
                  " tsCpuDeferred=",
                  sample(g_counters.timestamp_cpu_deferred),
                  " tsCpuDeferredQueries=",
                  sample(g_counters.timestamp_cpu_deferred_queries),
                  " tsCpuImmediate=",
                  sample(g_counters.timestamp_cpu_immediate),
                  " tsCpuUnsafe=", sample(g_counters.timestamp_cpu_unsafe),
                  " tsCpuMaterialized=",
                  sample(g_counters.timestamp_cpu_materialized),
                  " tsCpuWaitUs=", sample(g_counters.timestamp_cpu_wait_us),
                  " tsCpuWaitMaxUs=",
                  sample(g_counters.timestamp_cpu_wait_max_us),
                  " queryBatchWaits=", sample(g_counters.query_batch_waits),
                  " queryBatchWaitQueries=",
                  sample(g_counters.query_batch_wait_queries),
                  " queryBatchWaitUs=", sample(g_counters.query_batch_wait_us),
                  " queryBatchWaitMaxUs=",
                  sample(g_counters.query_batch_wait_max_us),
                  " tileMappingCalls=",
                  sample(g_counters.tile_mapping_calls),
                  " tileMappingStandardOps=",
                  sample(g_counters.tile_mapping_standard_ops),
                  " tileMappingPackedOps=",
                  sample(g_counters.tile_mapping_packed_ops),
                  " tileMappingMapOps=",
                  sample(g_counters.tile_mapping_map_ops),
                  " tileMappingUnmapOps=",
                  sample(g_counters.tile_mapping_unmap_ops),
                  " tileMappingInvalid=",
                  sample(g_counters.tile_mapping_invalid),
                  " tileMappingMetalFailures=",
                  sample(g_counters.tile_mapping_metal_failures),
                  " tileMappingBarriers=",
                  sample(g_counters.tile_mapping_barriers),
                  " graphicsPsoCreates=",
                  sample(g_counters.graphics_pso_creates),
                  " graphicsPsoCreateUs=",
                  sample(g_counters.graphics_pso_create_us),
                  " graphicsPsoCreateMaxUs=",
                  sample(g_counters.graphics_pso_create_max_us),
                  " graphicsPsoCreateFailures=",
                  sample(g_counters.graphics_pso_create_failures),
                  " computePsoCreates=",
                  sample(g_counters.compute_pso_creates),
                  " computePsoCreateUs=",
                  sample(g_counters.compute_pso_create_us),
                  " computePsoCreateMaxUs=",
                  sample(g_counters.compute_pso_create_max_us),
                  " computePsoCreateFailures=",
                  sample(g_counters.compute_pso_create_failures),
                  " frameWallUs=", sample(g_counters.frame_wall_us),
                  " frameWallMaxUs=",
                  sample(g_counters.frame_wall_max_us),
                  " frameCmdBufs=",
                  sample(g_counters.frame_command_buffers),
                  " frameCmdBufsMax=",
                  sample(g_counters.frame_command_buffers_max),
                  " frameRenderPasses=",
                  sample(g_counters.frame_render_passes),
                  " frameRenderPassesMax=",
                  sample(g_counters.frame_render_passes_max),
                  " frameRenderCmds=",
                  sample(g_counters.frame_render_commands),
                  " frameRenderCmdsMax=",
                  sample(g_counters.frame_render_commands_max),
                  " frameRenderDraws=",
                  sample(g_counters.frame_render_draws),
                  " frameRenderDrawsMax=",
                  sample(g_counters.frame_render_draws_max),
                  " frameRenderIndexed=",
                  sample(g_counters.frame_render_indexed),
                  " frameRenderIndexedMax=",
                  sample(g_counters.frame_render_indexed_max),
                  " frameRenderPsoBinds=",
                  sample(g_counters.frame_render_pso_binds),
                  " frameRenderPsoBindsMax=",
                  sample(g_counters.frame_render_pso_binds_max),
                  " frameComputePasses=",
                  sample(g_counters.frame_compute_passes),
                  " frameComputePassesMax=",
                  sample(g_counters.frame_compute_passes_max),
                  " frameComputeCmds=",
                  sample(g_counters.frame_compute_commands),
                  " frameComputeCmdsMax=",
                  sample(g_counters.frame_compute_commands_max),
                  " frameComputeDispatches=",
                  sample(g_counters.frame_compute_dispatches),
                  " frameComputeDispatchesMax=",
                  sample(g_counters.frame_compute_dispatches_max),
                  " frameBlitPasses=", sample(g_counters.frame_blit_passes),
                  " frameBlitPassesMax=",
                  sample(g_counters.frame_blit_passes_max),
                  " frameBlitEmptyPasses=",
                  sample(g_counters.frame_blit_empty_passes),
                  " frameBlitDeferredFenceOnlyPasses=",
                  sample(g_counters.frame_blit_deferred_fence_only_passes),
                  " frameBlitMergedFenceOnlyPasses=",
                  sample(g_counters.frame_blit_merged_fence_only_passes),
                  " frameBlitBarrierOnlyPasses=",
                  sample(g_counters.frame_blit_barrier_only_passes),
                  " frameBlitSeparatorPasses=",
                  sample(g_counters.frame_blit_separator_passes),
                  " frameResourceBarrierBatchesMerged=",
                  sample(g_counters.frame_resource_barrier_batches_merged),
                  " frameResourceBarrierBatchesGraphicsInlined=",
                  sample(g_counters.frame_resource_barrier_batches_graphics_inlined),
                  " frameResourceBarrierBatchesComputeInlined=",
                  sample(g_counters.frame_resource_barrier_batches_compute_inlined),
                  " frameFlushChunks=",
                  sample(g_counters.frame_flush_chunks),
                  " frameFlushChunksMax=",
                  sample(g_counters.frame_flush_chunks_max),
                  " frameFlushEncoders=",
                  sample(g_counters.frame_flush_encoders),
                  " frameFlushEncodersMax=",
                  sample(g_counters.frame_flush_encoders_max),
                  " frameEncodedEncoders=",
                  sample(g_counters.frame_encoded_encoders),
                  " frameEncodedEncodersMax=",
                  sample(g_counters.frame_encoded_encoders_max),
                  " frameFlushRender=",
                  sample(g_counters.frame_flush_render),
                  " frameFlushRenderMax=",
                  sample(g_counters.frame_flush_render_max),
                  " frameFlushCompute=",
                  sample(g_counters.frame_flush_compute),
                  " frameFlushComputeMax=",
                  sample(g_counters.frame_flush_compute_max),
                  " frameFlushBlit=", sample(g_counters.frame_flush_blit),
                  " frameFlushBlitMax=",
                  sample(g_counters.frame_flush_blit_max),
                  " frameEncoderSwitches=",
                  sample(g_counters.frame_encoder_switches),
                  " frameEncoderSwitchesMax=",
                  sample(g_counters.frame_encoder_switches_max),
                  " frameFlushUs=", sample(g_counters.frame_flush_us),
                  " frameFlushMaxUs=",
                  sample(g_counters.frame_flush_max_us),
                  " frameCommitUs=", sample(g_counters.frame_commit_us),
                  " frameCommitMaxUs=",
                  sample(g_counters.frame_commit_max_us),
                  " metalCommitCount=",
                  sample(g_counters.metal_commit_count),
                  " metalCommitWallUs=",
                  sample(g_counters.metal_commit_wall_us),
                  " metalCommitWallMaxUs=",
                  sample(g_counters.metal_commit_wall_max_us),
                  " drawableAcquireCount=",
                  sample(g_counters.drawable_acquire_count),
                  " drawableAcquireUs=",
                  sample(g_counters.drawable_acquire_us),
                  " drawableAcquireMaxUs=",
                  sample(g_counters.drawable_acquire_max_us),
                  " avgCmdBufs=", sample(g_counters.avg_command_buffers),
                  " avgRenderPasses=", sample(g_counters.avg_render_passes),
                  " avgRenderDraws=", sample(g_counters.avg_render_draws),
                  " avgRenderIndexed=",
                  sample(g_counters.avg_render_indexed),
                  " avgComputePasses=",
                  sample(g_counters.avg_compute_passes),
                  " avgComputeDispatches=",
                  sample(g_counters.avg_compute_dispatches),
                  " avgBlitPasses=", sample(g_counters.avg_blit_passes),
                  " avgFlushChunks=", sample(g_counters.avg_flush_chunks),
                  " avgFlushEncoders=",
                  sample(g_counters.avg_flush_encoders),
                  " avgEncodedEncoders=",
                  sample(g_counters.avg_encoded_encoders),
                  " avgFlushRender=", sample(g_counters.avg_flush_render),
                  " avgFlushCompute=", sample(g_counters.avg_flush_compute),
                  " avgFlushBlit=", sample(g_counters.avg_flush_blit),
                  " avgEncoderSwitches=",
                  sample(g_counters.avg_encoder_switches),
                  " avgFlushUs=", sample(g_counters.avg_flush_us),
                  " avgCommitUs=", sample(g_counters.avg_commit_us)));
}

void maybeFlush(uint64_t frame) {
  flushCounters(frame, false);
}

} // namespace

bool enabled() {
  static const bool result = parseEnabledEnv("DXMT_PERF_STATS");
  return result;
}

ScopedFrameTimer::ScopedFrameTimer(FrameTimeBucket bucket) :
    bucket_(bucket),
    stats_(enabled() ? t_current_frame_stats : nullptr),
    begin_(enabled() ? dxmt::clock::now() : dxmt::clock::time_point{}),
    active_(begin_ != dxmt::clock::time_point{} && stats_) {
  if (active_ && t_active_timed_api_depth == 0 &&
      t_last_timed_api_end != dxmt::clock::time_point{} &&
      begin_ > t_last_timed_api_end)
    addApiGapToBucket(*stats_, bucket_, begin_ - t_last_timed_api_end);
  if (active_)
    t_active_timed_api_depth++;
}

ScopedFrameTimer::~ScopedFrameTimer() {
  stop();
}

void ScopedFrameTimer::stop() {
  if (!active_)
    return;
  const auto end = dxmt::clock::now();
  addDurationToBucket(*stats_, bucket_, end - begin_);
  if (t_active_timed_api_depth)
    t_active_timed_api_depth--;
  if (t_active_timed_api_depth == 0)
    t_last_timed_api_end = end;
  active_ = false;
}

void setCurrentFrameStatistics(FrameStatistics *stats) {
  if (!enabled())
    return;
  t_current_frame_stats = stats;
}

FrameStatistics *currentFrameStatistics() {
  if (!enabled())
    return nullptr;
  return t_current_frame_stats;
}

void resetCurrentFrameApiGapMarker(dxmt::clock::time_point now) {
  if (!enabled())
    return;
  t_last_timed_api_end = now;
  t_active_timed_api_depth = 0;
}

void recordFrameBoundaryApiGap(FrameStatistics *stats,
                               dxmt::clock::time_point boundary_time) {
  if (!enabled() || !stats || t_active_timed_api_depth != 0 ||
      t_last_timed_api_end == dxmt::clock::time_point{} ||
      boundary_time <= t_last_timed_api_end)
    return;
  stats->frame_api_gap_before_frame_boundary_interval +=
      boundary_time - t_last_timed_api_end;
  t_last_timed_api_end = boundary_time;
}

void recordExecuteTime(FrameStatistics *stats, ExecuteTimeBucket bucket,
                       dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  addDurationToBucket(*stats, bucket, duration);
}

void recordReplayBreakdown(FrameStatistics *stats,
                           dxmt::clock::duration record_loop,
                           dxmt::clock::duration flush_pass,
                           dxmt::clock::duration timestamp_resolve,
                           dxmt::clock::duration cpu_query_resolve) {
  if (!enabled() || !stats)
    return;
  stats->frame_replay_record_loop_interval += record_loop;
  stats->frame_replay_flush_pass_interval += flush_pass;
  stats->frame_replay_timestamp_resolve_interval += timestamp_resolve;
  stats->frame_replay_cpu_query_resolve_interval += cpu_query_resolve;
}

void recordCompiledPassBuildTime(FrameStatistics *stats,
                                 dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  stats->frame_compiled_pass_build_interval += duration;
}

void recordCompiledDrawEncodeTime(FrameStatistics *stats,
                                  dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  stats->frame_compiled_draw_encode_interval += duration;
}

void recordCompiledDispatchEncodeTime(FrameStatistics *stats,
                                      dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  stats->frame_compiled_dispatch_encode_interval += duration;
}

void recordArgumentTableUpdateTime(FrameStatistics *stats,
                                   dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  stats->frame_argument_table_update_interval += duration;
}

void recordArgumentTableBindTime(FrameStatistics *stats,
                                 dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  stats->frame_argument_table_bind_interval += duration;
}

void recordResidencySubmitTime(FrameStatistics *stats,
                               dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  stats->frame_residency_submit_interval += duration;
}

void recordPsoCacheLookupTime(FrameStatistics *stats,
                              dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  stats->frame_pso_cache_lookup_interval += duration;
}

void recordPsoMaterializeTime(FrameStatistics *stats,
                              dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  stats->frame_pso_materialize_interval += duration;
}

void recordPsoCompileWaitTime(FrameStatistics *stats,
                              dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  stats->frame_pso_compile_wait_interval += duration;
}

void recordCompiledGraphicsPackets(FrameStatistics *stats, uint64_t count) {
  if (!enabled() || !stats)
    return;
  stats->frame_compiled_graphics_packets += count;
}

void recordFallbackGraphicsPackets(FrameStatistics *stats, uint64_t count,
                                   CompiledFallbackReason reason) {
  if (!enabled() || !stats)
    return;
  stats->frame_fallback_graphics_packets += count;
  addCompiledFallbackReason(*stats, reason, count);
}

void recordCompiledComputePackets(FrameStatistics *stats, uint64_t count) {
  if (!enabled() || !stats)
    return;
  stats->frame_compiled_compute_packets += count;
}

void recordFallbackComputePackets(FrameStatistics *stats, uint64_t count,
                                  CompiledFallbackReason reason) {
  if (!enabled() || !stats)
    return;
  stats->frame_fallback_compute_packets += count;
  addCompiledFallbackReason(*stats, reason, count);
}

void recordStateRecordsElided(FrameStatistics *stats, uint64_t count) {
  if (!enabled() || !stats)
    return;
  stats->frame_state_records_elided += count;
}

void recordFrameBoundary(uint64_t frame) {
  if (!enabled())
    return;
  g_counters.frames.fetch_add(1, std::memory_order_relaxed);
  maybeFlush(frame);
}

void recordFrameBoundary(uint64_t frame, const FrameStatistics &frame_stats,
                         const FrameStatistics &average_stats,
                         uint64_t frame_wall_us) {
  if (!enabled())
    return;

  auto frame_stats_with_external = frame_stats;
  frame_stats_with_external.frame_descriptor_content_writes =
      g_counters.descriptor_content_writes.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_descriptor_content_write_cbv =
      g_counters.descriptor_content_write_cbv.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_descriptor_content_write_srv =
      g_counters.descriptor_content_write_srv.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_descriptor_content_write_uav =
      g_counters.descriptor_content_write_uav.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_descriptor_content_write_sampler =
      g_counters.descriptor_content_write_sampler.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_descriptor_content_write_copy =
      g_counters.descriptor_content_write_copy.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_descriptor_content_write_feedback_uav =
      g_counters.descriptor_content_write_feedback_uav.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_native_descriptor_buffer_records =
      g_counters.native_descriptor_buffer_records.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_native_descriptor_buffer_record_cbv =
      g_counters.native_descriptor_buffer_record_cbv.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_native_descriptor_buffer_record_srv =
      g_counters.native_descriptor_buffer_record_srv.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_native_descriptor_buffer_record_uav =
      g_counters.native_descriptor_buffer_record_uav.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_native_descriptor_buffer_record_counters =
      g_counters.native_descriptor_buffer_record_counters.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_native_descriptor_buffer_record_missing_resource =
      g_counters.native_descriptor_buffer_record_missing_resource.exchange(
          0, std::memory_order_relaxed);
  frame_stats_with_external.frame_native_descriptor_resource_table_entries =
      g_counters.native_descriptor_resource_table_entries.exchange(
          0, std::memory_order_relaxed);

  g_counters.frames.fetch_add(1, std::memory_order_relaxed);
  g_counters.frame_wall_us.fetch_add(frame_wall_us,
                                    std::memory_order_relaxed);
  updateMax(g_counters.frame_wall_max_us, frame_wall_us);

  const auto render_draws =
      uint64_t(frame_stats.render_draw_count) +
      frame_stats.render_indexed_draw_count +
      frame_stats.render_indirect_draw_count +
      frame_stats.render_mesh_draw_count +
      frame_stats.render_tile_dispatch_count;
  const auto compute_passes =
      uint64_t(frame_stats.compute_pass_with_dispatch_count) +
      frame_stats.compute_pass_without_dispatch_count;
  const auto encoder_switches =
      uint64_t(frame_stats.encoder_switch_render_compute_count) +
      frame_stats.encoder_switch_compute_render_count +
      frame_stats.encoder_switch_to_other_count;
  const auto flush_us = durationUs(frame_stats.flush_total_interval);
  const auto commit_us = durationUs(frame_stats.commit_interval);
  const auto execute_command_lists_us =
      durationUs(frame_stats.frame_execute_command_lists_interval);
  const auto present_us = durationUs(frame_stats.frame_present_interval);
  const auto queue_signal_us =
      durationUs(frame_stats.frame_queue_signal_interval);
  const auto queue_wait_us = durationUs(frame_stats.frame_queue_wait_interval);
  const auto create_resource_us =
      durationUs(frame_stats.frame_create_resource_interval);
  const auto create_reserved_resource_us =
      durationUs(frame_stats.frame_create_reserved_resource_interval);
  const auto create_heap_us = durationUs(frame_stats.frame_create_heap_interval);
  const auto create_pipeline_us =
      durationUs(frame_stats.frame_create_pipeline_interval);
  const auto unscoped_d3d12_api_us =
      durationUs(frame_stats.frame_other_d3d12_interval);
  const auto cmdlist_record_us =
      durationUs(frame_stats.frame_cmdlist_record_interval);
  const auto api_gap_before_execute_command_lists_us =
      durationUs(frame_stats.frame_api_gap_before_execute_command_lists_interval);
  const auto api_gap_before_present_us =
      durationUs(frame_stats.frame_api_gap_before_present_interval);
  const auto api_gap_before_queue_signal_us =
      durationUs(frame_stats.frame_api_gap_before_queue_signal_interval);
  const auto api_gap_before_queue_wait_us =
      durationUs(frame_stats.frame_api_gap_before_queue_wait_interval);
  const auto api_gap_before_create_resource_us =
      durationUs(frame_stats.frame_api_gap_before_create_resource_interval);
  const auto api_gap_before_create_reserved_resource_us = durationUs(
      frame_stats.frame_api_gap_before_create_reserved_resource_interval);
  const auto api_gap_before_create_heap_us =
      durationUs(frame_stats.frame_api_gap_before_create_heap_interval);
  const auto api_gap_before_create_pipeline_us =
      durationUs(frame_stats.frame_api_gap_before_create_pipeline_interval);
  const auto api_gap_before_unscoped_d3d12_api_us = durationUs(
      frame_stats.frame_api_gap_before_unscoped_d3d12_api_interval);
  const auto api_gap_before_cmdlist_record_us =
      durationUs(frame_stats.frame_api_gap_before_cmdlist_record_interval);
  const auto api_gap_before_frame_boundary_us =
      durationUs(frame_stats.frame_api_gap_before_frame_boundary_interval);
  const auto api_gap_total_us =
      api_gap_before_execute_command_lists_us + api_gap_before_present_us +
      api_gap_before_queue_signal_us + api_gap_before_queue_wait_us +
      api_gap_before_create_resource_us +
      api_gap_before_create_reserved_resource_us +
      api_gap_before_create_heap_us + api_gap_before_create_pipeline_us +
      api_gap_before_unscoped_d3d12_api_us +
      api_gap_before_cmdlist_record_us + api_gap_before_frame_boundary_us;
  const auto present_latency_wait_us =
      durationUs(frame_stats.present_latency_interval);
  const auto drawable_blocking_us =
      durationUs(frame_stats.drawable_blocking_interval);
  const auto execute_validate_us =
      durationUs(frame_stats.frame_execute_validate_interval);
  const auto execute_collect_us =
      durationUs(frame_stats.frame_execute_collect_interval);
  const auto execute_enqueue_us =
      durationUs(frame_stats.frame_execute_enqueue_interval);
  const auto execute_drain_us =
      durationUs(frame_stats.frame_execute_drain_interval);
  const auto execute_drain_lock_us =
      durationUs(frame_stats.frame_execute_drain_lock_interval);
  const auto execute_replay_us =
      durationUs(frame_stats.frame_execute_replay_interval);
  const auto execute_decay_us =
      durationUs(frame_stats.frame_execute_decay_interval);
  const auto execute_signal_us =
      durationUs(frame_stats.frame_execute_signal_interval);
  const auto execute_commit_us =
      durationUs(frame_stats.frame_execute_commit_interval);
  const auto execute_wait_arm_us =
      durationUs(frame_stats.frame_execute_wait_arm_interval);
  const auto replay_record_loop_us =
      durationUs(frame_stats.frame_replay_record_loop_interval);
  const auto replay_flush_pass_us =
      durationUs(frame_stats.frame_replay_flush_pass_interval);
  const auto replay_timestamp_resolve_us =
      durationUs(frame_stats.frame_replay_timestamp_resolve_interval);
  const auto replay_cpu_query_resolve_us =
      durationUs(frame_stats.frame_replay_cpu_query_resolve_interval);
  const auto replay_timed_us =
      replay_record_loop_us + replay_flush_pass_us + replay_timestamp_resolve_us +
      replay_cpu_query_resolve_us;
  const auto replay_unaccounted_us =
      execute_replay_us > replay_timed_us ? execute_replay_us - replay_timed_us : 0;
  const auto replay_get_pipeline_us =
      durationUs(frame_stats.frame_replay_get_pipeline_interval);
  const auto replay_get_metal_pso_us =
      durationUs(frame_stats.frame_replay_get_metal_pso_interval);
  const auto replay_select_pso_us =
      durationUs(frame_stats.frame_replay_select_pso_interval);
  const auto replay_desc_access_us =
      durationUs(frame_stats.frame_replay_desc_access_interval);
  const auto replay_state_update_us =
      durationUs(frame_stats.frame_replay_state_update_interval);
  const auto replay_attach_us =
      durationUs(frame_stats.frame_replay_attach_interval);
  const auto replay_bind_snapshot_us =
      durationUs(frame_stats.frame_replay_bind_snapshot_interval);
  const auto replay_estimate_us =
      durationUs(frame_stats.frame_replay_estimate_interval);
  const auto replay_packet_us =
      durationUs(frame_stats.frame_replay_packet_interval);
  const auto replay_queue_us =
      durationUs(frame_stats.frame_replay_queue_interval);
  const auto replay_emit_us =
      durationUs(frame_stats.frame_replay_emit_interval);
  const auto replay_flush_blit_us =
      durationUs(frame_stats.frame_replay_flush_blit_interval);
  const auto replay_flush_compute_us =
      durationUs(frame_stats.frame_replay_flush_compute_interval);
  const auto replay_flush_graphics_us =
      durationUs(frame_stats.frame_replay_flush_graphics_interval);
  const auto replay_flush_barrier_us =
      durationUs(frame_stats.frame_replay_flush_barrier_interval);
  const auto replay_build_plan_us =
      durationUs(frame_stats.frame_replay_build_plan_interval);
  const auto replay_emit_timestamp_us =
      durationUs(frame_stats.frame_replay_emit_timestamp_interval);
  const auto replay_record_draw_us =
      durationUs(frame_stats.frame_replay_record_draw_interval);
  const auto replay_record_draw_indexed_us =
      durationUs(frame_stats.frame_replay_record_draw_indexed_interval);
  const auto replay_record_dispatch_us =
      durationUs(frame_stats.frame_replay_record_dispatch_interval);
  const auto replay_record_pipeline_state_us =
      durationUs(frame_stats.frame_replay_record_pipeline_state_interval);
  const auto replay_record_descriptor_heaps_us =
      durationUs(frame_stats.frame_replay_record_descriptor_heaps_interval);
  const auto replay_record_root_signature_us =
      durationUs(frame_stats.frame_replay_record_root_signature_interval);
  const auto replay_record_root_table_us =
      durationUs(frame_stats.frame_replay_record_root_table_interval);
  const auto replay_record_root_descriptor_us =
      durationUs(frame_stats.frame_replay_record_root_descriptor_interval);
  const auto replay_record_root_constants_us =
      durationUs(frame_stats.frame_replay_record_root_constants_interval);
  const auto replay_record_vertex_index_state_us =
      durationUs(frame_stats.frame_replay_record_vertex_index_state_interval);
  const auto replay_record_render_targets_us =
      durationUs(frame_stats.frame_replay_record_render_targets_interval);
  const auto replay_record_resource_barrier_us =
      durationUs(frame_stats.frame_replay_record_resource_barrier_interval);
  const auto replay_record_copy_clear_resolve_us =
      durationUs(frame_stats.frame_replay_record_copy_clear_resolve_interval);
  const auto replay_record_query_us =
      durationUs(frame_stats.frame_replay_record_query_interval);
  const auto replay_record_execute_indirect_us =
      durationUs(frame_stats.frame_replay_record_execute_indirect_interval);
  const auto replay_record_temporal_upscale_us =
      durationUs(frame_stats.frame_replay_record_temporal_upscale_interval);
  const auto compiled_pass_build_us =
      durationUs(frame_stats.frame_compiled_pass_build_interval);
  const auto compiled_draw_encode_us =
      durationUs(frame_stats.frame_compiled_draw_encode_interval);
  const auto compiled_draw_direct_encode_us =
      durationUs(frame_stats.frame_compiled_draw_direct_encode_interval);
  const auto compiled_draw_replay_encode_us =
      durationUs(frame_stats.frame_compiled_draw_replay_encode_interval);
  const auto compiled_draw_common_us =
      durationUs(frame_stats.frame_compiled_draw_common_interval);
  const auto compiled_draw_retain_us =
      durationUs(frame_stats.frame_compiled_draw_retain_interval);
  const auto compiled_draw_pipeline_us =
      durationUs(frame_stats.frame_compiled_draw_pipeline_interval);
  const auto compiled_draw_dsso_us =
      durationUs(frame_stats.frame_compiled_draw_dsso_interval);
  const auto compiled_draw_rasterizer_us =
      durationUs(frame_stats.frame_compiled_draw_rasterizer_interval);
  const auto compiled_draw_binding_gate_us =
      durationUs(frame_stats.frame_compiled_draw_binding_gate_interval);
  const auto compiled_draw_binding_snapshot_us =
      durationUs(frame_stats.frame_compiled_draw_binding_snapshot_interval);
  const auto compiled_draw_dynamic_state_us =
      durationUs(frame_stats.frame_compiled_draw_dynamic_state_interval);
  const auto compiled_draw_visibility_us =
      durationUs(frame_stats.frame_compiled_draw_visibility_interval);
  const auto compiled_draw_body_us =
      durationUs(frame_stats.frame_compiled_draw_body_interval);
  const auto compiled_snapshot_static_samplers_us =
      durationUs(frame_stats.frame_compiled_snapshot_static_samplers_interval);
  const auto compiled_snapshot_entries_us =
      durationUs(frame_stats.frame_compiled_snapshot_entries_interval);
  const auto compiled_snapshot_root_constants_us =
      durationUs(frame_stats.frame_compiled_snapshot_root_constants_interval);
  const auto compiled_snapshot_descriptors_us =
      durationUs(frame_stats.frame_compiled_snapshot_descriptors_interval);
  const auto compiled_snapshot_clear_descriptors_us =
      durationUs(frame_stats.frame_compiled_snapshot_clear_descriptors_interval);
  const auto compiled_snapshot_bindless_fill_us =
      durationUs(frame_stats.frame_compiled_snapshot_bindless_fill_interval);
  const auto compiled_snapshot_vertex_buffers_us =
      durationUs(frame_stats.frame_compiled_snapshot_vertex_buffers_interval);
  const auto compiled_snapshot_vertex_table_us =
      durationUs(frame_stats.frame_compiled_snapshot_vertex_table_interval);
  const auto compiled_snapshot_restore_argbuf_us =
      durationUs(frame_stats.frame_compiled_snapshot_restore_argbuf_interval);
  const auto compiled_snapshot_shader_bindings_us =
      durationUs(frame_stats.frame_compiled_snapshot_shader_bindings_interval);
  const auto compiled_snapshot_bindless_shader_bindings_us =
      durationUs(
          frame_stats.frame_compiled_snapshot_bindless_shader_bindings_interval);
  const auto compiled_snapshot_legacy_shader_bindings_us =
      durationUs(
          frame_stats.frame_compiled_snapshot_legacy_shader_bindings_interval);
  const auto compiled_dispatch_encode_us =
      durationUs(frame_stats.frame_compiled_dispatch_encode_interval);
  const auto argument_table_update_us =
      durationUs(frame_stats.frame_argument_table_update_interval);
  const auto argument_table_bind_us =
      durationUs(frame_stats.frame_argument_table_bind_interval);
  const auto residency_submit_us =
      durationUs(frame_stats.frame_residency_submit_interval);
  const auto pso_cache_lookup_us =
      durationUs(frame_stats.frame_pso_cache_lookup_interval);
  const auto pso_materialize_us =
      durationUs(frame_stats.frame_pso_materialize_interval);
  const auto pso_compile_wait_us =
      durationUs(frame_stats.frame_pso_compile_wait_interval);
  const auto compiled_fallback_reason_count =
      [&](CompiledFallbackReason reason) -> uint64_t {
    const auto index = static_cast<size_t>(reason);
    return index < frame_stats.frame_compiled_fallback_reasons.size()
               ? frame_stats.frame_compiled_fallback_reasons[index]
               : 0;
  };
  const auto execute_known_us =
      execute_validate_us + execute_collect_us + execute_enqueue_us +
      execute_drain_us;
  const auto execute_unscoped_us =
      execute_command_lists_us > execute_known_us
          ? execute_command_lists_us - execute_known_us
          : 0;
  const auto flush_collect_us = durationUs(frame_stats.flush_collect_interval);
  const auto flush_relation_us =
      durationUs(frame_stats.flush_relation_interval);
  const auto flush_query_us = durationUs(frame_stats.flush_query_interval);
  const auto flush_timestamp_us =
      durationUs(frame_stats.flush_timestamp_interval);
  const auto flush_render_us = durationUs(frame_stats.flush_render_interval);
  const auto flush_compute_us = durationUs(frame_stats.flush_compute_interval);
  const auto flush_blit_us = durationUs(frame_stats.flush_blit_interval);
  const auto flush_clear_us = durationUs(frame_stats.flush_clear_interval);
  const auto flush_resolve_us = durationUs(frame_stats.flush_resolve_interval);
  const auto flush_scaler_us = durationUs(frame_stats.flush_scaler_interval);
  const auto flush_present_us = durationUs(frame_stats.flush_present_interval);
  const auto flush_event_us = durationUs(frame_stats.flush_event_interval);
  const auto flush_sample_us = durationUs(frame_stats.flush_sample_interval);
  const auto flush_pending_fence_us =
      durationUs(frame_stats.flush_pending_fence_interval);
  const auto flush_signal_us = durationUs(frame_stats.flush_signal_interval);
  const auto flush_cleanup_us = durationUs(frame_stats.flush_cleanup_interval);
  const auto flush_known_us =
      flush_collect_us + flush_relation_us + flush_query_us +
      flush_timestamp_us + flush_render_us + flush_compute_us +
      flush_blit_us + flush_clear_us + flush_resolve_us + flush_scaler_us +
      flush_present_us + flush_event_us + flush_sample_us +
      flush_pending_fence_us + flush_signal_us + flush_cleanup_us;
  const auto flush_unscoped_us =
      flush_us > flush_known_us ? flush_us - flush_known_us : 0;
  const auto accounted_wall_us =
      execute_command_lists_us + present_us + queue_signal_us + queue_wait_us +
      create_resource_us + create_reserved_resource_us + create_heap_us +
      create_pipeline_us + unscoped_d3d12_api_us + cmdlist_record_us +
      present_latency_wait_us + api_gap_total_us;
  const auto overlap_adjust_us =
      accounted_wall_us > frame_wall_us ? accounted_wall_us - frame_wall_us : 0;
  const auto boundary_unclassified_us =
      frame_wall_us > accounted_wall_us ? frame_wall_us - accounted_wall_us : 0;
  const auto closure_us =
      accounted_wall_us + boundary_unclassified_us - overlap_adjust_us;
  const auto monotonic_us = durationUs(dxmt::clock::now() - g_perf_start_time);
  const auto previous_monotonic_us =
      g_last_frame_log_monotonic_us.exchange(monotonic_us,
                                             std::memory_order_relaxed);
  const auto log_delta_us = previous_monotonic_us
                                ? monotonic_us - previous_monotonic_us
                                : 0;

  g_counters.frame_command_buffers.fetch_add(
      frame_stats.command_buffer_count, std::memory_order_relaxed);
  updateMax(g_counters.frame_command_buffers_max,
            frame_stats.command_buffer_count);
  g_counters.frame_render_passes.fetch_add(frame_stats.render_pass_count,
                                          std::memory_order_relaxed);
  updateMax(g_counters.frame_render_passes_max,
            frame_stats.render_pass_count);
  g_counters.frame_render_commands.fetch_add(frame_stats.render_command_count,
                                            std::memory_order_relaxed);
  updateMax(g_counters.frame_render_commands_max,
            frame_stats.render_command_count);
  g_counters.frame_render_draws.fetch_add(render_draws,
                                         std::memory_order_relaxed);
  updateMax(g_counters.frame_render_draws_max, render_draws);
  g_counters.frame_render_indexed.fetch_add(
      frame_stats.render_indexed_draw_count, std::memory_order_relaxed);
  updateMax(g_counters.frame_render_indexed_max,
            frame_stats.render_indexed_draw_count);
  g_counters.frame_render_pso_binds.fetch_add(
      frame_stats.render_pso_bind_count, std::memory_order_relaxed);
  updateMax(g_counters.frame_render_pso_binds_max,
            frame_stats.render_pso_bind_count);
  g_counters.frame_compute_passes.fetch_add(compute_passes,
                                           std::memory_order_relaxed);
  updateMax(g_counters.frame_compute_passes_max, compute_passes);
  g_counters.frame_compute_commands.fetch_add(
      frame_stats.compute_command_count, std::memory_order_relaxed);
  updateMax(g_counters.frame_compute_commands_max,
            frame_stats.compute_command_count);
  g_counters.frame_compute_dispatches.fetch_add(
      frame_stats.compute_dispatch_count, std::memory_order_relaxed);
  updateMax(g_counters.frame_compute_dispatches_max,
            frame_stats.compute_dispatch_count);
  g_counters.frame_blit_passes.fetch_add(frame_stats.blit_pass_count,
                                        std::memory_order_relaxed);
  updateMax(g_counters.frame_blit_passes_max, frame_stats.blit_pass_count);
  g_counters.frame_blit_empty_passes.fetch_add(
      frame_stats.blit_pass_empty_count, std::memory_order_relaxed);
  g_counters.frame_blit_deferred_fence_only_passes.fetch_add(
      frame_stats.blit_pass_deferred_fence_only_count,
      std::memory_order_relaxed);
  g_counters.frame_blit_merged_fence_only_passes.fetch_add(
      frame_stats.blit_pass_merged_fence_only_count,
      std::memory_order_relaxed);
  g_counters.frame_blit_barrier_only_passes.fetch_add(
      frame_stats.blit_barrier_only_pass_count, std::memory_order_relaxed);
  g_counters.frame_blit_separator_passes.fetch_add(
      frame_stats.blit_separator_pass_count, std::memory_order_relaxed);
  g_counters.frame_resource_barrier_batches_merged.fetch_add(
      frame_stats.resource_barrier_batches_merged, std::memory_order_relaxed);
  g_counters.frame_resource_barrier_batches_graphics_inlined.fetch_add(
      frame_stats.resource_barrier_batches_graphics_inlined,
      std::memory_order_relaxed);
  g_counters.frame_resource_barrier_batches_compute_inlined.fetch_add(
      frame_stats.resource_barrier_batches_compute_inlined,
      std::memory_order_relaxed);
  g_counters.frame_flush_chunks.fetch_add(frame_stats.flush_chunk_count,
                                         std::memory_order_relaxed);
  updateMax(g_counters.frame_flush_chunks_max,
            frame_stats.flush_chunk_count);
  g_counters.frame_flush_encoders.fetch_add(frame_stats.flush_encoder_count,
                                           std::memory_order_relaxed);
  updateMax(g_counters.frame_flush_encoders_max,
            frame_stats.flush_encoder_count);
  g_counters.frame_encoded_encoders.fetch_add(
      frame_stats.flush_encoded_encoder_count, std::memory_order_relaxed);
  updateMax(g_counters.frame_encoded_encoders_max,
            frame_stats.flush_encoded_encoder_count);
  g_counters.frame_flush_render.fetch_add(
      frame_stats.flush_render_encoder_count, std::memory_order_relaxed);
  updateMax(g_counters.frame_flush_render_max,
            frame_stats.flush_render_encoder_count);
  g_counters.frame_flush_compute.fetch_add(
      frame_stats.flush_compute_encoder_count, std::memory_order_relaxed);
  updateMax(g_counters.frame_flush_compute_max,
            frame_stats.flush_compute_encoder_count);
  g_counters.frame_flush_blit.fetch_add(
      frame_stats.flush_blit_encoder_count, std::memory_order_relaxed);
  updateMax(g_counters.frame_flush_blit_max,
            frame_stats.flush_blit_encoder_count);
  g_counters.frame_encoder_switches.fetch_add(encoder_switches,
                                             std::memory_order_relaxed);
  updateMax(g_counters.frame_encoder_switches_max, encoder_switches);
  g_counters.frame_flush_us.fetch_add(flush_us, std::memory_order_relaxed);
  updateMax(g_counters.frame_flush_max_us, flush_us);
  g_counters.frame_commit_us.fetch_add(commit_us, std::memory_order_relaxed);
  updateMax(g_counters.frame_commit_max_us, commit_us);

  g_counters.avg_command_buffers.store(average_stats.command_buffer_count,
                                       std::memory_order_relaxed);
  g_counters.avg_render_passes.store(average_stats.render_pass_count,
                                     std::memory_order_relaxed);
  g_counters.avg_render_draws.store(
      uint64_t(average_stats.render_draw_count) +
          average_stats.render_indexed_draw_count +
          average_stats.render_indirect_draw_count +
          average_stats.render_mesh_draw_count +
          average_stats.render_tile_dispatch_count,
      std::memory_order_relaxed);
  g_counters.avg_render_indexed.store(
      average_stats.render_indexed_draw_count, std::memory_order_relaxed);
  g_counters.avg_compute_passes.store(
      uint64_t(average_stats.compute_pass_with_dispatch_count) +
          average_stats.compute_pass_without_dispatch_count,
      std::memory_order_relaxed);
  g_counters.avg_compute_dispatches.store(
      average_stats.compute_dispatch_count, std::memory_order_relaxed);
  g_counters.avg_blit_passes.store(average_stats.blit_pass_count,
                                   std::memory_order_relaxed);
  g_counters.avg_flush_chunks.store(average_stats.flush_chunk_count,
                                    std::memory_order_relaxed);
  g_counters.avg_flush_encoders.store(average_stats.flush_encoder_count,
                                      std::memory_order_relaxed);
  g_counters.avg_encoded_encoders.store(
      average_stats.flush_encoded_encoder_count, std::memory_order_relaxed);
  g_counters.avg_flush_render.store(average_stats.flush_render_encoder_count,
                                    std::memory_order_relaxed);
  g_counters.avg_flush_compute.store(average_stats.flush_compute_encoder_count,
                                     std::memory_order_relaxed);
  g_counters.avg_flush_blit.store(average_stats.flush_blit_encoder_count,
                                  std::memory_order_relaxed);
  g_counters.avg_encoder_switches.store(
      uint64_t(average_stats.encoder_switch_render_compute_count) +
          average_stats.encoder_switch_compute_render_count +
          average_stats.encoder_switch_to_other_count,
      std::memory_order_relaxed);
  g_counters.avg_flush_us.store(durationUs(average_stats.flush_total_interval),
                                std::memory_order_relaxed);
  g_counters.avg_commit_us.store(durationUs(average_stats.commit_interval),
                                 std::memory_order_relaxed);

  Logger::logFileOnly(
      LogLevel::Info,
      str::format("DXMT perf frame:"
                  " frame=", frame,
                  " monoUs=", monotonic_us,
                  " logDeltaUs=", log_delta_us,
                  " totalWallUs=", frame_wall_us,
                  " executeCommandListsUs=", execute_command_lists_us,
                  " executeKnownUs=", execute_known_us,
                  " executeUnscopedUs=", execute_unscoped_us,
                  " executeValidateUs=", execute_validate_us,
                  " executeCollectUs=", execute_collect_us,
                  " executeEnqueueUs=", execute_enqueue_us,
                  " executeDrainUs=", execute_drain_us,
                  " executeDrainLockUs=", execute_drain_lock_us,
                  " executeReplayUs=", execute_replay_us,
                  " executeDecayUs=", execute_decay_us,
                  " executeSignalUs=", execute_signal_us,
                  " executeCommitUs=", execute_commit_us,
                  " executeWaitArmUs=", execute_wait_arm_us,
                  " replayRecordLoopUs=", replay_record_loop_us,
                  " replayFlushPassUs=", replay_flush_pass_us,
                  " replayTimestampResolveUs=", replay_timestamp_resolve_us,
                  " replayCpuQueryResolveUs=", replay_cpu_query_resolve_us,
                  " replayTimedUs=", replay_timed_us,
                  " replayUnaccountedUs=", replay_unaccounted_us,
                  " replayGetPipelineUs=", replay_get_pipeline_us,
                  " replayGetMetalPsoUs=", replay_get_metal_pso_us,
                  " replaySelectPsoUs=", replay_select_pso_us,
                  " replayDescAccessUs=", replay_desc_access_us,
                  " replayStateUpdateUs=", replay_state_update_us,
                  " replayAttachUs=", replay_attach_us,
                  " replayBindSnapshotUs=", replay_bind_snapshot_us,
                  " replayEstimateUs=", replay_estimate_us,
                  " replayPacketUs=", replay_packet_us,
                  " replayQueueUs=", replay_queue_us,
                  " replayEmitUs=", replay_emit_us,
                  " replayFlushBlitUs=", replay_flush_blit_us,
                  " replayFlushComputeUs=", replay_flush_compute_us,
                  " replayFlushGraphicsUs=", replay_flush_graphics_us,
                  " replayFlushBarrierUs=", replay_flush_barrier_us,
                  " replayBuildPlanUs=", replay_build_plan_us,
                  " replayEmitTimestampUs=", replay_emit_timestamp_us,
                  " replayRecordDrawUs=", replay_record_draw_us,
                  " replayRecordDrawCount=",
                  frame_stats.frame_replay_record_draw_count,
                  " replayRecordDrawIndexedUs=", replay_record_draw_indexed_us,
                  " replayRecordDrawIndexedCount=",
                  frame_stats.frame_replay_record_draw_indexed_count,
                  " replayRecordDispatchUs=", replay_record_dispatch_us,
                  " replayRecordDispatchCount=",
                  frame_stats.frame_replay_record_dispatch_count,
                  " replayRecordPipelineStateUs=",
                  replay_record_pipeline_state_us,
                  " replayRecordPipelineStateCount=",
                  frame_stats.frame_replay_record_pipeline_state_count,
                  " replayRecordDescriptorHeapsUs=",
                  replay_record_descriptor_heaps_us,
                  " replayRecordDescriptorHeapsCount=",
                  frame_stats.frame_replay_record_descriptor_heaps_count,
                  " replayRecordRootSignatureUs=",
                  replay_record_root_signature_us,
                  " replayRecordRootSignatureCount=",
                  frame_stats.frame_replay_record_root_signature_count,
                  " replayRecordRootTableUs=", replay_record_root_table_us,
                  " replayRecordRootTableCount=",
                  frame_stats.frame_replay_record_root_table_count,
                  " replayRecordRootDescriptorUs=",
                  replay_record_root_descriptor_us,
                  " replayRecordRootDescriptorCount=",
                  frame_stats.frame_replay_record_root_descriptor_count,
                  " replayRecordRootConstantsUs=",
                  replay_record_root_constants_us,
                  " replayRecordRootConstantsCount=",
                  frame_stats.frame_replay_record_root_constants_count,
                  " replayRecordVertexIndexStateUs=",
                  replay_record_vertex_index_state_us,
                  " replayRecordVertexIndexStateCount=",
                  frame_stats.frame_replay_record_vertex_index_state_count,
                  " replayRecordRenderTargetsUs=",
                  replay_record_render_targets_us,
                  " replayRecordRenderTargetsCount=",
                  frame_stats.frame_replay_record_render_targets_count,
                  " replayRecordResourceBarrierUs=",
                  replay_record_resource_barrier_us,
                  " replayRecordResourceBarrierCount=",
                  frame_stats.frame_replay_record_resource_barrier_count,
                  " replayRecordCopyClearResolveUs=",
                  replay_record_copy_clear_resolve_us,
                  " replayRecordCopyClearResolveCount=",
                  frame_stats.frame_replay_record_copy_clear_resolve_count,
                  " replayRecordQueryUs=", replay_record_query_us,
                  " replayRecordQueryCount=",
                  frame_stats.frame_replay_record_query_count,
                  " replayRecordExecuteIndirectUs=",
                  replay_record_execute_indirect_us,
                  " replayRecordExecuteIndirectCount=",
                  frame_stats.frame_replay_record_execute_indirect_count,
                  " replayRecordTemporalUpscaleUs=",
                  replay_record_temporal_upscale_us,
                  " replayRecordTemporalUpscaleCount=",
                  frame_stats.frame_replay_record_temporal_upscale_count,
                  " compiledPassBuildUs=", compiled_pass_build_us,
                  " compiledDrawEncodeUs=", compiled_draw_encode_us,
                  " compiledDrawDirectEncodeUs=",
                  compiled_draw_direct_encode_us,
                  " compiledDrawReplayEncodeUs=",
                  compiled_draw_replay_encode_us,
                  " compiledDrawCommonUs=", compiled_draw_common_us,
                  " compiledDrawRetainUs=", compiled_draw_retain_us,
                  " compiledDrawPipelineUs=", compiled_draw_pipeline_us,
                  " compiledDrawDssoUs=", compiled_draw_dsso_us,
                  " compiledDrawRasterizerUs=", compiled_draw_rasterizer_us,
                  " compiledDrawBindingGateUs=",
                  compiled_draw_binding_gate_us,
                  " compiledDrawBindingSnapshotUs=",
                  compiled_draw_binding_snapshot_us,
                  " compiledDrawDynamicStateUs=",
                  compiled_draw_dynamic_state_us,
                  " compiledDrawVisibilityUs=", compiled_draw_visibility_us,
                  " compiledDrawBodyUs=", compiled_draw_body_us,
                  " compiledSnapshotStaticSamplersUs=",
                  compiled_snapshot_static_samplers_us,
                  " compiledSnapshotEntriesUs=",
                  compiled_snapshot_entries_us,
                  " compiledSnapshotRootConstantsUs=",
                  compiled_snapshot_root_constants_us,
                  " compiledSnapshotDescriptorsUs=",
                  compiled_snapshot_descriptors_us,
                  " compiledSnapshotClearDescriptorsUs=",
                  compiled_snapshot_clear_descriptors_us,
                  " compiledSnapshotBindlessFillUs=",
                  compiled_snapshot_bindless_fill_us,
                  " compiledSnapshotVertexBuffersUs=",
                  compiled_snapshot_vertex_buffers_us,
                  " compiledSnapshotVertexTableUs=",
                  compiled_snapshot_vertex_table_us,
                  " compiledSnapshotRestoreArgbufUs=",
                  compiled_snapshot_restore_argbuf_us,
                  " compiledSnapshotShaderBindingsUs=",
                  compiled_snapshot_shader_bindings_us,
                  " compiledSnapshotBindlessShaderBindingsUs=",
                  compiled_snapshot_bindless_shader_bindings_us,
                  " compiledSnapshotLegacyShaderBindingsUs=",
                  compiled_snapshot_legacy_shader_bindings_us,
                  " compiledDispatchEncodeUs=",
                  compiled_dispatch_encode_us,
                  " argumentTableUpdateUs=", argument_table_update_us,
                  " argumentTableBindUs=", argument_table_bind_us,
                  " residencySubmitUs=", residency_submit_us,
                  " psoCacheLookupUs=", pso_cache_lookup_us,
                  " psoMaterializeUs=", pso_materialize_us,
                  " psoCompileWaitUs=", pso_compile_wait_us,
                  " compiledGraphicsPackets=",
                  frame_stats.frame_compiled_graphics_packets,
                  " compiledDrawDirectPackets=",
                  frame_stats.frame_compiled_draw_direct_packets,
                  " compiledDrawReplayPackets=",
                  frame_stats.frame_compiled_draw_replay_packets,
                  " compiledDrawIndexedPackets=",
                  frame_stats.frame_compiled_draw_indexed_packets,
                  " compiledDrawNonIndexedPackets=",
                  frame_stats.frame_compiled_draw_nonindexed_packets,
                  " compiledDrawSnapshotApplied=",
                  frame_stats.frame_compiled_draw_binding_snapshot_applied,
                  " compiledDrawSnapshotSkipped=",
                  frame_stats.frame_compiled_draw_binding_snapshot_skipped,
                  " compiledDrawBindingGenerationHits=",
                  frame_stats.frame_compiled_draw_binding_generation_hits,
                  " compiledDrawBindingFingerprintHits=",
                  frame_stats.frame_compiled_draw_binding_fingerprint_hits,
                  " compiledDrawBindingMisses=",
                  frame_stats.frame_compiled_draw_binding_misses,
                  " compiledSnapshotEntries=",
                  frame_stats.frame_compiled_snapshot_entries,
                  " compiledSnapshotRootConstants=",
                  frame_stats.frame_compiled_snapshot_root_constants,
                  " compiledSnapshotDescriptors=",
                  frame_stats.frame_compiled_snapshot_descriptors,
                  " compiledSnapshotClearDescriptors=",
                  frame_stats.frame_compiled_snapshot_clear_descriptors,
                  " compiledSnapshotBindlessFills=",
                  frame_stats.frame_compiled_snapshot_bindless_fills,
                  " compiledSnapshotBindlessFillTexture=",
                  frame_stats.frame_compiled_snapshot_bindless_fill_texture,
                  " compiledSnapshotBindlessFillSampler=",
                  frame_stats.frame_compiled_snapshot_bindless_fill_sampler,
                  " compiledSnapshotBindlessFillTextureBuffer=",
                  frame_stats
                      .frame_compiled_snapshot_bindless_fill_texture_buffer,
                  " compiledSnapshotBindlessFillNull=",
                  frame_stats.frame_compiled_snapshot_bindless_fill_null,
                  " compiledSnapshotVertexBuffers=",
                  frame_stats.frame_compiled_snapshot_vertex_buffers,
                  " compiledSnapshotShaderBindings=",
                  frame_stats.frame_compiled_snapshot_shader_bindings,
                  " compiledSnapshotBindlessShaderBindings=",
                  frame_stats.frame_compiled_snapshot_bindless_shader_bindings,
                  " compiledSnapshotLegacyShaderBindings=",
                  frame_stats.frame_compiled_snapshot_legacy_shader_bindings,
                  " compiledSnapshotLegacyArgbufRestores=",
                  frame_stats.frame_compiled_snapshot_legacy_argbuf_restores,
                  " fallbackGraphicsPackets=",
                  frame_stats.frame_fallback_graphics_packets,
                  " compiledComputePackets=",
                  frame_stats.frame_compiled_compute_packets,
                  " fallbackComputePackets=",
                  frame_stats.frame_fallback_compute_packets,
                  " stateRecordsElided=",
                  frame_stats.frame_state_records_elided,
                  " compiledFallbackReasonLegacyPath=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::LegacyPath),
                  " compiledFallbackReasonResourceBarrier=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::ResourceBarrier),
                  " compiledFallbackReasonGeometryOrTessellation=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::GeometryOrTessellation),
                  " compiledFallbackReasonIndirect=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::Indirect),
                  " compiledFallbackReasonMissingCompiledEncoder=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::MissingCompiledEncoder),
                  " compiledFallbackReasonUnsupportedPipeline=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::UnsupportedPipeline),
                  " compiledFallbackReasonUnsupportedRootSignature=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::UnsupportedRootSignature),
                  " compiledFallbackReasonUnsupportedDescriptorTable=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::UnsupportedDescriptorTable),
                  " compiledFallbackReasonUnsupportedRootDescriptor=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::UnsupportedRootDescriptor),
                  " compiledFallbackReasonUnsupportedRootConstants=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::UnsupportedRootConstants),
                  " compiledFallbackReasonUnsupportedVertexIndexState=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::UnsupportedVertexIndexState),
                  " compiledFallbackReasonUnsupportedRenderTargetState=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::UnsupportedRenderTargetState),
                  " compiledFallbackReasonUnsupportedResourceAccess=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::UnsupportedResourceAccess),
                  " compiledFallbackReasonUnsupportedArgumentTable=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::UnsupportedArgumentTable),
                  " compiledFallbackReasonResidency=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::Residency),
                  " compiledFallbackReasonNativeUnsupportedRootSignature=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeUnsupportedRootSignature),
                  " compiledFallbackReasonNativeUnsupportedDescriptorRange=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeUnsupportedDescriptorRange),
                  " compiledFallbackReasonNativeUnsupportedRootDescriptor=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeUnsupportedRootDescriptor),
                  " compiledFallbackReasonNativeUnsupportedGeometryPipeline=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeUnsupportedGeometryPipeline),
                  " compiledFallbackReasonNativeUnsupportedTessellationPipeline=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeUnsupportedTessellationPipeline),
                  " compiledFallbackReasonNativeUnsupportedExecuteIndirect=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeUnsupportedExecuteIndirect),
                  " compiledFallbackReasonNativeUnsupportedDynamicResource=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeUnsupportedDynamicResource),
                  " compiledFallbackReasonNativeMissingDescriptorBackend=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeMissingDescriptorBackend),
                  " compiledFallbackReasonNativeShaderAbiMismatch=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeShaderAbiMismatch),
                  " compiledFallbackReasonNativeResidencyUnsupported=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::NativeResidencyUnsupported),
                  " compiledFallbackReasonUnknown=",
                  compiled_fallback_reason_count(
                      CompiledFallbackReason::Unknown),
                  " replayDrawCount=", frame_stats.frame_replay_draw_count,
                  " replayPsoRootUnchanged=",
                  frame_stats.frame_replay_pso_root_unchanged,
                  " replayFullBindUnchanged=",
                  frame_stats.frame_replay_full_bind_unchanged,
                  " replayFlushBlitCalls=",
                  frame_stats.frame_replay_flush_blit_count,
                  " replayFlushComputeCalls=",
                  frame_stats.frame_replay_flush_compute_count,
                  " replayFlushGraphicsCalls=",
                  frame_stats.frame_replay_flush_graphics_count,
                  " replayFlushBarrierCalls=",
                  frame_stats.frame_replay_flush_barrier_count,
                  " replayEmitTimestampCalls=",
                  frame_stats.frame_replay_emit_timestamp_count,
                  " replayFlushPassBatchesCalls=",
                  frame_stats.frame_replay_flush_pass_batches_count,
                  " replayFlushPassBatchesEmpty=",
                  frame_stats.frame_replay_flush_pass_batches_empty,
                  " replayResourceAccessCalls=",
                  frame_stats.frame_replay_resource_access_count,
                  " replayResourceAccessSteadyNoop=",
                  frame_stats.frame_replay_resource_access_steady_noop,
                  " replayDescAccessHits=",
                  frame_stats.frame_replay_desc_access_hit,
                  " replayDescAccessMiss=",
                  frame_stats.frame_replay_desc_access_miss,
                  " replayDescAccessPassthrough=",
                  frame_stats.frame_replay_desc_access_passthrough,
                  " replaySupersededStateRecordsSkipped=",
                  frame_stats.frame_replay_superseded_state_records_skipped,
                  " replayCompiledGraphicsCandidates=",
                  frame_stats.frame_replay_compiled_graphics_candidates,
                  " replayCompiledGraphicsLegacy=",
                  frame_stats.frame_replay_compiled_graphics_legacy,
                  " replayCompiledGraphicsBarriers=",
                  frame_stats.frame_replay_compiled_graphics_barriers,
                  " replayCompiledGraphicsGsTs=",
                  frame_stats.frame_replay_compiled_graphics_gs_ts,
                  " replayCompiledGraphicsIndirect=",
                  frame_stats.frame_replay_compiled_graphics_indirect,
                  " replayMismatchBarriers=",
                  frame_stats.frame_replay_mismatch_barriers,
                  " replayAppBarrierTransitions=",
                  frame_stats.frame_replay_app_barrier_transitions,
                  " replayBindingGenBumps=",
                  frame_stats.frame_replay_binding_gen_bumps,
                  " replayBindingGenPipeline=",
                  frame_stats.frame_replay_binding_gen_pipeline,
                  " replayBindingGenDescriptorHeaps=",
                  frame_stats.frame_replay_binding_gen_descriptor_heaps,
                  " replayBindingGenVertexBuffers=",
                  frame_stats.frame_replay_binding_gen_vertex_buffers,
                  " replayBindingGenRootSignature=",
                  frame_stats.frame_replay_binding_gen_root_signature,
                  " replayBindingGenRootDescriptorTable=",
                  frame_stats.frame_replay_binding_gen_root_descriptor_table,
                  " replayBindingGenRootDescriptor=",
                  frame_stats.frame_replay_binding_gen_root_descriptor,
                  " replayBindingGenRootConstants=",
                  frame_stats.frame_replay_binding_gen_root_constants,
                  " replayBindingGenIndirectVertexBuffer=",
                  frame_stats.frame_replay_binding_gen_indirect_vertex_buffer,
                  " replayBindingGenIndirectRootConstants=",
                  frame_stats
                      .frame_replay_binding_gen_indirect_root_constants,
                  " replayBindingGenIndirectRootDescriptor=",
                  frame_stats
                      .frame_replay_binding_gen_indirect_root_descriptor,
                  " replaySnapshotRequests=",
                  frame_stats.frame_replay_snapshot_requests,
                  " replaySnapshotCacheHits=",
                  frame_stats.frame_replay_snapshot_cache_hits,
                  " replaySnapshotCacheMisses=",
                  frame_stats.frame_replay_snapshot_cache_misses,
                  " replaySnapshotPassthrough=",
                  frame_stats.frame_replay_snapshot_passthrough,
                  " replaySnapshotGraphicsGenChanges=",
                  frame_stats.frame_replay_snapshot_graphics_gen_changes,
                  " replaySnapshotDescriptorGenChanges=",
                  frame_stats.frame_replay_snapshot_descriptor_gen_changes,
                  " replaySnapshotBothGenChanges=",
                  frame_stats.frame_replay_snapshot_both_gen_changes,
                  " replaySnapshotNoGenChanges=",
                  frame_stats.frame_replay_snapshot_no_gen_changes,
                  " replaySnapshotCapturedEntries=",
                  frame_stats.frame_replay_snapshot_captured_entries,
                  " replaySnapshotCapturedDescriptors=",
                  frame_stats.frame_replay_snapshot_captured_descriptors,
                  " replaySnapshotCapturedMissingDescriptors=",
                  frame_stats
                      .frame_replay_snapshot_captured_missing_descriptors,
                  " replaySnapshotCapturedRootDescriptors=",
                  frame_stats.frame_replay_snapshot_captured_root_descriptors,
                  " replaySnapshotCapturedRootConstants=",
                  frame_stats.frame_replay_snapshot_captured_root_constants,
                  " replaySnapshotCapturedVertexBuffers=",
                  frame_stats.frame_replay_snapshot_captured_vertex_buffers,
                  " replaySnapshotCapturedBindless=",
                  frame_stats.frame_replay_snapshot_captured_bindless,
                  " nativeDescriptorRootTables=",
                  frame_stats.frame_native_descriptor_root_tables,
                  " nativeDescriptorRootTableBackendReady=",
                  frame_stats.frame_native_descriptor_root_table_backend_ready,
                  " nativeDescriptorRecordStorageReady=",
                  frame_stats.frame_native_descriptor_record_storage_ready,
                  " nativeDescriptorResourceRootTables=",
                  frame_stats.frame_native_descriptor_resource_root_tables,
                  " nativeDescriptorSamplerRootTables=",
                  frame_stats.frame_native_descriptor_sampler_root_tables,
                  " nativeDescriptorBufferRecords=",
                  frame_stats_with_external
                      .frame_native_descriptor_buffer_records,
                  " nativeDescriptorBufferRecordCBV=",
                  frame_stats_with_external
                      .frame_native_descriptor_buffer_record_cbv,
                  " nativeDescriptorBufferRecordSRV=",
                  frame_stats_with_external
                      .frame_native_descriptor_buffer_record_srv,
                  " nativeDescriptorBufferRecordUAV=",
                  frame_stats_with_external
                      .frame_native_descriptor_buffer_record_uav,
                  " nativeDescriptorBufferRecordCounters=",
                  frame_stats_with_external
                      .frame_native_descriptor_buffer_record_counters,
                  " nativeDescriptorBufferRecordMissingResource=",
                  frame_stats_with_external
                      .frame_native_descriptor_buffer_record_missing_resource,
                  " nativeDescriptorResourceTableEntries=",
                  frame_stats_with_external
                      .frame_native_descriptor_resource_table_entries,
                  " descriptorContentWrites=",
                  frame_stats_with_external.frame_descriptor_content_writes,
                  " descriptorContentWriteCBV=",
                  frame_stats_with_external.frame_descriptor_content_write_cbv,
                  " descriptorContentWriteSRV=",
                  frame_stats_with_external.frame_descriptor_content_write_srv,
                  " descriptorContentWriteUAV=",
                  frame_stats_with_external.frame_descriptor_content_write_uav,
                  " descriptorContentWriteSampler=",
                  frame_stats_with_external.frame_descriptor_content_write_sampler,
                  " descriptorContentWriteCopy=",
                  frame_stats_with_external.frame_descriptor_content_write_copy,
                  " descriptorContentWriteFeedbackUAV=",
                  frame_stats_with_external
                      .frame_descriptor_content_write_feedback_uav,
                  " presentUs=", present_us,
                  " queueSignalUs=", queue_signal_us,
                  " queueWaitUs=", queue_wait_us,
                  " createResourceUs=", create_resource_us,
                  " createReservedResourceUs=", create_reserved_resource_us,
                  " createHeapUs=", create_heap_us,
                  " createPipelineUs=", create_pipeline_us,
                  " unscopedD3D12ApiUs=", unscoped_d3d12_api_us,
                  " cmdlistRecordUs=", cmdlist_record_us,
                  " apiGapBeforeExecuteCommandListsUs=",
                  api_gap_before_execute_command_lists_us,
                  " apiGapBeforePresentUs=", api_gap_before_present_us,
                  " apiGapBeforeQueueSignalUs=", api_gap_before_queue_signal_us,
                  " apiGapBeforeQueueWaitUs=", api_gap_before_queue_wait_us,
                  " apiGapBeforeCreateResourceUs=",
                  api_gap_before_create_resource_us,
                  " apiGapBeforeCreateReservedResourceUs=",
                  api_gap_before_create_reserved_resource_us,
                  " apiGapBeforeCreateHeapUs=",
                  api_gap_before_create_heap_us,
                  " apiGapBeforeCreatePipelineUs=",
                  api_gap_before_create_pipeline_us,
                  " apiGapBeforeUnscopedD3D12ApiUs=",
                  api_gap_before_unscoped_d3d12_api_us,
                  " apiGapBeforeCmdlistRecordUs=",
                  api_gap_before_cmdlist_record_us,
                  " apiGapBeforeFrameBoundaryUs=",
                  api_gap_before_frame_boundary_us,
                  " apiGapTotalUs=", api_gap_total_us,
                  " presentLatencyWaitUs=", present_latency_wait_us,
                  " accountedWallUs=", accounted_wall_us,
                  " frameBoundaryUnclassifiedUs=",
                  boundary_unclassified_us,
                  " overlapAdjustUs=", overlap_adjust_us,
                  " closureUs=", closure_us,
                  " asyncDrawableBlockingUs=", drawable_blocking_us,
                  " asyncFlushUs=", flush_us,
                  " asyncFlushKnownUs=", flush_known_us,
                  " asyncFlushUnscopedUs=", flush_unscoped_us,
                  " asyncFlushCollectUs=", flush_collect_us,
                  " asyncFlushRelationUs=", flush_relation_us,
                  " asyncFlushQueryUs=", flush_query_us,
                  " asyncFlushTimestampUs=", flush_timestamp_us,
                  " asyncFlushRenderUs=", flush_render_us,
                  " asyncFlushComputeUs=", flush_compute_us,
                  " asyncFlushBlitUs=", flush_blit_us,
                  " asyncFlushClearUs=", flush_clear_us,
                  " asyncFlushResolveUs=", flush_resolve_us,
                  " asyncFlushScalerUs=", flush_scaler_us,
                  " asyncFlushPresentUs=", flush_present_us,
                  " asyncFlushEventUs=", flush_event_us,
                  " asyncFlushSampleUs=", flush_sample_us,
                  " asyncFlushPendingFenceUs=", flush_pending_fence_us,
                  " asyncFlushSignalUs=", flush_signal_us,
                  " asyncFlushCleanupUs=", flush_cleanup_us,
                  " asyncCommitUs=", commit_us,
                  " asyncEncodePrepareUs=",
                  durationUs(frame_stats.encode_prepare_interval),
                  " asyncEncodeFlushUs=",
                  durationUs(frame_stats.encode_flush_interval),
                  " commandBuffers=", frame_stats.command_buffer_count,
                  " renderPasses=", frame_stats.render_pass_count,
                  " renderCommands=", frame_stats.render_command_count,
                  " renderPsoBinds=", frame_stats.render_pso_bind_count,
                  " computePasses=", compute_passes,
                  " computeCommands=", frame_stats.compute_command_count,
                  " blitPasses=", frame_stats.blit_pass_count,
                  " blitPassOptimized=", frame_stats.blit_pass_optimized,
                  " blitPassCmd=", frame_stats.blit_pass_with_commands_count,
                  " emptyBlitPasses=", frame_stats.blit_pass_empty_count,
                  " deferredFenceOnlyBlitPasses=",
                  frame_stats.blit_pass_deferred_fence_only_count,
                  " mergedFenceOnlyBlitPasses=",
                  frame_stats.blit_pass_merged_fence_only_count,
                  " barrierOnlyBlitPasses=",
                  frame_stats.blit_barrier_only_pass_count,
                  " separatorBlitPasses=",
                  frame_stats.blit_separator_pass_count,
                  " blitPassWaitFence=",
                  frame_stats.blit_pass_with_fence_wait_count,
                  " blitPassUpdateFence=",
                  frame_stats.blit_pass_with_fence_update_count,
                  " blitFenceWaitEntries=",
                  frame_stats.blit_fence_wait_entry_count,
                  " blitFenceUpdateEntries=",
                  frame_stats.blit_fence_update_entry_count,
                  " resourceBarrierBatchesMerged=",
                  frame_stats.resource_barrier_batches_merged,
                  " resourceBarrierBatchesGraphicsInlined=",
                  frame_stats.resource_barrier_batches_graphics_inlined,
                  " resourceBarrierBatchesComputeInlined=",
                  frame_stats.resource_barrier_batches_compute_inlined,
                  " blitCommands=", frame_stats.blit_command_count,
                  " blitB2B=", frame_stats.blit_copy_buffer_to_buffer_count,
                  " blitB2T=", frame_stats.blit_copy_buffer_to_texture_count,
                  " blitT2B=", frame_stats.blit_copy_texture_to_buffer_count,
                  " blitT2T=", frame_stats.blit_copy_texture_to_texture_count,
                  " blitFill=", frame_stats.blit_fill_buffer_count,
                  " blitResolveCounters=", frame_stats.blit_resolve_counters_count,
                  " blitWaitFence=", frame_stats.blit_fence_wait_count,
                  " blitUpdateFence=", frame_stats.blit_fence_update_count,
                  " presentPasses=", frame_stats.present_pass_count,
                  " clearPasses=", frame_stats.clear_pass_count,
                  " clearPassOptimized=", frame_stats.clear_pass_optimized,
                  " resolvePassOptimized=", frame_stats.resolve_pass_optimized,
                  " flushEncoders=", frame_stats.flush_encoder_count,
                  " encodedEncoders=", frame_stats.flush_encoded_encoder_count,
                  " flushNullEncoders=", frame_stats.flush_null_encoder_count,
                  " flushRenderEncoders=", frame_stats.flush_render_encoder_count,
                  " flushComputeEncoders=", frame_stats.flush_compute_encoder_count,
                  " flushBlitEncoders=", frame_stats.flush_blit_encoder_count,
                  " flushTimestampEncoders=",
                  frame_stats.flush_timestamp_encoder_count,
                  " renderDraws=", render_draws,
                  " computeDispatches=", frame_stats.compute_dispatch_count));

  maybeFlush(frame);
}

void flushFinal(uint64_t frame) {
  flushCounters(frame, true);
}

void recordWaitCpuFence(uint64_t wait_us) {
  if (!enabled() || !wait_us)
    return;
  g_counters.wait_cpu_fence_count.fetch_add(1, std::memory_order_relaxed);
  g_counters.wait_cpu_fence_us.fetch_add(wait_us, std::memory_order_relaxed);
  updateMax(g_counters.wait_cpu_fence_max_us, wait_us);
}

void recordTimestampGpuResolve(uint64_t queries) {
  if (!enabled())
    return;
  g_counters.timestamp_gpu_runs.fetch_add(1, std::memory_order_relaxed);
  g_counters.timestamp_gpu_queries.fetch_add(queries,
                                            std::memory_order_relaxed);
}

void recordTimestampCpuFallback(uint64_t queries) {
  if (!enabled())
    return;
  g_counters.timestamp_cpu_fallbacks.fetch_add(1, std::memory_order_relaxed);
  g_counters.timestamp_cpu_fallback_queries.fetch_add(
      queries, std::memory_order_relaxed);
}

void recordTimestampCpuDeferred(uint64_t queries) {
  if (!enabled())
    return;
  g_counters.timestamp_cpu_deferred.fetch_add(1, std::memory_order_relaxed);
  g_counters.timestamp_cpu_deferred_queries.fetch_add(
      queries, std::memory_order_relaxed);
}

void recordTimestampCpuImmediate(bool unsafe) {
  if (!enabled())
    return;
  g_counters.timestamp_cpu_immediate.fetch_add(1, std::memory_order_relaxed);
  if (unsafe)
    g_counters.timestamp_cpu_unsafe.fetch_add(1, std::memory_order_relaxed);
}

void recordTimestampCpuMaterialized() {
  if (!enabled())
    return;
  g_counters.timestamp_cpu_materialized.fetch_add(1,
                                                 std::memory_order_relaxed);
}

void recordTimestampCpuWait(uint64_t wait_us) {
  if (!enabled() || !wait_us)
    return;
  g_counters.timestamp_cpu_wait_us.fetch_add(wait_us,
                                            std::memory_order_relaxed);
  updateMax(g_counters.timestamp_cpu_wait_max_us, wait_us);
}

void recordQueryBatchWait(uint64_t batches, uint64_t queries,
                          uint64_t wait_us) {
  if (!enabled())
    return;
  g_counters.query_batch_waits.fetch_add(batches, std::memory_order_relaxed);
  g_counters.query_batch_wait_queries.fetch_add(queries,
                                               std::memory_order_relaxed);
  g_counters.query_batch_wait_us.fetch_add(wait_us, std::memory_order_relaxed);
  updateMax(g_counters.query_batch_wait_max_us, wait_us);
}

void recordTileMapping(uint64_t standard_ops, uint64_t packed_ops,
                       uint64_t map_ops, uint64_t unmap_ops,
                       uint64_t invalid, uint64_t metal_failures,
                       uint64_t barriers) {
  if (!enabled())
    return;
  g_counters.tile_mapping_calls.fetch_add(1, std::memory_order_relaxed);
  g_counters.tile_mapping_standard_ops.fetch_add(standard_ops,
                                                std::memory_order_relaxed);
  g_counters.tile_mapping_packed_ops.fetch_add(packed_ops,
                                              std::memory_order_relaxed);
  g_counters.tile_mapping_map_ops.fetch_add(map_ops,
                                           std::memory_order_relaxed);
  g_counters.tile_mapping_unmap_ops.fetch_add(unmap_ops,
                                             std::memory_order_relaxed);
  g_counters.tile_mapping_invalid.fetch_add(invalid,
                                           std::memory_order_relaxed);
  g_counters.tile_mapping_metal_failures.fetch_add(
      metal_failures, std::memory_order_relaxed);
  g_counters.tile_mapping_barriers.fetch_add(barriers,
                                            std::memory_order_relaxed);
}

void recordGraphicsPipelineCreate(uint64_t duration_us, bool success) {
  if (!enabled())
    return;
  g_counters.graphics_pso_creates.fetch_add(1, std::memory_order_relaxed);
  g_counters.graphics_pso_create_us.fetch_add(duration_us,
                                             std::memory_order_relaxed);
  updateMax(g_counters.graphics_pso_create_max_us, duration_us);
  if (!success)
    g_counters.graphics_pso_create_failures.fetch_add(
        1, std::memory_order_relaxed);
}

void recordComputePipelineCreate(uint64_t duration_us, bool success) {
  if (!enabled())
    return;
  g_counters.compute_pso_creates.fetch_add(1, std::memory_order_relaxed);
  g_counters.compute_pso_create_us.fetch_add(duration_us,
                                            std::memory_order_relaxed);
  updateMax(g_counters.compute_pso_create_max_us, duration_us);
  if (!success)
    g_counters.compute_pso_create_failures.fetch_add(
        1, std::memory_order_relaxed);
}

void recordMetalCommandBufferCommit(uint64_t duration_us) {
  if (!enabled())
    return;
  g_counters.metal_commit_count.fetch_add(1, std::memory_order_relaxed);
  g_counters.metal_commit_wall_us.fetch_add(duration_us,
                                           std::memory_order_relaxed);
  updateMax(g_counters.metal_commit_wall_max_us, duration_us);
}

void recordDrawableAcquire(uint64_t duration_us) {
  if (!enabled())
    return;
  g_counters.drawable_acquire_count.fetch_add(1, std::memory_order_relaxed);
  g_counters.drawable_acquire_us.fetch_add(duration_us,
                                           std::memory_order_relaxed);
  updateMax(g_counters.drawable_acquire_max_us, duration_us);
}

void recordDescriptorContentWrite(uint32_t kind) {
  if (!enabled())
    return;
  g_counters.descriptor_content_writes.fetch_add(1,
                                                 std::memory_order_relaxed);
  switch (kind) {
  case 1:
    g_counters.descriptor_content_write_cbv.fetch_add(
        1, std::memory_order_relaxed);
    break;
  case 2:
    g_counters.descriptor_content_write_srv.fetch_add(
        1, std::memory_order_relaxed);
    break;
  case 3:
    g_counters.descriptor_content_write_uav.fetch_add(
        1, std::memory_order_relaxed);
    break;
  case 4:
    g_counters.descriptor_content_write_sampler.fetch_add(
        1, std::memory_order_relaxed);
    break;
  case 5:
    g_counters.descriptor_content_write_copy.fetch_add(
        1, std::memory_order_relaxed);
    break;
  case 6:
    g_counters.descriptor_content_write_feedback_uav.fetch_add(
        1, std::memory_order_relaxed);
    break;
  default:
    break;
  }
}

void recordNativeDescriptorBufferRecord(uint32_t kind) {
  if (!enabled())
    return;
  g_counters.native_descriptor_buffer_records.fetch_add(
      1, std::memory_order_relaxed);
  switch (kind) {
  case 1:
    g_counters.native_descriptor_buffer_record_cbv.fetch_add(
        1, std::memory_order_relaxed);
    break;
  case 2:
    g_counters.native_descriptor_buffer_record_srv.fetch_add(
        1, std::memory_order_relaxed);
    break;
  case 3:
    g_counters.native_descriptor_buffer_record_uav.fetch_add(
        1, std::memory_order_relaxed);
    break;
  default:
    break;
  }
}

void recordNativeDescriptorBufferRecordCounter() {
  if (!enabled())
    return;
  g_counters.native_descriptor_buffer_record_counters.fetch_add(
      1, std::memory_order_relaxed);
}

void recordNativeDescriptorBufferRecordMissingResource() {
  if (!enabled())
    return;
  g_counters.native_descriptor_buffer_record_missing_resource.fetch_add(
      1, std::memory_order_relaxed);
}

void recordNativeDescriptorResourceTableEntry() {
  if (!enabled())
    return;
  g_counters.native_descriptor_resource_table_entries.fetch_add(
      1, std::memory_order_relaxed);
}

} // namespace dxmt::perf
