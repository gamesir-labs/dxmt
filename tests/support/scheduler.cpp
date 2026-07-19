#include <dxmt_test.hpp>

#include "wine_process.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dxmt::test {
namespace {

using Clock = std::chrono::steady_clock;

struct CostHint {
  std::string pattern;
  std::uint32_t cost;
};

struct SerialHint {
  std::string pattern;
};

struct SerialGroupHint {
  std::string pattern;
  std::string group;
};

struct SerialDomainHint {
  std::string pattern;
  std::string domain;
};

std::vector<CostHint> &CostHints() {
  static std::vector<CostHint> hints;
  return hints;
}

std::vector<SerialHint> &SerialHints() {
  static std::vector<SerialHint> hints;
  return hints;
}

std::vector<SerialGroupHint> &SerialGroupHints() {
  static std::vector<SerialGroupHint> hints;
  return hints;
}

std::vector<SerialDomainHint> &SerialDomainHints() {
  static std::vector<SerialDomainHint> hints;
  return hints;
}

std::unordered_map<std::string, std::uint64_t> &HistoricalTestMicros() {
  static std::unordered_map<std::string, std::uint64_t> timings;
  return timings;
}

std::vector<const LogicalCaseFamily *> &LogicalCaseFamilies() {
  static std::vector<const LogicalCaseFamily *> families;
  return families;
}

std::string &ActiveCaseNamespace() {
  static std::string value;
  return value;
}

std::string &ActiveCaseIdFilter() {
  static std::string value;
  return value;
}

bool TestOwnsMatchingLogicalCase(std::string_view test_name,
                                 std::string_view case_namespace,
                                 std::string_view filter) {
  return std::ranges::any_of(LogicalCaseFamilies(), [&](const auto *family) {
    return family->owner_test == test_name &&
           LogicalCaseFamilyMatchesFilter(*family, case_namespace, filter);
  });
}

class FailureCollector final : public ::testing::EmptyTestEventListener {
public:
  explicit FailureCollector(std::string_view case_namespace)
      : case_namespace_(case_namespace) {}

  void OnTestStart(const ::testing::TestInfo &test) override {
    current_test_ = std::string(test.test_suite_name()) + "." + test.name();
    current_case_id_ = CaseIdForTest(case_namespace_, current_test_);
    current_test_reported_ = false;
  }

  void OnTestPartResult(const ::testing::TestPartResult &result) override {
    if (!result.failed())
      return;

    if (!current_test_reported_) {
      report_ += "[  FAILED  ] ";
      report_ +=
          current_test_.empty() ? "global test environment" : current_test_;
      report_ += '\n';
      if (!current_case_id_.empty()) {
        report_ += "CaseId: ";
        report_ += current_case_id_;
        report_ += '\n';
      }
      current_test_reported_ = true;
    }

    if (result.file_name() != nullptr) {
      report_ += result.file_name();
      report_ += ':';
      report_ += std::to_string(result.line_number());
      report_ += ": Failure\n";
    }
    report_ += result.message();
    report_ += '\n';
  }

  void OnTestEnd(const ::testing::TestInfo &) override {
    current_test_.clear();
    current_case_id_.clear();
    current_test_reported_ = false;
  }

  bool has_failures() const { return !report_.empty(); }
  const std::string &report() const { return report_; }

  bool WriteReport(std::string_view path) const {
    if (!has_failures())
      return true;

    auto *file = std::fopen(std::string(path).c_str(), "w");
    if (file == nullptr)
      return false;
    const auto written = std::fwrite(report_.data(), 1, report_.size(), file);
    return std::fclose(file) == 0 && written == report_.size();
  }

private:
  std::string case_namespace_;
  std::string current_test_;
  std::string current_case_id_;
  std::string report_;
  bool current_test_reported_ = false;
};

class TimingCollector final : public ::testing::EmptyTestEventListener {
public:
  void OnTestStart(const ::testing::TestInfo &) override {
    started_ = Clock::now();
  }

  void OnTestEnd(const ::testing::TestInfo &test) override {
    const auto micros = std::max<std::int64_t>(
        1, std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                                started_)
               .count());
    timings_.push_back({std::string(test.test_suite_name()) + "." + test.name(),
                        static_cast<std::uint64_t>(micros)});
  }

  bool WriteReport(std::string_view path) const {
    auto *file = std::fopen(std::string(path).c_str(), "w");
    if (file == nullptr)
      return false;
    bool ok = true;
    for (const auto &[name, micros] : timings_) {
      if (std::fprintf(file, "%s\t%llu\n", name.c_str(),
                       static_cast<unsigned long long>(micros)) < 0) {
        ok = false;
        break;
      }
    }
    return std::fclose(file) == 0 && ok;
  }

private:
  Clock::time_point started_;
  std::vector<std::pair<std::string, std::uint64_t>> timings_;
};

FailureCollector *ConfigureWorkerOutput(std::string_view case_namespace) {
  if (std::getenv("DXMT_TEST_VERBOSE_WORKERS") != nullptr)
    return nullptr;

  auto &listeners = ::testing::UnitTest::GetInstance()->listeners();
  delete listeners.Release(listeners.default_result_printer());
  auto *collector = new FailureCollector(case_namespace);
  listeners.Append(collector);
  return collector;
}

void DisableFailureShortCircuiting() {
  GTEST_FLAG_SET(fail_fast, false);
  GTEST_FLAG_SET(break_on_failure, false);
  GTEST_FLAG_SET(throw_on_failure, false);
  GTEST_FLAG_SET(catch_exceptions, true);
}

int RunTestsAndReport(std::string_view case_namespace,
                      std::string_view report_path = {},
                      std::string_view timing_report_path = {}) {
  auto *collector = ConfigureWorkerOutput(case_namespace);
  auto *timing_collector = new TimingCollector();
  ::testing::UnitTest::GetInstance()->listeners().Append(timing_collector);
  const int result = RUN_ALL_TESTS();
  bool report_error = false;

  if (!timing_report_path.empty() &&
      !timing_collector->WriteReport(timing_report_path)) {
    std::fprintf(stderr, "failed to write unit-test timing report '%s': %s\n",
                 std::string(timing_report_path).c_str(), std::strerror(errno));
    report_error = true;
  }

  if (collector == nullptr)
    return result == 0 && report_error ? 2 : result;

  if (!report_path.empty()) {
    if (!collector->WriteReport(report_path)) {
      std::fprintf(stderr,
                   "failed to write unit-test failure report '%s': %s\n",
                   std::string(report_path).c_str(), std::strerror(errno));
      report_error = true;
    }
  } else if (collector->has_failures()) {
    std::fprintf(stderr, "[ DXMT     ] failure summary\n%s",
                 collector->report().c_str());
  } else {
    std::printf("[ DXMT     ] all selected tests passed\n");
  }
  return result == 0 && report_error ? 2 : result;
}

bool IsDisabledName(std::string_view name) {
  return name.starts_with("DISABLED_") ||
         name.find("/DISABLED_") != std::string_view::npos;
}

std::string TimingDatabasePath(std::string_view case_namespace) {
  std::vector<char> buffer(32768);
  const DWORD size =
      GetTempPathA(static_cast<DWORD>(buffer.size()), buffer.data());
  std::string path;
  if (size == 0 || size >= buffer.size()) {
    path = ".\\";
  } else {
    path.assign(buffer.data(), size);
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
      path.push_back('\\');
  }
  path += "dxmt-gtest-timings-";
  for (const char character : case_namespace)
    path.push_back((character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9')
                       ? character
                       : '_');
  path += ".tsv";
  return path;
}

void LoadTimingDatabase(std::string_view path) {
  auto &timings = HistoricalTestMicros();
  timings.clear();
  auto *file = std::fopen(std::string(path).c_str(), "r");
  if (file == nullptr)
    return;

  char line[8192];
  while (std::fgets(line, sizeof(line), file) != nullptr) {
    std::string_view record(line);
    if (!record.empty() && record.back() == '\n')
      record.remove_suffix(1);
    const auto separator = record.rfind('\t');
    if (separator == std::string_view::npos)
      continue;
    std::uint64_t micros = 0;
    const auto value = record.substr(separator + 1);
    const auto parse =
        std::from_chars(value.data(), value.data() + value.size(), micros);
    if (parse.ec == std::errc() && parse.ptr == value.data() + value.size() &&
        micros != 0)
      timings[std::string(record.substr(0, separator))] = micros;
  }
  std::fclose(file);
}

std::uint32_t TestCost(std::string_view name) {
  std::uint32_t cost = kNormalTestCost;
  for (const auto &hint : CostHints()) {
    if (GlobMatches(hint.pattern, name))
      cost = std::max(cost, hint.cost);
  }
  if (const auto timing = HistoricalTestMicros().find(std::string(name));
      timing != HistoricalTestMicros().end()) {
    constexpr std::uint64_t kCostQuantumMicros = 1000;
    const auto measured = std::min<std::uint64_t>(
        UINT32_MAX,
        (timing->second + kCostQuantumMicros - 1) / kCostQuantumMicros);
    cost = std::max(cost, static_cast<std::uint32_t>(measured));
  }
  return cost;
}

bool TestMustRunSerially(std::string_view name) {
  return std::ranges::any_of(SerialHints(), [&](const auto &hint) {
    return GlobMatches(hint.pattern, name);
  });
}

std::string TestSerialGroup(std::string_view name) {
  for (const auto &hint : SerialGroupHints())
    if (GlobMatches(hint.pattern, name))
      return hint.group;
  return {};
}

std::string TestSerialDomain(std::string_view name) {
  for (const auto &hint : SerialDomainHints())
    if (GlobMatches(hint.pattern, name))
      return hint.domain;
  return {};
}

std::vector<ScheduledTest>
CollectRunnableTests(std::string_view case_namespace,
                     std::string_view case_id_filter = {}) {
  std::vector<ScheduledTest> tests;
  const auto filter = std::string_view(GTEST_FLAG_GET(filter));
  const bool run_disabled = GTEST_FLAG_GET(also_run_disabled_tests);
  const auto *unit_test = ::testing::UnitTest::GetInstance();

  tests.reserve(static_cast<std::size_t>(unit_test->total_test_count()));
  for (int suite_index = 0; suite_index < unit_test->total_test_suite_count();
       ++suite_index) {
    const auto *suite = unit_test->GetTestSuite(suite_index);
    for (int test_index = 0; test_index < suite->total_test_count();
         ++test_index) {
      const auto *test = suite->GetTestInfo(test_index);
      std::string full_name = std::string(suite->name()) + "." + test->name();
      const bool disabled =
          IsDisabledName(suite->name()) || IsDisabledName(test->name());
      const auto case_id = CaseIdForTest(case_namespace, full_name);
      if ((!disabled || run_disabled) && FilterMatches(filter, full_name) &&
          (case_id_filter.empty() ||
           FilterMatches(case_id_filter, case_id) ||
           TestOwnsMatchingLogicalCase(full_name, case_namespace,
                                       case_id_filter)))
        tests.push_back({full_name, TestCost(full_name),
                         TestMustRunSerially(full_name),
                         TestSerialGroup(full_name),
                         TestSerialDomain(full_name)});
    }
  }
  return tests;
}

std::optional<std::size_t> ParsePositiveInteger(std::string_view value) {
  std::size_t result = 0;
  const auto *begin = value.data();
  const auto *end = value.data() + value.size();
  const auto parse = std::from_chars(begin, end, result);
  if (parse.ec != std::errc() || parse.ptr != end || result == 0)
    return std::nullopt;
  return result;
}

struct SchedulerOptions {
  std::size_t jobs = 0;
  bool jobs_were_set = false;
  bool show_help = false;
  bool is_worker = false;
  bool list_case_ids = false;
  bool list_case_metadata = false;
  std::string case_id_filter;
  std::string report_path;
  std::string timing_report_path;
};

std::optional<SchedulerOptions>
ParseSchedulerOptions(const std::vector<std::string> &arguments) {
  SchedulerOptions options;
  constexpr std::string_view jobs_prefix = "--dxmt-test-jobs=";
  constexpr std::string_view report_prefix = "--dxmt-test-report=";
  constexpr std::string_view timing_report_prefix =
      "--dxmt-test-timing-report=";
  constexpr std::string_view case_id_prefix = "--dxmt-case-id=";
  constexpr std::string_view legacy_case_id_prefix = "--dxmt_case_id=";

  if (const char *value = std::getenv("DXMT_TEST_JOBS")) {
    const auto parsed = ParsePositiveInteger(value);
    if (!parsed) {
      std::fprintf(stderr,
                   "DXMT_TEST_JOBS must be a positive integer, got '%s'\n",
                   value);
      return std::nullopt;
    }
    options.jobs = *parsed;
    options.jobs_were_set = true;
  }

  if (const char *value = std::getenv("DXMT_TEST_CASE_ID"))
    options.case_id_filter = value;

  for (const auto &argument : arguments) {
    if (argument == "--help" || argument == "-h" ||
        argument == "--gtest_help") {
      options.show_help = true;
      continue;
    }
    if (argument == "--dxmt-test-worker") {
      options.is_worker = true;
      continue;
    }
    if (argument == "--dxmt-list-case-ids") {
      options.list_case_ids = true;
      continue;
    }
    if (argument == "--dxmt-list-case-metadata") {
      options.list_case_metadata = true;
      continue;
    }
    const std::string_view argument_view(argument);
    if (argument_view.starts_with(case_id_prefix)) {
      const auto filter = argument_view.substr(case_id_prefix.size());
      if (filter.empty()) {
        std::fprintf(stderr, "--dxmt-case-id requires a non-empty filter\n");
        return std::nullopt;
      }
      options.case_id_filter = std::string(filter);
      continue;
    }
    if (argument_view.starts_with(legacy_case_id_prefix)) {
      const auto filter = argument_view.substr(legacy_case_id_prefix.size());
      if (filter.empty()) {
        std::fprintf(stderr, "--dxmt_case_id requires a non-empty filter\n");
        return std::nullopt;
      }
      options.case_id_filter = std::string(filter);
      continue;
    }
    if (std::string_view(argument).starts_with(report_prefix)) {
      options.report_path =
          std::string(std::string_view(argument).substr(report_prefix.size()));
      continue;
    }
    if (argument_view.starts_with(timing_report_prefix)) {
      options.timing_report_path =
          std::string(argument_view.substr(timing_report_prefix.size()));
      continue;
    }
    if (!argument_view.starts_with(jobs_prefix))
      continue;

    const auto parsed = ParsePositiveInteger(
        std::string_view(argument).substr(jobs_prefix.size()));
    if (!parsed) {
      std::fprintf(stderr,
                   "--dxmt-test-jobs must be a positive integer, got '%s'\n",
                   argument.c_str() + jobs_prefix.size());
      return std::nullopt;
    }
    options.jobs = *parsed;
    options.jobs_were_set = true;
  }

  if ((options.list_case_ids || options.list_case_metadata) &&
      options.case_id_filter.empty())
    options.case_id_filter = "*";

  if (!options.jobs_were_set)
    options.jobs = std::max(1u, std::thread::hardware_concurrency());
  return options;
}

std::string BuildFilter(const TestShard &shard) {
  std::size_t size = 0;
  for (const auto &test : shard.tests)
    size += test.size() + 1;

  std::string filter;
  filter.reserve(size);
  for (const auto &test : shard.tests) {
    if (!filter.empty())
      filter.push_back(':');
    filter += test;
  }
  return filter;
}

std::vector<std::string>
BuildWorkerArguments(const std::vector<std::string> &original_arguments,
                     const TestShard &shard, std::string_view report_path,
                     std::string_view timing_report_path) {
  std::vector<std::string> arguments;
  arguments.reserve(original_arguments.size() + 2);
  for (std::size_t index = 1; index < original_arguments.size(); ++index) {
    const std::string_view argument(original_arguments[index]);
    if (!argument.starts_with("--dxmt-test-jobs=") &&
        !argument.starts_with("--dxmt-test-report=") &&
        !argument.starts_with("--dxmt-test-timing-report=") &&
        !argument.starts_with("--gtest_filter=") &&
        argument != "--dxmt-list-case-ids" &&
        argument != "--dxmt-list-case-metadata" &&
        argument != "--dxmt-test-worker") {
      arguments.push_back(original_arguments[index]);
    }
  }
  arguments.push_back("--dxmt-test-worker");
  arguments.push_back("--gtest_filter=" + BuildFilter(shard));
  arguments.push_back("--dxmt-test-report=" + std::string(report_path));
  arguments.push_back("--dxmt-test-timing-report=" +
                      std::string(timing_report_path));
  return arguments;
}

std::string BuildReportPrefix() {
  std::vector<char> buffer(32768);
  const DWORD size =
      GetTempPathA(static_cast<DWORD>(buffer.size()), buffer.data());
  std::string path;
  if (size == 0 || size >= buffer.size()) {
    path = ".\\";
  } else {
    path.assign(buffer.data(), size);
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
      path.push_back('\\');
  }
  path += "dxmt-gtest-" + std::to_string(GetCurrentProcessId()) + "-";
  path += std::to_string(Clock::now().time_since_epoch().count());
  return path;
}

std::string ReadAndRemoveReport(std::string_view path) {
  std::string report;
  auto *file = std::fopen(std::string(path).c_str(), "r");
  if (file != nullptr) {
    char buffer[4096];
    while (const auto size = std::fread(buffer, 1, sizeof(buffer), file))
      report.append(buffer, size);
    std::fclose(file);
  }
  DeleteFileA(std::string(path).c_str());
  return report;
}

void MergeTimingReport(std::string_view path) {
  auto *file = std::fopen(std::string(path).c_str(), "r");
  if (file == nullptr)
    return;

  auto &timings = HistoricalTestMicros();
  char line[8192];
  while (std::fgets(line, sizeof(line), file) != nullptr) {
    std::string_view record(line);
    if (!record.empty() && record.back() == '\n')
      record.remove_suffix(1);
    const auto separator = record.rfind('\t');
    if (separator == std::string_view::npos)
      continue;
    const auto value = record.substr(separator + 1);
    std::uint64_t observed = 0;
    const auto parse =
        std::from_chars(value.data(), value.data() + value.size(), observed);
    if (parse.ec != std::errc() || parse.ptr != value.data() + value.size() ||
        observed == 0)
      continue;

    auto [timing, inserted] = timings.try_emplace(
        std::string(record.substr(0, separator)), observed);
    if (!inserted)
      timing->second = (timing->second * 3 + observed) / 4;
  }
  std::fclose(file);
  DeleteFileA(std::string(path).c_str());
}

bool WriteTimingDatabase(std::string_view path) {
  std::vector<std::pair<std::string, std::uint64_t>> sorted(
      HistoricalTestMicros().begin(), HistoricalTestMicros().end());
  std::ranges::sort(sorted, {}, &decltype(sorted)::value_type::first);

  const std::string temporary = std::string(path) + ".tmp-" +
                                std::to_string(GetCurrentProcessId());
  auto *file = std::fopen(temporary.c_str(), "w");
  if (file == nullptr)
    return false;
  bool ok = true;
  for (const auto &[name, micros] : sorted) {
    if (std::fprintf(file, "%s\t%llu\n", name.c_str(),
                     static_cast<unsigned long long>(micros)) < 0) {
      ok = false;
      break;
    }
  }
  ok = std::fclose(file) == 0 && ok;
  if (!ok) {
    DeleteFileA(temporary.c_str());
    return false;
  }
  if (!MoveFileExA(temporary.c_str(), std::string(path).c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileA(temporary.c_str());
    return false;
  }
  return true;
}

bool HasExternalSharding() {
  return std::getenv("GTEST_TOTAL_SHARDS") != nullptr ||
         std::getenv("GTEST_SHARD_INDEX") != nullptr;
}

bool HasUnrecognizedGoogleTestArgument(int argc, char **argv) {
  for (int index = 1; index < argc; ++index) {
    if (std::string_view(argv[index]).starts_with("--gtest_"))
      return true;
  }
  return false;
}

void PrintSelectedCaseIdentities(const std::vector<ScheduledTest> &tests,
                                 std::string_view case_namespace,
                                 std::string_view case_id_filter,
                                 bool metadata) {
  for (const auto &test : tests) {
    const auto outer_case_id = CaseIdForTest(case_namespace, test.name);
    if (!metadata && FilterMatches(case_id_filter, outer_case_id))
      std::printf("%s\n", outer_case_id.c_str());

    for (const auto *family : LogicalCaseFamilies()) {
      if (family->owner_test != test.name)
        continue;
      for (std::size_t index = 0; index < family->case_count; ++index) {
        if (!LogicalCaseMatchesFilter(*family, index, case_namespace,
                                      case_id_filter))
          continue;
        const auto line = metadata
                              ? LogicalCaseMetadataJson(*family, index)
                              : LogicalCaseId(*family, index);
        std::printf("%s\n", line.c_str());
      }
    }
  }
}

} // namespace

TestCostRegistration::TestCostRegistration(std::string_view pattern,
                                           std::uint32_t cost) {
  CostHints().push_back({std::string(pattern), cost});
}

SerialTestRegistration::SerialTestRegistration(std::string_view pattern) {
  SerialHints().push_back({std::string(pattern)});
}

SerialTestGroupRegistration::SerialTestGroupRegistration(
    std::string_view pattern, std::string_view group) {
  SerialGroupHints().push_back({std::string(pattern), std::string(group)});
}

SerialTestDomainRegistration::SerialTestDomainRegistration(
    std::string_view pattern, std::string_view domain) {
  SerialDomainHints().push_back({std::string(pattern), std::string(domain)});
}

LogicalCaseFamilyRegistration::LogicalCaseFamilyRegistration(
    std::string_view owner_test, std::string_view case_id_prefix,
    std::size_t case_count, std::size_t index_width, CaseTraits traits)
    : family_{std::string(owner_test), std::string(case_id_prefix), case_count,
              index_width, std::move(traits)} {
  LogicalCaseFamilies().push_back(&family_);
}

bool LogicalCaseSelected(const LogicalCaseFamily &family, std::size_t index) {
  return LogicalCaseMatchesFilter(family, index, ActiveCaseNamespace(),
                                  ActiveCaseIdFilter());
}

int RunScheduledTests(int argc, char **argv) {
  std::vector<std::string> original_arguments;
  original_arguments.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index)
    original_arguments.emplace_back(argv[index]);

  const auto options = ParseSchedulerOptions(original_arguments);
  if (!options)
    return 2;

  GTEST_FLAG_SET(brief, true);
  ::testing::InitGoogleTest(&argc, argv);
  DisableFailureShortCircuiting();

  const auto case_namespace = CaseNamespaceFromExecutable(
      original_arguments.empty() ? std::string_view() : original_arguments[0]);
  ActiveCaseNamespace() = case_namespace;
  ActiveCaseIdFilter() = options->case_id_filter;
  const auto timing_database_path = TimingDatabasePath(case_namespace);
  LoadTimingDatabase(timing_database_path);

  auto tests =
      CollectRunnableTests(case_namespace, options->case_id_filter);
  if (options->list_case_ids || options->list_case_metadata) {
    PrintSelectedCaseIdentities(tests, case_namespace, options->case_id_filter,
                                options->list_case_metadata);
    return 0;
  }
  if (!options->case_id_filter.empty()) {
    if (tests.empty()) {
      std::fprintf(stderr, "no test matches CaseId filter '%s'\n",
                   options->case_id_filter.c_str());
      return 2;
    }
    TestShard selected;
    selected.tests.reserve(tests.size());
    for (const auto &test : tests)
      selected.tests.push_back(test.name);
    GTEST_FLAG_SET(filter, BuildFilter(selected));
  }

  if (options->show_help || GTEST_FLAG_GET(list_tests) ||
      HasUnrecognizedGoogleTestArgument(argc, argv)) {
    return RUN_ALL_TESTS();
  }
  if (options->is_worker)
    return RunTestsAndReport(case_namespace, options->report_path,
                             options->timing_report_path);
  if (HasExternalSharding())
    return RunTestsAndReport(case_namespace);

  auto worker_count = options->jobs;
  if (!GTEST_FLAG_GET(output).empty() ||
      !GTEST_FLAG_GET(stream_result_to).empty()) {
    worker_count = 1;
  }

  const auto plan_start = Clock::now();
  auto serial_tests = ExtractSerialTests(tests);
  auto serial_shards = BuildSerialTestShards(std::move(serial_tests));
  constexpr std::size_t kMaximumSerialConcurrency = 2;
  const auto serial_concurrency =
      std::min(kMaximumSerialConcurrency, options->jobs);
  const auto serial_waves =
      BuildSerialShardWaves(serial_shards, serial_concurrency);
  worker_count = options->jobs_were_set
                     ? std::min(worker_count, tests.size())
                     : SelectWorkerCount(tests, worker_count);
  if (serial_shards.empty() && worker_count <= 1)
    return RunTestsAndReport(case_namespace);

  auto shards = BuildTestShards(std::move(tests),
                                std::max<std::size_t>(1, worker_count),
                                kMaximumWorkerFilterLength);
  const auto parallel_shard_count = shards.size();
  const auto parallel_concurrency =
      std::min(parallel_shard_count, std::max<std::size_t>(1, worker_count));
  for (auto &shard : serial_shards)
    shards.push_back(std::move(shard));
  const auto executable = WineExecutablePath();
  if (executable.empty()) {
    std::fprintf(stderr, "failed to resolve Wine test executable path: %s\n",
                 WineErrorMessage(GetLastError()).c_str());
    return 2;
  }

  std::vector<std::string> report_paths;
  report_paths.reserve(shards.size());
  std::vector<std::string> timing_report_paths;
  timing_report_paths.reserve(shards.size());
  const auto report_prefix = BuildReportPrefix();
  for (std::size_t index = 0; index < shards.size(); ++index) {
    report_paths.push_back(report_prefix + "-" + std::to_string(index));
    timing_report_paths.push_back(report_prefix + "-timing-" +
                                  std::to_string(index));
  }
  const auto plan_end = Clock::now();
  const auto plan_us = std::chrono::duration_cast<std::chrono::microseconds>(
                           plan_end - plan_start)
                           .count();

  std::printf(
      "[ DXMT     ] scheduling %zu tests in %zu parallel shards "
      "(%zu concurrent) and %zu serial shards (%zu concurrent) in %zu "
      "serial waves "
      "(plan %lld us)\n",
      [&shards] {
        std::size_t count = 0;
        for (const auto &shard : shards)
          count += shard.tests.size();
        return count;
      }(),
      parallel_shard_count, parallel_concurrency, serial_shards.size(),
      serial_concurrency, serial_waves.size(),
      static_cast<long long>(plan_us));
  std::fflush(stdout);

  std::string scheduler_errors;
  std::size_t failed_workers = 0;
  std::vector<std::size_t> failed_shard_indexes;
  std::int64_t launch_us = 0;

  std::fflush(nullptr);
  const auto run_start = Clock::now();
  auto run_wave = [&](const std::vector<std::size_t> &indexes) {
    std::vector<std::pair<WineProcess, std::size_t>> children;
    children.reserve(indexes.size());
    const auto launch_start = Clock::now();
    for (const auto index : indexes) {
      const auto arguments = BuildWorkerArguments(
          original_arguments, shards[index], report_paths[index],
          timing_report_paths[index]);
      DWORD error = ERROR_SUCCESS;
      auto child = StartWineProcess(executable, arguments, &error);
      if (!child) {
        scheduler_errors += "failed to start Wine unit-test worker: ";
        scheduler_errors += WineErrorMessage(error);
        scheduler_errors += '\n';
        ++failed_workers;
      } else {
        children.push_back({*child, index});
      }
    }
    launch_us += std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - launch_start)
                     .count();

    for (const auto &[child, shard_index] : children) {
      const DWORD wait_result = WaitForSingleObject(child.handle, INFINITE);
      DWORD exit_code = 1;
      if (wait_result != WAIT_OBJECT_0) {
        scheduler_errors += "failed to wait for Wine unit-test worker ";
        scheduler_errors += std::to_string(child.id);
        scheduler_errors += ": ";
        scheduler_errors += WineErrorMessage(GetLastError());
        scheduler_errors += '\n';
        ++failed_workers;
        failed_shard_indexes.push_back(shard_index);
      } else if (!GetExitCodeProcess(child.handle, &exit_code)) {
        scheduler_errors += "failed to read Wine unit-test worker status ";
        scheduler_errors += std::to_string(child.id);
        scheduler_errors += ": ";
        scheduler_errors += WineErrorMessage(GetLastError());
        scheduler_errors += '\n';
        ++failed_workers;
        failed_shard_indexes.push_back(shard_index);
      } else if (exit_code != 0) {
        scheduler_errors += "Wine unit-test worker shard ";
        scheduler_errors += std::to_string(shard_index);
        scheduler_errors += " exited with status ";
        scheduler_errors += std::to_string(exit_code);
        scheduler_errors += '\n';
        ++failed_workers;
        failed_shard_indexes.push_back(shard_index);
      }
      CloseHandle(child.handle);
    }
  };

  for (std::size_t first = 0; first < parallel_shard_count;
       first += parallel_concurrency) {
    const auto last = std::min(parallel_shard_count,
                               first + parallel_concurrency);
    std::vector<std::size_t> parallel_indexes;
    parallel_indexes.reserve(last - first);
    for (std::size_t index = first; index < last; ++index)
      parallel_indexes.push_back(index);
    run_wave(parallel_indexes);
  }
  for (const auto &wave : serial_waves) {
    std::vector<std::size_t> indexes;
    indexes.reserve(wave.size());
    for (const auto index : wave)
      indexes.push_back(parallel_shard_count + index);
    run_wave(indexes);
  }

  std::string failure_reports;
  std::vector<bool> shard_reported_failure(shards.size(), false);
  for (std::size_t index = 0; index < report_paths.size(); ++index) {
    auto report = ReadAndRemoveReport(report_paths[index]);
    shard_reported_failure[index] = !report.empty();
    failure_reports += report;
  }
  for (const auto index : failed_shard_indexes) {
    if (!shard_reported_failure[index]) {
      scheduler_errors += "tests assigned to failed shard ";
      scheduler_errors += std::to_string(index);
      scheduler_errors += ":\n";
      for (const auto &test : shards[index].tests) {
        scheduler_errors += "  ";
        scheduler_errors += test;
        scheduler_errors += '\n';
      }
    }
  }
  for (const auto &path : timing_report_paths)
    MergeTimingReport(path);
  if (!WriteTimingDatabase(timing_database_path)) {
    scheduler_errors += "failed to update unit-test timing database: ";
    scheduler_errors += WineErrorMessage(GetLastError());
    scheduler_errors += '\n';
    ++failed_workers;
  }
  const auto run_end = Clock::now();

  const auto run_ms =
      std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
          run_end - run_start)
          .count();
  if (!scheduler_errors.empty() || !failure_reports.empty()) {
    std::fprintf(stderr, "[ DXMT     ] failure summary\n%s%s",
                 scheduler_errors.c_str(), failure_reports.c_str());
  }
  std::printf(
      "[ DXMT     ] Wine workers %s (launch %lld us, wall %.3f ms)\n",
      failed_workers == 0 ? "passed" : "failed",
      static_cast<long long>(launch_us), run_ms);
  return failed_workers == 0 ? 0 : 1;
}

} // namespace dxmt::test
