#include <dxmt_test_scheduler.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dxmt::test {
namespace {

std::vector<std::string_view> Split(std::string_view value, char separator) {
  std::vector<std::string_view> parts;
  while (true) {
    const auto position = value.find(separator);
    const auto part = value.substr(0, position);
    if (!part.empty())
      parts.push_back(part);
    if (position == std::string_view::npos)
      return parts;
    value.remove_prefix(position + 1);
  }
}

bool MatchesAny(std::string_view patterns, std::string_view value) {
  for (const auto pattern : Split(patterns, ':')) {
    if (GlobMatches(pattern, value))
      return true;
  }
  return false;
}

} // namespace

bool GlobMatches(std::string_view pattern, std::string_view value) {
  std::size_t pattern_index = 0;
  std::size_t value_index = 0;
  std::size_t star_index = std::string_view::npos;
  std::size_t star_value_index = 0;

  while (value_index < value.size()) {
    if (pattern_index < pattern.size() &&
        (pattern[pattern_index] == '?' ||
         pattern[pattern_index] == value[value_index])) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < pattern.size() &&
               pattern[pattern_index] == '*') {
      star_index = pattern_index++;
      star_value_index = value_index;
    } else if (star_index != std::string_view::npos) {
      pattern_index = star_index + 1;
      value_index = ++star_value_index;
    } else {
      return false;
    }
  }

  while (pattern_index < pattern.size() && pattern[pattern_index] == '*')
    ++pattern_index;
  return pattern_index == pattern.size();
}

bool FilterMatches(std::string_view filter, std::string_view test_name) {
  const auto separator = filter.find('-');
  auto positive = filter.substr(0, separator);
  const auto negative = separator == std::string_view::npos
                            ? std::string_view()
                            : filter.substr(separator + 1);
  if (positive.empty())
    positive = "*";
  return MatchesAny(positive, test_name) &&
         (negative.empty() || !MatchesAny(negative, test_name));
}

std::string CaseNamespaceFromExecutable(std::string_view executable_path) {
  const auto separator = executable_path.find_last_of("/\\");
  if (separator != std::string_view::npos)
    executable_path.remove_prefix(separator + 1);

  constexpr std::string_view prefix = "dxmt-wine-";
  constexpr std::string_view executable_suffix = ".exe";
  constexpr std::string_view test_suffix = "-tests";
  if (executable_path.starts_with(prefix))
    executable_path.remove_prefix(prefix.size());
  if (executable_path.ends_with(executable_suffix))
    executable_path.remove_suffix(executable_suffix.size());
  if (executable_path.ends_with(test_suffix))
    executable_path.remove_suffix(test_suffix.size());

  if (executable_path.empty())
    return "DXMT";

  std::string result(executable_path);
  for (auto &character : result) {
    if (character == '-')
      character = '_';
    else
      character = static_cast<char>(
          std::toupper(static_cast<unsigned char>(character)));
  }
  return result;
}

std::string CaseIdForTest(std::string_view case_namespace,
                          std::string_view test_name) {
  std::string result;
  result.reserve(case_namespace.size() + test_name.size() + 1);
  if (case_namespace.empty())
    result = "DXMT";
  else
    result = case_namespace;
  result.push_back('.');
  result += test_name;
  return result;
}

std::size_t SelectWorkerCount(const std::vector<ScheduledTest> &tests,
                              std::size_t maximum_worker_count) {
  if (tests.empty() || maximum_worker_count == 0)
    return 0;

  std::uint64_t total_cost = 0;
  for (const auto &test : tests)
    total_cost += test.cost;
  const auto amortized_worker_count = static_cast<std::size_t>(
      (total_cost + kMinimumBatchCost - 1) / kMinimumBatchCost);
  return std::min({maximum_worker_count, tests.size(),
                   std::max<std::size_t>(1, amortized_worker_count)});
}

std::vector<ScheduledTest>
ExtractSerialTests(std::vector<ScheduledTest> &tests) {
  std::vector<ScheduledTest> serial;
  for (auto &test : tests)
    if (test.serial)
      serial.push_back(std::move(test));
  std::erase_if(tests, [](const auto &test) { return test.serial; });
  return serial;
}

std::vector<TestShard> BuildTestShards(std::vector<ScheduledTest> tests,
                                       std::size_t worker_count) {
  if (tests.empty() || worker_count == 0)
    return {};

  worker_count = std::min(worker_count, tests.size());
  std::stable_sort(tests.begin(), tests.end(),
                   [](const ScheduledTest &left, const ScheduledTest &right) {
                     return left.cost > right.cost;
                   });

  std::vector<TestShard> shards(worker_count);
  for (auto &test : tests) {
    const auto shard =
        std::min_element(shards.begin(), shards.end(),
                         [](const TestShard &left, const TestShard &right) {
                           if (left.estimated_cost != right.estimated_cost)
                             return left.estimated_cost < right.estimated_cost;
                           return left.tests.size() < right.tests.size();
                         });
    shard->estimated_cost += test.cost;
    shard->tests.push_back(std::move(test.name));
  }
  return shards;
}

} // namespace dxmt::test
