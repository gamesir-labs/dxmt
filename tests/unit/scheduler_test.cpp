#include <dxmt_test.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

namespace {

DXMT_SLOW_TEST_PATTERN("NoSuchParameterizedTest.*");

DXMT_SLOW_TEST(TestSchedulerSlowMarker, DISABLED_CompilesTestMacro) {
  SUCCEED();
}

class TestSchedulerSlowFixture : public ::testing::Test {};

DXMT_SLOW_TEST_F(TestSchedulerSlowFixture, DISABLED_CompilesFixtureMacro) {
  SUCCEED();
}

TEST(TestSchedulerGlob, MatchesGoogleTestWildcards) {
  EXPECT_TRUE(
      dxmt::test::GlobMatches("Pipeline*.*Cache?", "PipelineState.DiskCache1"));
  EXPECT_TRUE(dxmt::test::GlobMatches("*", "AnySuite.AnyTest"));
  EXPECT_FALSE(
      dxmt::test::GlobMatches("Pipeline?.Cache", "PipelineState.Cache"));
}

TEST(TestSchedulerFilter, AppliesPositiveAndNegativePatterns) {
  EXPECT_TRUE(
      dxmt::test::FilterMatches("Pipeline*.*-*.Slow", "PipelineState.Fast"));
  EXPECT_FALSE(
      dxmt::test::FilterMatches("Pipeline*.*-*.Slow", "PipelineState.Slow"));
  EXPECT_TRUE(dxmt::test::FilterMatches("-*Disabled*", "PipelineState.Fast"));
}

TEST(TestSchedulerPlan, BalancesSlowHintsBeforeFastTests) {
  std::vector<dxmt::test::ScheduledTest> tests = {
      {"Suite.SlowA", dxmt::test::kSlowTestCost},
      {"Suite.SlowB", dxmt::test::kSlowTestCost},
      {"Suite.FastA", dxmt::test::kNormalTestCost},
      {"Suite.FastB", dxmt::test::kNormalTestCost},
      {"Suite.FastC", dxmt::test::kNormalTestCost},
      {"Suite.FastD", dxmt::test::kNormalTestCost},
  };

  const auto shards = dxmt::test::BuildTestShards(std::move(tests), 2);

  ASSERT_EQ(shards.size(), 2u);
  EXPECT_EQ(shards[0].estimated_cost, shards[1].estimated_cost);
  EXPECT_EQ(shards[0].tests.size(), 3u);
  EXPECT_EQ(shards[1].tests.size(), 3u);
}

TEST(TestSchedulerPlan, CapsWorkersAtRunnableTestCount) {
  std::vector<dxmt::test::ScheduledTest> tests = {
      {"Suite.First", dxmt::test::kNormalTestCost},
      {"Suite.Second", dxmt::test::kNormalTestCost},
  };

  const auto shards = dxmt::test::BuildTestShards(std::move(tests), 64);

  ASSERT_EQ(shards.size(), 2u);
  EXPECT_TRUE(std::ranges::all_of(
      shards, [](const auto &shard) { return shard.tests.size() == 1; }));
}

TEST(TestSchedulerPlan, AmortizesDefaultWorkerLaunches) {
  std::vector<dxmt::test::ScheduledTest> tests(
      40, {"Suite.Fast", dxmt::test::kNormalTestCost});
  EXPECT_EQ(dxmt::test::SelectWorkerCount(tests, 16), 10u);

  tests.resize(100, {"Suite.Fast", dxmt::test::kNormalTestCost});
  EXPECT_EQ(dxmt::test::SelectWorkerCount(tests, 16), 16u);
}

TEST(TestSchedulerWorker, PropagatesConditionalFailure) {
  if (std::getenv("DXMT_TEST_INJECT_SCHEDULER_FAILURE") != nullptr)
    FAIL() << "injected worker failure";
}

class SchedulerParameterizedSmoke : public ::testing::TestWithParam<int> {};

TEST_P(SchedulerParameterizedSmoke, RunsEveryDiscoveredCase) {
  EXPECT_GE(GetParam(), 0);
  EXPECT_LT(GetParam(), 32);
}

INSTANTIATE_TEST_SUITE_P(ParallelBatch, SchedulerParameterizedSmoke,
                         ::testing::Range(0, 32));

} // namespace
