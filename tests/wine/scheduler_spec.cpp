#include <dxmt_test.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int conditional_failure_count = 0;

DXMT_SLOW_TEST_PATTERN("NoSuchParameterizedTest.*");

DXMT_SLOW_TEST(TestSchedulerSlowMarker, DISABLED_CompilesTestMacro) {
  SUCCEED();
}

class TestSchedulerSlowFixture : public ::testing::Test {};

DXMT_SLOW_TEST_F(TestSchedulerSlowFixture, DISABLED_CompilesFixtureMacro) {
  SUCCEED();
}

DXMT_SERIAL_TEST_F(TestSchedulerSlowFixture, DISABLED_CompilesSerialMacro) {
  SUCCEED();
}

DXMT_GROUP_SERIAL_TESTS("TestSchedulerSlowFixture.*", "scheduler-fixture");

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

TEST(TestSchedulerCaseIdentity, DerivesStableNamespaceFromExecutable) {
  EXPECT_EQ(dxmt::test::CaseNamespaceFromExecutable(
                "Z:\\build\\dxmt-wine-d3d12-tests.exe"),
            "D3D12");
  EXPECT_EQ(dxmt::test::CaseNamespaceFromExecutable(
                "/tmp/dxmt-wine-framework-tests.exe"),
            "FRAMEWORK");
  EXPECT_EQ(dxmt::test::CaseNamespaceFromExecutable("custom-runner"),
            "CUSTOM_RUNNER");
  EXPECT_EQ(dxmt::test::CaseNamespaceFromExecutable(""), "DXMT");
}

TEST(TestSchedulerCaseIdentity, BuildsGlobFilterableGlobalCaseId) {
  const auto case_id = dxmt::test::CaseIdForTest(
      "D3D12", "CopyTextureSpec.FullFootprintMatrix");
  EXPECT_EQ(case_id, "D3D12.CopyTextureSpec.FullFootprintMatrix");
  EXPECT_TRUE(dxmt::test::FilterMatches(
      "D3D12.Copy*.*-*.Slow", case_id));
  EXPECT_FALSE(dxmt::test::FilterMatches("D3D11.*", case_id));
}

dxmt::test::LogicalCaseFamily ExampleLogicalFamily() {
  return {"BatchSpec.CopiesRegions",
          "D3D12.Copy.Buffer.ShuffledRegion.",
          4096,
          4,
          {dxmt::test::TestClass::Conformance,
           dxmt::test::ExecutionPath::Both,
           {"12_0", "5_0", "Direct", "CopyBufferRegion"},
           dxmt::test::kGpuBatchTestCost,
           "upload source and output atlas",
           "copy shuffled regions then read back",
           "each destination equals its source value",
           "first mismatch, offsets, expected, actual, replay"}};
}

TEST(TestSchedulerLogicalCaseIdentity, BuildsStableZeroPaddedIds) {
  const auto family = ExampleLogicalFamily();
  EXPECT_EQ(dxmt::test::LogicalCaseId(family, 0),
            "D3D12.Copy.Buffer.ShuffledRegion.0000");
  EXPECT_EQ(dxmt::test::LogicalCaseId(family, 42),
            "D3D12.Copy.Buffer.ShuffledRegion.0042");
  EXPECT_EQ(dxmt::test::LogicalCaseId(family, 4095),
            "D3D12.Copy.Buffer.ShuffledRegion.4095");
}

TEST(TestSchedulerLogicalCaseIdentity, SelectsFamilyByChildOrOwnerId) {
  const auto family = ExampleLogicalFamily();
  EXPECT_TRUE(dxmt::test::LogicalCaseMatchesFilter(
      family, 42, "D3D12", "D3D12.Copy.Buffer.ShuffledRegion.0042"));
  EXPECT_FALSE(dxmt::test::LogicalCaseMatchesFilter(
      family, 41, "D3D12", "D3D12.Copy.Buffer.ShuffledRegion.0042"));
  EXPECT_TRUE(dxmt::test::LogicalCaseFamilyMatchesFilter(
      family, "D3D12", "D3D12.Copy.Buffer.ShuffledRegion.4095"));
  EXPECT_TRUE(dxmt::test::LogicalCaseMatchesFilter(
      family, 41, "D3D12", "D3D12.BatchSpec.CopiesRegions"));
  EXPECT_FALSE(dxmt::test::LogicalCaseFamilyMatchesFilter(
      family, "D3D12", "D3D12.Shader.*"));
}

TEST(TestSchedulerLogicalCaseMetadata, EmitsRequiredTraitsAsJson) {
  const auto metadata =
      dxmt::test::LogicalCaseMetadataJson(ExampleLogicalFamily(), 42);
  EXPECT_NE(metadata.find("\"CaseId\":\"D3D12.Copy.Buffer.ShuffledRegion.0042\""),
            std::string::npos);
  EXPECT_NE(metadata.find("\"Class\":\"Conformance\""),
            std::string::npos);
  EXPECT_NE(metadata.find("\"ExecutionPath\":\"Both\""),
            std::string::npos);
  EXPECT_NE(metadata.find("\"MinimumFeatureLevel\":\"12_0\""),
            std::string::npos);
  EXPECT_NE(metadata.find("\"Setup\":\"upload source and output atlas\""),
            std::string::npos);
  EXPECT_NE(metadata.find("\"OperationSequence\":"), std::string::npos);
  EXPECT_NE(metadata.find("\"Oracle\":"), std::string::npos);
  EXPECT_NE(metadata.find("\"DiagnosticState\":"), std::string::npos);
  EXPECT_NE(metadata.find("\"Cost\":4"), std::string::npos);
}

TEST(TestSchedulerPolicy, DisablesFailureShortCircuiting) {
  EXPECT_FALSE(GTEST_FLAG_GET(fail_fast));
  EXPECT_FALSE(GTEST_FLAG_GET(break_on_failure));
  EXPECT_FALSE(GTEST_FLAG_GET(throw_on_failure));
  EXPECT_TRUE(GTEST_FLAG_GET(catch_exceptions));
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

TEST(TestSchedulerPlan, ExtractsSerialTestsIntoAnExclusiveWave) {
  std::vector<dxmt::test::ScheduledTest> tests = {
      {"Suite.Parallel", dxmt::test::kNormalTestCost, false},
      {"Suite.Serial", dxmt::test::kNormalTestCost, true},
      {"Suite.AlsoParallel", dxmt::test::kNormalTestCost, false},
  };
  const auto serial = dxmt::test::ExtractSerialTests(tests);
  ASSERT_EQ(serial.size(), 1u);
  EXPECT_EQ(serial[0].name, "Suite.Serial");
  ASSERT_EQ(tests.size(), 2u);
  EXPECT_FALSE(tests[0].serial);
  EXPECT_FALSE(tests[1].serial);
}

TEST(TestSchedulerPlan, ReusesWorkersWithinNamedSerialGroups) {
  std::vector<dxmt::test::ScheduledTest> tests = {
      {"Suite.GroupAFirst", 2, true, "group-a"},
      {"Suite.IsolatedFirst", 3, true},
      {"Suite.GroupB", 5, true, "group-b"},
      {"Suite.GroupASecond", 7, true, "group-a"},
      {"Suite.IsolatedSecond", 11, true},
  };

  const auto shards = dxmt::test::BuildSerialTestShards(std::move(tests));

  ASSERT_EQ(shards.size(), 4u);
  EXPECT_EQ(shards[0].tests,
            (std::vector<std::string>{"Suite.GroupAFirst",
                                      "Suite.GroupASecond"}));
  EXPECT_EQ(shards[0].estimated_cost, 9u);
  EXPECT_EQ(shards[1].tests,
            (std::vector<std::string>{"Suite.IsolatedFirst"}));
  EXPECT_EQ(shards[2].tests,
            (std::vector<std::string>{"Suite.GroupB"}));
  EXPECT_EQ(shards[3].tests,
            (std::vector<std::string>{"Suite.IsolatedSecond"}));
}

TEST(TestSchedulerPlan, RunsDistinctSerialDomainsInBoundedWaves) {
  const std::vector<dxmt::test::TestShard> shards = {
      {{"Suite.Swapchain"}, 10, "swapchain"},
      {{"Suite.PathFirst"}, 8, "execution-path"},
      {{"Suite.PathSecond"}, 7, "execution-path"},
      {{"Suite.Isolated"}, 6, {}},
      {{"Suite.Descriptor"}, 5, "descriptor"},
  };

  const auto waves = dxmt::test::BuildSerialShardWaves(shards, 2);

  ASSERT_EQ(waves.size(), 3u);
  EXPECT_EQ(waves[0], (std::vector<std::size_t>{0, 1}));
  EXPECT_EQ(waves[1], (std::vector<std::size_t>{2, 4}));
  EXPECT_EQ(waves[2], (std::vector<std::size_t>{3}));
}

TEST(TestSchedulerWorker, PropagatesConditionalFailure) {
  if (std::getenv("DXMT_TEST_INJECT_SCHEDULER_FAILURE") != nullptr) {
    ++conditional_failure_count;
    FAIL() << "injected worker failure";
  }
}

TEST(TestSchedulerWorker, ContinuesAfterFirstConditionalFailure) {
  if (std::getenv("DXMT_TEST_INJECT_SCHEDULER_FAILURE") != nullptr) {
    EXPECT_EQ(conditional_failure_count, 1);
    ++conditional_failure_count;
    FAIL() << "second injected worker failure";
  }
}

TEST(TestSchedulerWorker, CatchesConditionalException) {
  if (std::getenv("DXMT_TEST_INJECT_SCHEDULER_FAILURE") != nullptr) {
    EXPECT_EQ(conditional_failure_count, 2);
    ++conditional_failure_count;
    throw std::runtime_error("injected worker exception");
  }
}

TEST(TestSchedulerWorker, ReachesTestsAfterCaughtException) {
  if (std::getenv("DXMT_TEST_INJECT_SCHEDULER_FAILURE") != nullptr) {
    EXPECT_EQ(conditional_failure_count, 3);
  }
}

TEST(TestSchedulerWorker, ReportsIndependentConditionalFailure) {
  if (std::getenv("DXMT_TEST_INJECT_SCHEDULER_FAILURE") != nullptr) {
    FAIL() << "independent injected worker failure";
  }
}

class SchedulerParameterizedSmoke : public ::testing::TestWithParam<int> {};

TEST_P(SchedulerParameterizedSmoke, RunsEveryDiscoveredCase) {
  EXPECT_GE(GetParam(), 0);
  EXPECT_LT(GetParam(), 32);
}

INSTANTIATE_TEST_SUITE_P(ParallelBatch, SchedulerParameterizedSmoke,
                         ::testing::Range(0, 32));

} // namespace
