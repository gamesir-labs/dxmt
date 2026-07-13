#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dxmt::test {

inline constexpr std::uint32_t kNormalTestCost = 1;
inline constexpr std::uint32_t kSlowTestCost = 100;
inline constexpr std::uint32_t kMinimumBatchCost = 4;

struct ScheduledTest {
  std::string name;
  std::uint32_t cost = kNormalTestCost;
  bool serial = false;
};

struct TestShard {
  std::vector<std::string> tests;
  std::uint64_t estimated_cost = 0;
};

class TestCostRegistration {
public:
  TestCostRegistration(std::string_view pattern, std::uint32_t cost);
};

class SerialTestRegistration {
public:
  explicit SerialTestRegistration(std::string_view pattern);
};

bool GlobMatches(std::string_view pattern, std::string_view value);
bool FilterMatches(std::string_view filter, std::string_view test_name);
std::size_t SelectWorkerCount(const std::vector<ScheduledTest> &tests,
                              std::size_t maximum_worker_count);
std::vector<ScheduledTest>
ExtractSerialTests(std::vector<ScheduledTest> &tests);
std::vector<TestShard> BuildTestShards(std::vector<ScheduledTest> tests,
                                       std::size_t worker_count);
int RunScheduledTests(int argc, char **argv);

} // namespace dxmt::test

#define DXMT_TEST_CONCAT_INNER_(left, right) left##right
#define DXMT_TEST_CONCAT_(left, right) DXMT_TEST_CONCAT_INNER_(left, right)

#define DXMT_SLOW_TEST(test_suite_name, test_name)                             \
  static const ::dxmt::test::TestCostRegistration DXMT_TEST_CONCAT_(           \
      dxmt_slow_test_, __LINE__)(#test_suite_name "." #test_name,              \
                                 ::dxmt::test::kSlowTestCost);                 \
  TEST(test_suite_name, test_name)

#define DXMT_SLOW_TEST_F(test_fixture, test_name)                              \
  static const ::dxmt::test::TestCostRegistration DXMT_TEST_CONCAT_(           \
      dxmt_slow_test_fixture_, __LINE__)(#test_fixture "." #test_name,         \
                                         ::dxmt::test::kSlowTestCost);         \
  TEST_F(test_fixture, test_name)

#define DXMT_SLOW_TEST_PATTERN(pattern)                                        \
  static const ::dxmt::test::TestCostRegistration DXMT_TEST_CONCAT_(           \
      dxmt_slow_test_pattern_, __LINE__)(pattern, ::dxmt::test::kSlowTestCost)

#define DXMT_SERIAL_TEST_F(test_fixture, test_name)                            \
  static const ::dxmt::test::SerialTestRegistration DXMT_TEST_CONCAT_(         \
      dxmt_serial_test_fixture_, __LINE__)(#test_fixture "." #test_name);      \
  TEST_F(test_fixture, test_name)
