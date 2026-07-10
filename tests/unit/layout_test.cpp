#include <dxmt_test.hpp>

#include <array>
#include <filesystem>
#include <string_view>

TEST(TestLayout, KeepsInfrastructureInStableLocations) {
  constexpr std::array<std::string_view, 7> required_files = {
    "external/googletest/meson.build",
    "tests/README.md",
    "tests/include/dxmt_test.hpp",
    "tests/meson.build",
    "tests/support/main.cpp",
    "tests/unit/meson.build",
    "tests/unit/placeholder_test.cpp",
  };

  for (const auto relative_path : required_files) {
    EXPECT_TRUE(std::filesystem::is_regular_file(relative_path))
      << "missing required test infrastructure file: " << relative_path;
  }
}
