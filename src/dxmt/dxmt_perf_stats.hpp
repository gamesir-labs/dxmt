#pragma once

#include "dxmt_statistics.hpp"
#include <cstdint>

namespace dxmt::perf {

bool enabled();

void recordFrameBoundary(uint64_t frame);
void recordFrameBoundary(uint64_t frame, const FrameStatistics &frame_stats,
                         const FrameStatistics &average_stats,
                         uint64_t frame_wall_us);
void recordWaitCpuFence(uint64_t wait_us);

void recordTimestampGpuResolve(uint64_t queries);
void recordTimestampCpuFallback(uint64_t queries);
void recordTimestampCpuDeferred(uint64_t queries);
void recordTimestampCpuImmediate(bool unsafe);
void recordTimestampCpuMaterialized();
void recordTimestampCpuWait(uint64_t wait_us);
void recordQueryBatchWait(uint64_t batches, uint64_t queries,
                          uint64_t wait_us);

void recordGraphicsPipelineCreate(uint64_t duration_us, bool success);
void recordComputePipelineCreate(uint64_t duration_us, bool success);

} // namespace dxmt::perf
