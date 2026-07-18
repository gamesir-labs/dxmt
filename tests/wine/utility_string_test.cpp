#include <dxmt_test.hpp>

#include "util_string.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

struct UnicodeScalarCase {
  uint32_t value;
  size_t utf8_length;
  size_t utf16_length;
};

class UnicodeScalarTest : public ::testing::TestWithParam<UnicodeScalarCase> {};

TEST_P(UnicodeScalarTest, RoundTripsAcrossUtfEncodings) {
  const auto test = GetParam();
  std::array<uint8_t, 8> utf8 = {};
  std::array<uint16_t, 4> utf16 = {};
  std::array<uint32_t, 2> utf32 = {};

  EXPECT_EQ(
      dxmt::str::encodeChar(utf8.data(), utf8.data() + utf8.size(), test.value),
      test.utf8_length);
  EXPECT_EQ(dxmt::str::encodeChar(utf16.data(), utf16.data() + utf16.size(),
                                  test.value),
            test.utf16_length);
  EXPECT_EQ(dxmt::str::encodeChar(utf32.data(), utf32.data() + utf32.size(),
                                  test.value),
            1u);

  uint32_t decoded = 0;
  EXPECT_EQ(dxmt::str::decodeChar(utf8.data(), utf8.data() + test.utf8_length,
                                  decoded),
            utf8.data() + test.utf8_length);
  EXPECT_EQ(decoded, test.value);
  EXPECT_EQ(dxmt::str::decodeChar(utf16.data(),
                                  utf16.data() + test.utf16_length, decoded),
            utf16.data() + test.utf16_length);
  EXPECT_EQ(decoded, test.value);
  EXPECT_EQ(dxmt::str::decodeChar(utf32.data(), utf32.data() + 1, decoded),
            utf32.data() + 1);
  EXPECT_EQ(decoded, test.value);
}

INSTANTIATE_TEST_SUITE_P(
    ScalarBoundaries, UnicodeScalarTest,
    ::testing::Values(
        UnicodeScalarCase{0x00, 1, 1}, UnicodeScalarCase{0x7f, 1, 1},
        UnicodeScalarCase{0x80, 2, 1}, UnicodeScalarCase{0x7ff, 2, 1},
        UnicodeScalarCase{0x800, 3, 1}, UnicodeScalarCase{0xd7ff, 3, 1},
        UnicodeScalarCase{0xe000, 3, 1}, UnicodeScalarCase{0xffff, 3, 1},
        UnicodeScalarCase{0x10000, 4, 2}, UnicodeScalarCase{0x10ffff, 4, 2}),
    [](const ::testing::TestParamInfo<UnicodeScalarCase> &info) {
      return "U" + std::to_string(info.param.value);
    });

class InvalidUnicodeScalarTest : public ::testing::TestWithParam<uint32_t> {};

TEST_P(InvalidUnicodeScalarTest, IsRejectedByEveryEncoding) {
  const auto value = GetParam();
  std::array<uint8_t, 8> utf8 = {};
  std::array<uint16_t, 4> utf16 = {};
  std::array<uint32_t, 2> utf32 = {};

  EXPECT_EQ(
      dxmt::str::encodeChar(utf8.data(), utf8.data() + utf8.size(), value), 0u);
  EXPECT_EQ(
      dxmt::str::encodeChar(utf16.data(), utf16.data() + utf16.size(), value),
      0u);
  EXPECT_EQ(
      dxmt::str::encodeChar(utf32.data(), utf32.data() + utf32.size(), value),
      0u);
}

INSTANTIATE_TEST_SUITE_P(InvalidScalars, InvalidUnicodeScalarTest,
                         ::testing::Values(0xd800u, 0xdfffu, 0x110000u,
                                           0xffffffffu));

TEST(UnicodeEncoding, DoesNotPartiallyWriteWhenCapacityIsInsufficient) {
  std::array<uint8_t, 4> utf8 = {0xaa, 0xaa, 0xaa, 0xaa};
  std::array<uint16_t, 2> utf16 = {0xaaaa, 0xaaaa};

  EXPECT_EQ(dxmt::str::encodeChar(utf8.data(), utf8.data() + 3, 0x1f600), 0u);
  EXPECT_EQ(utf8, (std::array<uint8_t, 4>{0xaa, 0xaa, 0xaa, 0xaa}));
  EXPECT_EQ(dxmt::str::encodeChar(utf16.data(), utf16.data() + 1, 0x1f600), 0u);
  EXPECT_EQ(utf16, (std::array<uint16_t, 2>{0xaaaa, 0xaaaa}));
}

struct InvalidUtf8Case {
  std::vector<uint8_t> bytes;
  size_t consumed;
};

class InvalidUtf8Test : public ::testing::TestWithParam<InvalidUtf8Case> {};

TEST_P(InvalidUtf8Test, ProducesReplacementCharacterAndMakesProgress) {
  const auto &test = GetParam();
  uint32_t decoded = 0;
  const auto *next = dxmt::str::decodeChar(
      test.bytes.data(), test.bytes.data() + test.bytes.size(), decoded);

  EXPECT_EQ(decoded, uint32_t('?'));
  EXPECT_EQ(next, test.bytes.data() + test.consumed);
}

INSTANTIATE_TEST_SUITE_P(
    MalformedSequences, InvalidUtf8Test,
    ::testing::Values(InvalidUtf8Case{{0x80}, 1},
                      InvalidUtf8Case{{0xc0, 0xaf}, 1},
                      InvalidUtf8Case{{0xe2, 0x28, 0xa1}, 1},
                      InvalidUtf8Case{{0xed, 0xa0, 0x80}, 3},
                      InvalidUtf8Case{{0xf4, 0x90, 0x80, 0x80}, 4},
                      InvalidUtf8Case{{0xf0, 0x9f, 0x98}, 3}));

TEST(UnicodeDecoding, RejectsUnpairedUtf16Surrogates) {
  const std::array<uint16_t, 2> high_then_ascii = {0xd800, 'A'};
  const std::array<uint16_t, 1> low = {0xdc00};
  uint32_t decoded = 0;

  EXPECT_EQ(dxmt::str::decodeChar(high_then_ascii.data(),
                                  high_then_ascii.data() + 2, decoded),
            high_then_ascii.data() + 1);
  EXPECT_EQ(decoded, uint32_t('?'));
  EXPECT_EQ(dxmt::str::decodeChar(low.data(), low.data() + 1, decoded),
            low.data() + 1);
  EXPECT_EQ(decoded, uint32_t('?'));
}

TEST(UnicodeDecoding, HandlesEmptyAndInvalidUtf32Input) {
  std::array<uint8_t, 1> utf8 = {};
  std::array<uint16_t, 1> utf16 = {};
  const std::array<uint32_t, 2> utf32 = {0xd800, 0x110000};
  uint32_t decoded = 0;

  EXPECT_EQ(dxmt::str::decodeChar(utf8.data(), utf8.data(), decoded),
            utf8.data());
  EXPECT_EQ(decoded, uint32_t('?'));
  EXPECT_EQ(dxmt::str::decodeChar(utf16.data(), utf16.data(), decoded),
            utf16.data());
  EXPECT_EQ(decoded, uint32_t('?'));

  auto *next =
      dxmt::str::decodeChar(utf32.data(), utf32.data() + utf32.size(), decoded);
  EXPECT_EQ(next, utf32.data() + 1);
  EXPECT_EQ(decoded, uint32_t('?'));
  EXPECT_EQ(dxmt::str::decodeChar(next, utf32.data() + utf32.size(), decoded),
            utf32.data() + 2);
  EXPECT_EQ(decoded, uint32_t('?'));
}

TEST(UnicodeTranscoding, ReportsRequiredLengthAndWritesWholeCharacters) {
  const std::array<uint8_t, 5> source = {'A', 0xf0, 0x9f, 0x98, 0x80};
  const auto required = dxmt::str::transcodeString<uint16_t>(
      nullptr, 0, source.data(), source.size());
  EXPECT_EQ(required, 3u);

  std::array<uint16_t, 3> full = {};
  EXPECT_EQ(dxmt::str::transcodeString(full.data(), full.size(), source.data(),
                                       source.size()),
            3u);
  EXPECT_EQ(full, (std::array<uint16_t, 3>{'A', 0xd83d, 0xde00}));

  std::array<uint16_t, 2> truncated = {0xaaaa, 0xaaaa};
  EXPECT_EQ(dxmt::str::transcodeString(truncated.data(), truncated.size(),
                                       source.data(), source.size()),
            1u);
  EXPECT_EQ(truncated, (std::array<uint16_t, 2>{'A', 0xaaaa}));
}

TEST(UnicodeTranscoding, StopsAfterAnEmbeddedNullTerminator) {
  const std::array<uint8_t, 3> source = {'A', 0, 'B'};
  EXPECT_EQ(dxmt::str::transcodeString<uint16_t>(nullptr, 0, source.data(),
                                                 source.size()),
            2u);

  std::array<uint16_t, 4> destination = {0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa};
  EXPECT_EQ(dxmt::str::transcodeString(destination.data(), destination.size(),
                                       source.data(), source.size()),
            2u);
  EXPECT_EQ(destination,
            (std::array<uint16_t, 4>{'A', 0, 0xaaaa, 0xaaaa}));
}

TEST(StringUtilities, FormatsCopiesAndSplitsText) {
  EXPECT_EQ(dxmt::str::format("dx", 12, '-', 3.5), "dx12-3.5");

  std::array<char, 5> destination = {};
  dxmt::str::strlcpy(destination.data(), "abcdef", destination.size());
  EXPECT_STREQ(destination.data(), "abcd");

  EXPECT_EQ(dxmt::str::split(" alpha,,beta;gamma ", " ,;"),
            (std::vector<std::string_view>{"alpha", "beta", "gamma"}));
  EXPECT_TRUE(dxmt::str::split("", ",").empty());

  std::array<char, 2> untouched = {'x', 'y'};
  dxmt::str::strlcpy(untouched.data(), "value", 0);
  EXPECT_EQ(untouched, (std::array<char, 2>{'x', 'y'}));
  dxmt::str::strlcpy(untouched.data(), "value", 1);
  EXPECT_EQ(untouched, (std::array<char, 2>{'\0', 'y'}));

  EXPECT_EQ(dxmt::str::length("dxmt"), 4u);
  EXPECT_EQ(dxmt::str::split("no delimiters", ""),
            (std::vector<std::string_view>{"no delimiters"}));
}

} // namespace
