#include "dxmt_perf_stats.hpp"

#include "log/log.hpp"
#include "util_env.hpp"
#include "util_string.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>

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
};

Counters g_counters;
dxmt::mutex g_flush_mutex;

bool parseEnabledEnv(const char *name) {
  const auto value = env::getEnvVar(name);
  return value == "1" || value == "true" || value == "yes" ||
         value == "on";
}

uint64_t parseUintEnv(const char *name, uint64_t fallback,
                      uint64_t minimum, uint64_t maximum) {
  const auto value = env::getEnvVar(name);
  if (value.empty())
    return fallback;
  char *end = nullptr;
  const auto parsed = std::strtoull(value.c_str(), &end, 10);
  if (!end || *end || parsed < minimum)
    return fallback;
  return std::min<uint64_t>(parsed, maximum);
}

uint64_t sample(std::atomic<uint64_t> &value) {
  return value.load(std::memory_order_relaxed);
}

void updateMax(std::atomic<uint64_t> &target, uint64_t value) {
  auto current = target.load(std::memory_order_relaxed);
  while (current < value &&
         !target.compare_exchange_weak(current, value,
                                       std::memory_order_relaxed)) {
  }
}

void maybeFlush(uint64_t frame) {
  if (!enabled())
    return;

  static const uint64_t interval =
      parseUintEnv("DXMT_PERF_STATS_INTERVAL_FRAMES", 120, 1, 10000);
  if (frame == 0 || frame % interval)
    return;

  std::lock_guard lock(g_flush_mutex);
  Logger::logFileOnly(
      LogLevel::Info,
      str::format("DXMT perf stats:"
                  " frame=", frame,
                  " windowFrames=", interval,
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
                  sample(g_counters.query_batch_wait_max_us)));
}

} // namespace

bool enabled() {
  static const bool result = parseEnabledEnv("DXMT_PERF_STATS");
  return result;
}

void recordFrameBoundary(uint64_t frame) {
  if (!enabled())
    return;
  g_counters.frames.fetch_add(1, std::memory_order_relaxed);
  maybeFlush(frame);
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

} // namespace dxmt::perf
