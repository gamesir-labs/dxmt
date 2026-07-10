#include <dxmt_test.hpp>

#include <array>
#include <filesystem>
#include <string_view>

TEST(TestLayout, KeepsWineInfrastructureInStableLocations) {
  constexpr std::array<std::string_view, 32> required_files = {
      "benchmarks/README.md",
      "benchmarks/include/dxmt_benchmark.hpp",
      "benchmarks/meson.build",
      "benchmarks/support/benchmark_acceptance.py",
      "benchmarks/support/main.cpp",
      "benchmarks/wine/d3d12_integration_benchmark.cpp",
      "benchmarks/wine/meson.build",
      "benchmarks/wine/ue_d3d11_initialization.cpp",
      "benchmarks/wine/ue_d3d12_initialization.cpp",
      "benchmarks/wine/ue_initialization_benchmark.cpp",
      "external/google-benchmark/meson.build",
      "external/googletest/meson.build",
      "tests/README.md",
      "tests/d3d10/meson.build",
      "tests/d3d11/meson.build",
      "tests/d3d11/capability_spec.cpp",
      "tests/d3d11/d3d11_test_context.hpp",
      "tests/d3d12/meson.build",
      "tests/d3d12/capability_spec.cpp",
      "tests/include/dxmt_test_com.hpp",
      "tests/include/dxmt_test.hpp",
      "tests/include/dxmt_test_scheduler.hpp",
      "tests/meson.build",
      "tests/support/main.cpp",
      "tests/support/scheduler.cpp",
      "tests/support/scheduler_plan.cpp",
      "tests/support/wine_process.hpp",
      "tests/support/wine_suite_scheduler.cpp",
      "tests/wine/framework_spec.cpp",
      "tests/wine/layout_spec.cpp",
      "tests/wine/placeholder_spec.cpp",
      "tests/wine/scheduler_spec.cpp",
  };

  for (const auto relative_path : required_files) {
    EXPECT_TRUE(std::filesystem::is_regular_file(relative_path))
        << "missing required Wine test infrastructure file: " << relative_path;
  }
}
