#include <dxmt_test.hpp>

#include "dxmt_perf_stats.hpp"

#include <chrono>
#include <cstddef>
#include <thread>

namespace {

using namespace std::chrono_literals;

TEST(PerfStats, ExclusiveCodeTimingSubtractsNestedWorkExactlyOnce) {
  dxmt::FrameStatistics stats;

  dxmt::perf::addExclusiveCodeTiming(
      &stats, dxmt::PerfCodePath::CommandListCloseControl, 100us, 80us);
  dxmt::perf::addExclusiveCodeTiming(
      &stats, dxmt::PerfCodePath::CommandListCloseBuildCompiled, 80us, 50us);
  dxmt::perf::addExclusiveCodeTiming(
      &stats, dxmt::PerfCodePath::CompiledBuildLoopDispatch, 50us, 0us);

  const auto close_control = static_cast<size_t>(
      dxmt::PerfCodePath::CommandListCloseControl);
  const auto close_build = static_cast<size_t>(
      dxmt::PerfCodePath::CommandListCloseBuildCompiled);
  const auto build_loop = static_cast<size_t>(
      dxmt::PerfCodePath::CompiledBuildLoopDispatch);

  EXPECT_EQ(stats.frame_code_path_intervals[close_control], 20us);
  EXPECT_EQ(stats.frame_code_path_intervals[close_build], 30us);
  EXPECT_EQ(stats.frame_code_path_intervals[build_loop], 50us);
  EXPECT_EQ(stats.frame_code_path_intervals[close_control] +
                stats.frame_code_path_intervals[close_build] +
                stats.frame_code_path_intervals[build_loop],
            100us);
}

TEST(PerfStats, ExclusiveCodeTimingTracksCallsAndMaximum) {
  dxmt::FrameStatistics stats;
  const auto path = dxmt::PerfCodePath::CompiledBuildNativeRootBaseRangeScan;
  const auto index = static_cast<size_t>(path);

  dxmt::perf::addExclusiveCodeTiming(&stats, path, 40us, 10us);
  dxmt::perf::addExclusiveCodeTiming(&stats, path, 15us, 5us);

  EXPECT_EQ(stats.frame_code_path_intervals[index], 40us);
  EXPECT_EQ(stats.frame_code_path_max_intervals[index], 30us);
  EXPECT_EQ(stats.frame_code_path_counts[index], 2u);
}

TEST(PerfStats, ExclusiveCodeTimingClampsClockSkewInsteadOfUnderflowing) {
  dxmt::FrameStatistics stats;
  const auto path = dxmt::PerfCodePath::CompiledBuildLoopDispatch;
  const auto index = static_cast<size_t>(path);

  dxmt::perf::addExclusiveCodeTiming(&stats, path, 5us, 9us);

  EXPECT_EQ(stats.frame_code_path_intervals[index], 0us);
  EXPECT_EQ(stats.frame_code_path_max_intervals[index], 0us);
  EXPECT_EQ(stats.frame_code_path_counts[index], 1u);
}

TEST(PerfStats, FrameStatisticsBindingCrossesWorkerThreadAndRestoresTls) {
  dxmt::FrameStatistics present_stats;
  dxmt::FrameStatistics worker_stats;
  auto *previous = dxmt::perf::currentFrameStatistics();

  {
    dxmt::perf::ScopedFrameStatisticsBinding present_binding(&present_stats);
    EXPECT_EQ(dxmt::perf::currentFrameStatistics(), &present_stats);

    std::thread worker([&]() {
      EXPECT_EQ(dxmt::perf::currentFrameStatistics(), nullptr);
      {
        dxmt::perf::ScopedFrameStatisticsBinding worker_binding(&worker_stats);
        EXPECT_EQ(dxmt::perf::currentFrameStatistics(), &worker_stats);
        dxmt::perf::addExclusiveCodeTiming(
            dxmt::perf::currentFrameStatistics(),
            dxmt::PerfCodePath::QueueExecuteControl, 25us, 5us);
      }
      EXPECT_EQ(dxmt::perf::currentFrameStatistics(), nullptr);
    });
    worker.join();

    EXPECT_EQ(dxmt::perf::currentFrameStatistics(), &present_stats);
  }

  EXPECT_EQ(dxmt::perf::currentFrameStatistics(), previous);
  const auto index =
      static_cast<size_t>(dxmt::PerfCodePath::QueueExecuteControl);
  EXPECT_EQ(worker_stats.frame_code_path_intervals[index], 20us);
  EXPECT_EQ(present_stats.frame_code_path_intervals[index], 0us);
}

TEST(PerfStats, ReplayWorkerSummaryIncludesSubFiftyMillisecondBatches) {
  dxmt::FrameStatistics stats;
  stats.frame_execute_replay_interval = 49ms;
  stats.frame_replay_record_loop_interval = 30ms;
  stats.frame_replay_flush_pass_interval = 10ms;
  stats.frame_replay_timestamp_resolve_interval = 5ms;
  stats.frame_replay_cpu_query_resolve_interval = 3ms;
  stats.frame_replay_record_draw_interval = 10ms;
  stats.frame_replay_record_draw_indexed_interval = 20ms;

  const auto summary = dxmt::perf::summarizeReplayWorkerFrame(stats);
  EXPECT_EQ(summary.execute_replay_us, 49000u);
  EXPECT_EQ(summary.replay_timed_us, 48000u);
  EXPECT_EQ(summary.record_loop_us, 30000u);
  EXPECT_EQ(summary.typed_record_us, 30000u);
  EXPECT_EQ(summary.replay_coverage_permille, 979u);
  EXPECT_EQ(summary.record_coverage_permille, 1000u);
}

TEST(PerfStats, ReplayWorkerSummaryClassifiesCompiledPacketAndControlTime) {
  dxmt::FrameStatistics stats;
  stats.frame_execute_replay_interval = 42ms;
  stats.frame_replay_record_loop_interval = 40ms;
  stats.frame_replay_superseded_mask_interval = 2ms;
  stats.frame_replay_compiled_graphics_interval = 18ms;
  stats.frame_replay_compiled_compute_interval = 7ms;
  stats.frame_replay_fallback_classification_interval = 1ms;
  stats.frame_replay_record_draw_interval = 5ms;
  stats.frame_replay_record_dispatch_interval = 3ms;

  const auto summary = dxmt::perf::summarizeReplayWorkerFrame(stats);
  EXPECT_EQ(summary.superseded_mask_us, 2000u);
  EXPECT_EQ(summary.compiled_graphics_us, 18000u);
  EXPECT_EQ(summary.compiled_compute_us, 7000u);
  EXPECT_EQ(summary.fallback_classification_us, 1000u);
  EXPECT_EQ(summary.typed_record_us, 8000u);
  EXPECT_EQ(summary.record_control_us, 4000u);
  EXPECT_EQ(summary.classified_record_us, 40000u);
  EXPECT_EQ(summary.record_coverage_permille, 1000u);
}

TEST(PerfStats, ReplayWorkerSummaryClampsCoverageAndHandlesEmptyFrames) {
  dxmt::FrameStatistics empty;
  const auto empty_summary = dxmt::perf::summarizeReplayWorkerFrame(empty);
  EXPECT_EQ(empty_summary.replay_coverage_permille, 0u);
  EXPECT_EQ(empty_summary.record_coverage_permille, 0u);

  dxmt::FrameStatistics overclassified;
  overclassified.frame_execute_replay_interval = 10us;
  overclassified.frame_replay_record_loop_interval = 5us;
  overclassified.frame_replay_flush_pass_interval = 20us;
  overclassified.frame_replay_compiled_graphics_interval = 9us;
  overclassified.frame_replay_record_draw_interval = 7us;
  const auto summary =
      dxmt::perf::summarizeReplayWorkerFrame(overclassified);
  EXPECT_EQ(summary.record_control_us, 0u);
  EXPECT_EQ(summary.classified_record_us, 5u);
  EXPECT_EQ(summary.replay_coverage_permille, 1000u);
  EXPECT_EQ(summary.record_coverage_permille, 1000u);
}

TEST(PerfStats, FrameStatisticsContainerExcludesCurrentFrameAndAveragesAllPasses) {
  dxmt::FrameStatisticsContainer statistics;
  for (uint32_t index = 0; index < dxmt::kFrameStatisticsCount; ++index) {
    auto &frame = statistics.at(index);
    const uint32_t value = index + 1;
    frame.command_buffer_count = value;
    frame.sync_count = value * 2;
    frame.event_stall = value * 3;
    frame.commit_interval = std::chrono::microseconds(value);
    frame.sync_interval = std::chrono::microseconds(value * 2);
    frame.encode_prepare_interval = std::chrono::microseconds(value * 3);
    frame.encode_flush_interval = std::chrono::microseconds(value * 4);
    frame.drawable_blocking_interval = std::chrono::microseconds(value * 5);
    frame.present_latency_interval = std::chrono::microseconds(value * 6);
    frame.present_pass_count = value;
    frame.clear_pass_count = value;
    frame.clear_pass_optimized = value;
    frame.resolve_pass_optimized = value;
    frame.compute_pass_count = value;
    frame.blit_pass_count = value;
    frame.latency = value;
    frame.shader_binding_upload_count = value;
    frame.shader_binding_dirty_cbuffer_count = value;
    frame.shader_binding_dirty_sampler_count = value;
    frame.shader_binding_dirty_srv_count = value;
    frame.shader_binding_dirty_uav_count = value;
    frame.shader_binding_clean_uav_count = value;
  }

  statistics.compute(dxmt::kFrameStatisticsCount - 1);

  EXPECT_EQ(statistics.min().command_buffer_count, 1u);
  EXPECT_EQ(statistics.min().sync_count, 2u);
  EXPECT_EQ(statistics.min().event_stall, 3u);
  EXPECT_EQ(statistics.min().commit_interval, 1us);
  EXPECT_EQ(statistics.max().command_buffer_count, 15u);
  EXPECT_EQ(statistics.max().sync_count, 30u);
  EXPECT_EQ(statistics.max().event_stall, 45u);
  EXPECT_EQ(statistics.max().present_latency_interval, 90us);

  const auto &average = statistics.average();
  EXPECT_EQ(average.command_buffer_count, 8u);
  EXPECT_EQ(average.sync_count, 16u);
  EXPECT_EQ(average.event_stall, 24u);
  EXPECT_EQ(average.commit_interval, 8us);
  EXPECT_EQ(average.present_pass_count, 8u);
  EXPECT_EQ(average.clear_pass_count, 8u);
  EXPECT_EQ(average.clear_pass_optimized, 8u);
  EXPECT_EQ(average.resolve_pass_optimized, 8u);
  EXPECT_EQ(average.compute_pass_count, 8u);
  EXPECT_EQ(average.blit_pass_count, 8u);
  EXPECT_EQ(average.latency, 8u);
  EXPECT_EQ(average.shader_binding_upload_count, 8u);
  EXPECT_EQ(average.shader_binding_dirty_cbuffer_count, 8u);
  EXPECT_EQ(average.shader_binding_dirty_sampler_count, 8u);
  EXPECT_EQ(average.shader_binding_dirty_srv_count, 8u);
  EXPECT_EQ(average.shader_binding_dirty_uav_count, 8u);
  EXPECT_EQ(average.shader_binding_clean_uav_count, 8u);
}

} // namespace
