#include <dxmt_test.hpp>

#include <array>
#include <filesystem>
#include <string_view>

TEST(TestLayout, KeepsInfrastructureInStableLocations) {
  constexpr std::array<std::string_view, 22> required_files = {
      "benchmarks/README.md",
      "benchmarks/budgets/placeholder.json",
      "benchmarks/budgets/test_scheduler.json",
      "benchmarks/include/dxmt_benchmark.hpp",
      "benchmarks/meson.build",
      "benchmarks/suites/meson.build",
      "benchmarks/suites/placeholder_benchmark.cpp",
      "benchmarks/suites/test_scheduler_benchmark.cpp",
      "benchmarks/support/benchmark_acceptance.py",
      "benchmarks/support/main.cpp",
      "external/google-benchmark/meson.build",
      "external/googletest/meson.build",
      "tests/README.md",
      "tests/include/dxmt_test.hpp",
      "tests/include/dxmt_test_scheduler.hpp",
      "tests/meson.build",
      "tests/support/main.cpp",
      "tests/support/scheduler.cpp",
      "tests/support/scheduler_plan.cpp",
      "tests/unit/meson.build",
      "tests/unit/placeholder_test.cpp",
      "tests/unit/scheduler_test.cpp",
  };

  for (const auto relative_path : required_files) {
    EXPECT_TRUE(std::filesystem::is_regular_file(relative_path))
        << "missing required test infrastructure file: " << relative_path;
  }
}
