#include <dxmt_test.hpp>

#include "config/config.hpp"
#include "util_env.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace {

struct BooleanOptionCase {
  const char *text;
  bool fallback;
  bool expected;
};

class BooleanOptionTest : public ::testing::TestWithParam<BooleanOptionCase> {};

TEST_P(BooleanOptionTest, ParsesCaseInsensitivelyOrUsesFallback) {
  const auto test = GetParam();
  dxmt::Config config;
  config.setOption("value", test.text);
  EXPECT_EQ(config.getOption<bool>("value", test.fallback), test.expected);
}

INSTANTIATE_TEST_SUITE_P(
    Values, BooleanOptionTest,
    ::testing::Values(BooleanOptionCase{"true", false, true},
                      BooleanOptionCase{"TRUE", false, true},
                      BooleanOptionCase{"False", true, false},
                      BooleanOptionCase{"1", true, true},
                      BooleanOptionCase{"yes", false, false}));

struct IntegerOptionCase {
  const char *text;
  int32_t expected;
};

class IntegerOptionTest : public ::testing::TestWithParam<IntegerOptionCase> {};

TEST_P(IntegerOptionTest, ParsesTheEntireBoundedValue) {
  const auto test = GetParam();
  dxmt::Config config;
  config.setOption("value", test.text);
  EXPECT_EQ(config.getOption<int32_t>("value", 17), test.expected);
}

INSTANTIATE_TEST_SUITE_P(
    ValidAndInvalid, IntegerOptionTest,
    ::testing::Values(
        IntegerOptionCase{"0", 0}, IntegerOptionCase{"-1", -1},
        IntegerOptionCase{"2147483647", std::numeric_limits<int32_t>::max()},
        IntegerOptionCase{"-2147483648", std::numeric_limits<int32_t>::min()},
        IntegerOptionCase{"", 17}, IntegerOptionCase{"-", 17},
        IntegerOptionCase{"+1", 17}, IntegerOptionCase{"1x", 17},
        IntegerOptionCase{"2147483648", 17},
        IntegerOptionCase{"-2147483649", 17}));

struct FloatOptionCase {
  const char *text;
  float expected;
};

class FloatOptionTest : public ::testing::TestWithParam<FloatOptionCase> {};

TEST_P(FloatOptionTest, ParsesFiniteGeneralFormatOrUsesFallback) {
  const auto test = GetParam();
  dxmt::Config config;
  config.setOption("value", test.text);
  EXPECT_FLOAT_EQ(config.getOption<float>("value", 17.0f), test.expected);
}

INSTANTIATE_TEST_SUITE_P(Values, FloatOptionTest,
                         ::testing::Values(FloatOptionCase{"0", 0.0f},
                                           FloatOptionCase{"-1.25", -1.25f},
                                           FloatOptionCase{"1e2", 100.0f},
                                           FloatOptionCase{".5", 0.5f},
                                           FloatOptionCase{"nan", 17.0f},
                                           FloatOptionCase{"inf", 17.0f},
                                           FloatOptionCase{"1.0x", 17.0f}));

TEST(Config, DistinguishesMissingAndExplicitlyEmptyStrings) {
  dxmt::Config config;
  EXPECT_EQ(config.getOption<std::string>("missing", "fallback"), "fallback");

  config.setOption("empty", "");
  EXPECT_EQ(config.getOption<std::string>("empty", "fallback"), "");
}

TEST(Config, MergeKeepsExistingValuesAndAddsMissingOnes) {
  dxmt::Config primary;
  primary.setOption("shared", "primary");
  dxmt::Config defaults;
  defaults.setOption("shared", "default");
  defaults.setOption("added", "value");

  primary.merge(defaults);
  EXPECT_EQ(primary.getOption<std::string>("shared"), "primary");
  EXPECT_EQ(primary.getOption<std::string>("added"), "value");
}

TEST(Config, ParsesTristateAndAppliesOverrides) {
  dxmt::Config config;
  config.setOption("auto", "AUTO");
  config.setOption("enabled", "true");
  config.setOption("disabled", "false");

  EXPECT_EQ(config.getOption<dxmt::Tristate>("auto"), dxmt::Tristate::Auto);
  EXPECT_EQ(config.getOption<dxmt::Tristate>("enabled"), dxmt::Tristate::True);
  EXPECT_EQ(config.getOption<dxmt::Tristate>("disabled"),
            dxmt::Tristate::False);

  bool value = true;
  dxmt::applyTristate(value, dxmt::Tristate::Auto);
  EXPECT_TRUE(value);
  dxmt::applyTristate(value, dxmt::Tristate::False);
  EXPECT_FALSE(value);
  dxmt::applyTristate(value, dxmt::Tristate::True);
  EXPECT_TRUE(value);
}

TEST(Config, InvalidTristateKeepsFallbackAndLowercasingIsAsciiOnly) {
  dxmt::Config config;
  config.setOption("invalid", "sometimes");
  EXPECT_EQ(config.getOption<dxmt::Tristate>("invalid", dxmt::Tristate::True),
            dxmt::Tristate::True);
  EXPECT_EQ(dxmt::Config::toLower("DxMT-123_"), "dxmt-123_");

  std::string bytes = "A";
  bytes.push_back(static_cast<char>(0xff));
  const auto lowered = dxmt::Config::toLower(bytes);
  ASSERT_EQ(lowered.size(), 2u);
  EXPECT_EQ(lowered[0], 'a');
  EXPECT_EQ(static_cast<unsigned char>(lowered[1]), 0xffu);
}

TEST(Config, MatchesBuiltInProfilesWithoutCaseSensitivity) {
  const auto config = dxmt::Config::getAppConfig("C:\\Games\\yuanshen.exe");
  EXPECT_EQ(config.getOption<std::string>("dxgi.customVendorId"), "1002");
  EXPECT_EQ(config.getOption<std::string>("dxgi.customDeviceId"), "7340");
  EXPECT_EQ(config.getOption<std::string>("dxgi.customDeviceDesc"),
            "AMD Radeon Pro 5300M");
}

struct ExtensionCase {
  const char *name;
  const char *extension;
  bool matches;
};

class FileExtensionTest : public ::testing::TestWithParam<ExtensionCase> {};

TEST_P(FileExtensionTest, MatchesTheCompleteSuffixCaseInsensitively) {
  const auto test = GetParam();
  EXPECT_EQ(dxmt::env::matchFileExtension(test.name, test.extension) !=
                std::string::npos,
            test.matches);
}

INSTANTIATE_TEST_SUITE_P(
    Names, FileExtensionTest,
    ::testing::Values(ExtensionCase{"game.exe", "exe", true},
                      ExtensionCase{"GAME.EXE", "exe", true},
                      ExtensionCase{"archive.tar.gz", "gz", true},
                      ExtensionCase{"game.ex", "exe", false},
                      ExtensionCase{"game.exe.bak", "exe", false},
                      ExtensionCase{"game", "exe", false},
                      ExtensionCase{"game.exe", "ex", false}));

TEST(Environment, ReportsTheRunningExecutable) {
  const auto path = dxmt::env::getExePath();
  EXPECT_FALSE(path.empty());
  EXPECT_TRUE(std::filesystem::is_regular_file(path));
  EXPECT_EQ(dxmt::env::getExeName(), std::filesystem::path(path).filename());

#ifdef _WIN32
  const auto unix_path = dxmt::env::getUnixPath(path);
  EXPECT_FALSE(unix_path.empty());
  EXPECT_TRUE(std::filesystem::is_regular_file(unix_path));
#else
  EXPECT_EQ(dxmt::env::getUnixPath(path), path);
#endif
}

} // namespace
