#include <dxmt_test_scheduler.hpp>

#include <algorithm>
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
