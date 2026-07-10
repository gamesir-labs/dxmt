#include <dxmt_benchmark.hpp>
#include <dxmt_test_scheduler.hpp>

#include <cstdint>
#include <string>
#include <vector>

static void BM_TestSchedulerPlan(benchmark::State &state) {
  const auto test_count = static_cast<std::size_t>(state.range(0));
  std::vector<dxmt::test::ScheduledTest> tests;
  tests.reserve(test_count);
  for (std::size_t index = 0; index < test_count; ++index) {
    const auto cost = index % 997 == 0 ? dxmt::test::kSlowTestCost
                                       : dxmt::test::kNormalTestCost;
    tests.push_back({"Subsystem.SyntheticCase" + std::to_string(index), cost});
  }

  for (auto _ : state) {
    const auto worker_count = dxmt::test::SelectWorkerCount(tests, 16);
    auto shards = dxmt::test::BuildTestShards(tests, worker_count);
    benchmark::DoNotOptimize(shards);
  }
  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(test_count));
}

BENCHMARK(BM_TestSchedulerPlan)->Arg(10'000);
