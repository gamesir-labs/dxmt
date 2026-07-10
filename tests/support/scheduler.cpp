#include <dxmt_test.hpp>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <optional>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

extern char **environ;

namespace dxmt::test {
namespace {

using Clock = std::chrono::steady_clock;

struct CostHint {
  std::string pattern;
  std::uint32_t cost;
};

std::vector<CostHint> &CostHints() {
  static std::vector<CostHint> hints;
  return hints;
}

class FailureOnlyPrinter final : public ::testing::EmptyTestEventListener {
public:
  void OnTestStart(const ::testing::TestInfo &test) override {
    current_test_ = std::string(test.test_suite_name()) + "." + test.name();
  }

  void OnTestPartResult(const ::testing::TestPartResult &result) override {
    if (!result.failed())
      return;

    if (!current_test_.empty()) {
      std::fprintf(stderr, "[  FAILED  ] %s\n", current_test_.c_str());
    } else {
      std::fprintf(stderr, "[  FAILED  ] global test environment\n");
    }

    if (result.file_name() != nullptr) {
      std::fprintf(stderr, "%s:%d: Failure\n", result.file_name(),
                   result.line_number());
    }
    std::fprintf(stderr, "%s\n", result.message());
  }

  void OnTestEnd(const ::testing::TestInfo &) override {
    current_test_.clear();
  }

private:
  std::string current_test_;
};

void ConfigureWorkerOutput() {
  if (std::getenv("DXMT_TEST_VERBOSE_WORKERS") != nullptr)
    return;

  auto &listeners = ::testing::UnitTest::GetInstance()->listeners();
  delete listeners.Release(listeners.default_result_printer());
  listeners.Append(new FailureOnlyPrinter());
}

bool IsDisabledName(std::string_view name) {
  return name.starts_with("DISABLED_") ||
         name.find("/DISABLED_") != std::string_view::npos;
}

std::uint32_t TestCost(std::string_view name) {
  std::uint32_t cost = kNormalTestCost;
  for (const auto &hint : CostHints()) {
    if (GlobMatches(hint.pattern, name))
      cost = std::max(cost, hint.cost);
  }
  return cost;
}

std::vector<ScheduledTest> CollectRunnableTests() {
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
      if ((!disabled || run_disabled) && FilterMatches(filter, full_name))
        tests.push_back({full_name, TestCost(full_name)});
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
};

enum class WorkerMode {
  Fork,
  Spawn,
};

std::optional<SchedulerOptions>
ParseSchedulerOptions(const std::vector<std::string> &arguments) {
  SchedulerOptions options;
  constexpr std::string_view prefix = "--dxmt-test-jobs=";

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

  for (const auto &argument : arguments) {
    if (argument == "--help" || argument == "-h" ||
        argument == "--gtest_help") {
      options.show_help = true;
      continue;
    }
    if (!std::string_view(argument).starts_with(prefix))
      continue;

    const auto parsed =
        ParsePositiveInteger(std::string_view(argument).substr(prefix.size()));
    if (!parsed) {
      std::fprintf(stderr,
                   "--dxmt-test-jobs must be a positive integer, got '%s'\n",
                   argument.c_str() + prefix.size());
      return std::nullopt;
    }
    options.jobs = *parsed;
    options.jobs_were_set = true;
  }

  if (!options.jobs_were_set) {
    options.jobs = std::max(1u, std::thread::hardware_concurrency());
  }
  return options;
}

std::string ExecutablePath() {
  std::uint32_t size = 1024;
  std::vector<char> buffer(size);
  while (_NSGetExecutablePath(buffer.data(), &size) != 0)
    buffer.resize(size);
  return std::string(buffer.data());
}

bool IsSingleThreaded() {
  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t thread_count = 0;
  const auto result = task_threads(mach_task_self(), &threads, &thread_count);
  if (result != KERN_SUCCESS)
    return false;

  for (mach_msg_type_number_t index = 0; index < thread_count; ++index)
    mach_port_deallocate(mach_task_self(), threads[index]);
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                thread_count * sizeof(thread_t));
  return thread_count == 1;
}

WorkerMode SelectWorkerMode() {
  if (const char *mode = std::getenv("DXMT_TEST_WORKER_MODE")) {
    if (std::strcmp(mode, "fork") == 0)
      return WorkerMode::Fork;
    if (std::strcmp(mode, "spawn") == 0)
      return WorkerMode::Spawn;
    std::fprintf(stderr,
                 "ignoring invalid DXMT_TEST_WORKER_MODE '%s'; expected "
                 "'fork' or 'spawn'\n",
                 mode);
  }
  return IsSingleThreaded() ? WorkerMode::Fork : WorkerMode::Spawn;
}

std::vector<std::string>
BuildWorkerEnvironment(const std::vector<std::string> &extra_entries) {
  std::vector<std::string> environment;
  for (char **entry = environ; *entry != nullptr; ++entry) {
    const std::string_view value(*entry);
    if (!value.starts_with("DXMT_GTEST_WORKER="))
      environment.emplace_back(value);
  }
  environment.insert(environment.end(), extra_entries.begin(),
                     extra_entries.end());
  return environment;
}

std::vector<char *> MutablePointers(std::vector<std::string> &values) {
  std::vector<char *> pointers;
  pointers.reserve(values.size() + 1);
  for (auto &value : values)
    pointers.push_back(value.data());
  pointers.push_back(nullptr);
  return pointers;
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
                     const std::string &executable, const TestShard &shard) {
  std::vector<std::string> arguments;
  arguments.reserve(original_arguments.size() + 1);
  arguments.push_back(executable);
  for (std::size_t index = 1; index < original_arguments.size(); ++index) {
    if (!std::string_view(original_arguments[index])
             .starts_with("--dxmt-test-jobs=")) {
      arguments.push_back(original_arguments[index]);
    }
  }
  arguments.push_back("--gtest_filter=" + BuildFilter(shard));
  return arguments;
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

} // namespace

TestCostRegistration::TestCostRegistration(std::string_view pattern,
                                           std::uint32_t cost) {
  CostHints().push_back({std::string(pattern), cost});
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

  const bool is_worker = std::getenv("DXMT_GTEST_WORKER") != nullptr;
  if (is_worker || HasExternalSharding() || options->show_help ||
      GTEST_FLAG_GET(list_tests) ||
      HasUnrecognizedGoogleTestArgument(argc, argv)) {
    if (is_worker)
      ConfigureWorkerOutput();
    return RUN_ALL_TESTS();
  }

  auto worker_count = options->jobs;
  if (!GTEST_FLAG_GET(output).empty() ||
      !GTEST_FLAG_GET(stream_result_to).empty()) {
    worker_count = 1;
  }

  const auto plan_start = Clock::now();
  auto tests = CollectRunnableTests();
  worker_count = options->jobs_were_set
                     ? std::min(worker_count, tests.size())
                     : SelectWorkerCount(tests, worker_count);
  if (worker_count <= 1)
    return RUN_ALL_TESTS();

  auto shards = BuildTestShards(std::move(tests), worker_count);
  const auto worker_mode = SelectWorkerMode();
  const auto executable =
      worker_mode == WorkerMode::Spawn ? ExecutablePath() : std::string();
  auto environment = worker_mode == WorkerMode::Spawn
                         ? BuildWorkerEnvironment({"DXMT_GTEST_WORKER=1"})
                         : std::vector<std::string>();
  auto environment_pointers = MutablePointers(environment);
  const auto plan_end = Clock::now();
  const auto plan_us = std::chrono::duration_cast<std::chrono::microseconds>(
                           plan_end - plan_start)
                           .count();

  std::printf(
      "[ DXMT     ] scheduling %zu tests on %zu workers via %s "
      "(plan %lld us)\n",
      [&shards] {
        std::size_t count = 0;
        for (const auto &shard : shards)
          count += shard.tests.size();
        return count;
      }(),
      shards.size(), worker_mode == WorkerMode::Fork ? "fork" : "spawn",
      static_cast<long long>(plan_us));
  std::fflush(stdout);

  std::vector<pid_t> children;
  children.reserve(shards.size());
  std::size_t spawn_failures = 0;

  std::fflush(nullptr);
  const auto run_start = Clock::now();
  for (const auto &shard : shards) {
    const auto filter = BuildFilter(shard);
    pid_t child = -1;
    int error = 0;
    if (worker_mode == WorkerMode::Fork) {
      child = fork();
      if (child == 0) {
        setenv("DXMT_GTEST_WORKER", "1", 1);
        GTEST_FLAG_SET(filter, filter);
        ConfigureWorkerOutput();
        const int result = RUN_ALL_TESTS();
        std::exit(result);
      }
      if (child < 0)
        error = errno;
    } else {
      auto arguments =
          BuildWorkerArguments(original_arguments, executable, shard);
      auto argument_pointers = MutablePointers(arguments);
      error =
          posix_spawn(&child, executable.c_str(), nullptr, nullptr,
                      argument_pointers.data(), environment_pointers.data());
    }
    if (error != 0) {
      std::fprintf(stderr, "failed to start unit-test worker: %s\n",
                   std::strerror(error));
      ++spawn_failures;
    } else {
      children.push_back(child);
    }
  }
  const auto spawn_end = Clock::now();

  std::size_t failed_workers = spawn_failures;
  for (const auto child : children) {
    int status = 0;
    pid_t result = -1;
    do {
      result = waitpid(child, &status, 0);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
      std::fprintf(stderr, "failed to wait for unit-test worker %d: %s\n",
                   child, std::strerror(errno));
      ++failed_workers;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      if (WIFSIGNALED(status)) {
        std::fprintf(stderr, "unit-test worker %d terminated by signal %d\n",
                     child, WTERMSIG(status));
      }
      ++failed_workers;
    }
  }
  const auto run_end = Clock::now();

  const auto launch_us = std::chrono::duration_cast<std::chrono::microseconds>(
                             spawn_end - run_start)
                             .count();
  const auto run_ms =
      std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
          run_end - run_start)
          .count();
  std::printf("[ DXMT     ] workers %s (launch %lld us, wall %.3f ms)\n",
              failed_workers == 0 ? "passed" : "failed",
              static_cast<long long>(launch_us), run_ms);
  return failed_workers == 0 ? 0 : 1;
}

} // namespace dxmt::test
