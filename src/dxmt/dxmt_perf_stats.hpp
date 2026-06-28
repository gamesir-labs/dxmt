#pragma once

#include "dxmt_statistics.hpp"
#include <cstdint>

namespace dxmt::perf {

bool enabled();

enum class FrameTimeBucket : uint32_t {
  ExecuteCommandLists,
  Present,
  QueueSignal,
  QueueWait,
  CreateResource,
  CreateReservedResource,
  CreateHeap,
  CreatePipeline,
  UnscopedD3D12Api,
  // Time spent in AddRecord recording CommandRecords on the calling thread. If
  // this accumulates on the present thread, it is part of the measured frame
  // wall and must be optimized as DXMT recording work. If it stays zero,
  // recording runs outside the measured frame boundary.
  CommandListRecord,
};

enum class ExecuteTimeBucket : uint32_t {
  Validate,
  Collect,
  Enqueue,
  Drain,
  DrainLock,
  Replay,
  Decay,
  Signal,
  Commit,
  WaitArm,
};

class ScopedFrameTimer {
public:
  explicit ScopedFrameTimer(FrameTimeBucket bucket);
  ~ScopedFrameTimer();
  void stop();

  ScopedFrameTimer(const ScopedFrameTimer &) = delete;
  ScopedFrameTimer &operator=(const ScopedFrameTimer &) = delete;

private:
  FrameTimeBucket bucket_;
  FrameStatistics *stats_;
  dxmt::clock::time_point begin_;
  bool active_;
};

void setCurrentFrameStatistics(FrameStatistics *stats);
FrameStatistics *currentFrameStatistics();
void recordExecuteTime(FrameStatistics *stats, ExecuteTimeBucket bucket,
                       dxmt::clock::duration duration);
void recordReplayBreakdown(FrameStatistics *stats,
                           dxmt::clock::duration record_loop,
                           dxmt::clock::duration flush_pass,
                           dxmt::clock::duration timestamp_resolve,
                           dxmt::clock::duration cpu_query_resolve);
void recordFrameBoundary(uint64_t frame);
void recordFrameBoundary(uint64_t frame, const FrameStatistics &frame_stats,
                         const FrameStatistics &average_stats,
                         uint64_t frame_wall_us);
void flushFinal(uint64_t frame);
void recordWaitCpuFence(uint64_t wait_us);

void recordTimestampGpuResolve(uint64_t queries);
void recordTimestampCpuFallback(uint64_t queries);
void recordTimestampCpuDeferred(uint64_t queries);
void recordTimestampCpuImmediate(bool unsafe);
void recordTimestampCpuMaterialized();
void recordTimestampCpuWait(uint64_t wait_us);
void recordQueryBatchWait(uint64_t batches, uint64_t queries,
                          uint64_t wait_us);

void recordTileMapping(uint64_t standard_ops, uint64_t packed_ops,
                       uint64_t map_ops, uint64_t unmap_ops,
                       uint64_t invalid, uint64_t metal_failures,
                       uint64_t barriers);

void recordGraphicsPipelineCreate(uint64_t duration_us, bool success);
void recordComputePipelineCreate(uint64_t duration_us, bool success);
void recordMetalCommandBufferCommit(uint64_t duration_us);
void recordDrawableAcquire(uint64_t duration_us);
void recordDescriptorContentWrite(uint32_t kind);

} // namespace dxmt::perf
