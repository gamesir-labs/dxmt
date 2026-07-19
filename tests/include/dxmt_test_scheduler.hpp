#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dxmt::test {

inline constexpr std::uint32_t kNormalTestCost = 1;
inline constexpr std::uint32_t kResourceTestCost = 2;
inline constexpr std::uint32_t kGpuBatchTestCost = 4;
inline constexpr std::uint32_t kMultiSubmissionTestCost = 8;
inline constexpr std::uint32_t kFreshDeviceTestCost = 16;
inline constexpr std::uint32_t kProcessIsolationTestCost = 32;
inline constexpr std::uint32_t kMediumStressTestCost = 64;
inline constexpr std::uint32_t kLongStressTestCost = 256;
inline constexpr std::uint32_t kSlowTestCost = 100;
inline constexpr std::uint32_t kMinimumBatchCost = 4;
// CreateProcessW accepts at most 32,767 UTF-16 code units including the
// executable and every argument. Keep each gtest filter comfortably below
// that limit so report paths and user-supplied arguments still fit.
inline constexpr std::size_t kMaximumWorkerFilterLength = 16 * 1024;

enum class TestClass {
  Conformance,
  Differential,
  Robustness,
  Performance,
};

enum class ExecutionPath {
  Auto,
  NativeCompiled,
  Fallback,
  Both,
};

struct TestRequirements {
  std::string minimum_feature_level;
  std::string minimum_shader_model;
  std::string queue_type;
  std::string required_capabilities;
};

struct CaseTraits {
  TestClass test_class = TestClass::Conformance;
  ExecutionPath execution_path = ExecutionPath::Auto;
  TestRequirements requirements;
  std::uint32_t estimated_cost = kNormalTestCost;
  std::string setup;
  std::string operation_sequence;
  std::string oracle;
  std::string diagnostic_state;
};

struct GpuCaseResult {
  std::uint32_t status = 0;
  std::uint32_t first_mismatch_index = 0;
  std::uint64_t expected = 0;
  std::uint64_t actual = 0;
};

struct LogicalCaseFamily {
  std::string owner_test;
  std::string case_id_prefix;
  std::size_t case_count = 0;
  std::size_t index_width = 0;
  CaseTraits traits;
};

struct ScheduledTest {
  std::string name;
  std::uint32_t cost = kNormalTestCost;
  bool serial = false;
  std::string serial_group;
  std::string serial_domain;
};

struct TestShard {
  std::vector<std::string> tests;
  std::uint64_t estimated_cost = 0;
  std::string serial_domain;
};

class TestCostRegistration {
public:
  TestCostRegistration(std::string_view pattern, std::uint32_t cost);
};

class SerialTestRegistration {
public:
  explicit SerialTestRegistration(std::string_view pattern);
};

class SerialTestGroupRegistration {
public:
  SerialTestGroupRegistration(std::string_view pattern,
                              std::string_view group);
};

class SerialTestDomainRegistration {
public:
  SerialTestDomainRegistration(std::string_view pattern,
                               std::string_view domain);
};

class LogicalCaseFamilyRegistration {
public:
  LogicalCaseFamilyRegistration(std::string_view owner_test,
                                std::string_view case_id_prefix,
                                std::size_t case_count,
                                std::size_t index_width, CaseTraits traits);
  LogicalCaseFamilyRegistration(const LogicalCaseFamilyRegistration &) =
      delete;
  LogicalCaseFamilyRegistration &
  operator=(const LogicalCaseFamilyRegistration &) = delete;

  const LogicalCaseFamily &family() const { return family_; }

private:
  LogicalCaseFamily family_;
};

bool GlobMatches(std::string_view pattern, std::string_view value);
bool FilterMatches(std::string_view filter, std::string_view test_name);
std::string CaseNamespaceFromExecutable(std::string_view executable_path);
std::string CaseIdForTest(std::string_view case_namespace,
                          std::string_view test_name);
const char *TestClassName(TestClass test_class);
const char *ExecutionPathName(ExecutionPath execution_path);
std::string LogicalCaseId(const LogicalCaseFamily &family,
                          std::size_t index);
bool LogicalCaseMatchesFilter(const LogicalCaseFamily &family,
                              std::size_t index,
                              std::string_view case_namespace,
                              std::string_view filter);
bool LogicalCaseFamilyMatchesFilter(const LogicalCaseFamily &family,
                                    std::string_view case_namespace,
                                    std::string_view filter);
bool LogicalCaseSelected(const LogicalCaseFamily &family, std::size_t index);
std::string LogicalCaseMetadataJson(const LogicalCaseFamily &family,
                                    std::size_t index);
std::size_t SelectWorkerCount(const std::vector<ScheduledTest> &tests,
                              std::size_t maximum_worker_count);
std::vector<ScheduledTest>
ExtractSerialTests(std::vector<ScheduledTest> &tests);
std::vector<TestShard>
BuildSerialTestShards(std::vector<ScheduledTest> tests);
std::vector<std::vector<std::size_t>>
BuildSerialShardWaves(const std::vector<TestShard> &shards,
                      std::size_t maximum_concurrency);
std::vector<TestShard> BuildTestShards(std::vector<ScheduledTest> tests,
                                       std::size_t worker_count,
                                       std::size_t maximum_filter_length =
                                           kMaximumWorkerFilterLength);
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

#define DXMT_GROUP_SERIAL_TESTS(pattern, group)                                \
  static const ::dxmt::test::SerialTestGroupRegistration DXMT_TEST_CONCAT_(    \
      dxmt_serial_test_group_, __LINE__)(pattern, group)

#define DXMT_SERIAL_TEST_DOMAIN(pattern, domain)                               \
  static const ::dxmt::test::SerialTestDomainRegistration DXMT_TEST_CONCAT_(   \
      dxmt_serial_test_domain_, __LINE__)(pattern, domain)
