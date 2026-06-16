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
  std::atomic<uint64_t> frame_blit_barrier_only_passes = {0};
  std::atomic<uint64_t> frame_blit_separator_passes = {0};
  std::atomic<uint64_t> frame_resource_barrier_batches_merged = {0};
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
  case FrameTimeBucket::OtherD3D12:
    stats.frame_other_d3d12_interval += duration;
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
                  " frameBlitBarrierOnlyPasses=",
                  sample(g_counters.frame_blit_barrier_only_passes),
                  " frameBlitSeparatorPasses=",
                  sample(g_counters.frame_blit_separator_passes),
                  " frameResourceBarrierBatchesMerged=",
                  sample(g_counters.frame_resource_barrier_batches_merged),
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
}

ScopedFrameTimer::~ScopedFrameTimer() {
  stop();
}

void ScopedFrameTimer::stop() {
  if (!active_)
    return;
  addDurationToBucket(*stats_, bucket_, dxmt::clock::now() - begin_);
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

void recordExecuteTime(FrameStatistics *stats, ExecuteTimeBucket bucket,
                       dxmt::clock::duration duration) {
  if (!enabled() || !stats)
    return;
  addDurationToBucket(*stats, bucket, duration);
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
  const auto other_d3d12_us =
      durationUs(frame_stats.frame_other_d3d12_interval);
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
  const auto execute_known_us =
      execute_validate_us + execute_collect_us + execute_enqueue_us +
      execute_drain_us;
  const auto execute_other_us = execute_command_lists_us > execute_known_us
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
  const auto flush_other_us =
      flush_us > flush_known_us ? flush_us - flush_known_us : 0;
  const auto accounted_wall_us =
      execute_command_lists_us + present_us + queue_signal_us + queue_wait_us +
      create_resource_us + create_reserved_resource_us + create_heap_us +
      create_pipeline_us + other_d3d12_us + present_latency_wait_us;
  const auto other_wall_us =
      frame_wall_us > accounted_wall_us ? frame_wall_us - accounted_wall_us : 0;
  const auto overlap_adjust_us =
      accounted_wall_us > frame_wall_us ? accounted_wall_us - frame_wall_us : 0;
  const auto closure_us = accounted_wall_us + other_wall_us - overlap_adjust_us;
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
  g_counters.frame_blit_barrier_only_passes.fetch_add(
      frame_stats.blit_barrier_only_pass_count, std::memory_order_relaxed);
  g_counters.frame_blit_separator_passes.fetch_add(
      frame_stats.blit_separator_pass_count, std::memory_order_relaxed);
  g_counters.frame_resource_barrier_batches_merged.fetch_add(
      frame_stats.resource_barrier_batches_merged, std::memory_order_relaxed);
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
                  " executeOtherUs=", execute_other_us,
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
                  " presentUs=", present_us,
                  " queueSignalUs=", queue_signal_us,
                  " queueWaitUs=", queue_wait_us,
                  " createResourceUs=", create_resource_us,
                  " createReservedResourceUs=", create_reserved_resource_us,
                  " createHeapUs=", create_heap_us,
                  " createPipelineUs=", create_pipeline_us,
                  " otherD3D12Us=", other_d3d12_us,
                  " presentLatencyWaitUs=", present_latency_wait_us,
                  " accountedWallUs=", accounted_wall_us,
                  " otherWallUs=", other_wall_us,
                  " overlapAdjustUs=", overlap_adjust_us,
                  " closureUs=", closure_us,
                  " asyncDrawableBlockingUs=", drawable_blocking_us,
                  " asyncFlushUs=", flush_us,
                  " asyncFlushKnownUs=", flush_known_us,
                  " asyncFlushOtherUs=", flush_other_us,
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

} // namespace dxmt::perf
