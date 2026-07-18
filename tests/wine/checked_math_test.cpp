#include <dxmt_test.hpp>

#include <dxmt_checked_math.hpp>
#include <dxmt_legacy_buffer_slice.hpp>

#include <cstdint>
#include <limits>

TEST(CheckedArithmetic, AddsWithoutWrappingAtTheUnsignedLimit) {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t result = 17;

  EXPECT_TRUE(dxmt::CheckedAdd(maximum, std::uint64_t{0}, result));
  EXPECT_EQ(result, maximum);

  result = 17;
  EXPECT_FALSE(dxmt::CheckedAdd(maximum, std::uint64_t{1}, result));
  EXPECT_EQ(result, 17u);
}

TEST(CheckedArithmetic, MultipliesWithoutWrapping) {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t result = 23;

  EXPECT_TRUE(dxmt::CheckedMultiply(maximum, std::uint64_t{1}, result));
  EXPECT_EQ(result, maximum);

  result = 23;
  EXPECT_FALSE(dxmt::CheckedMultiply(maximum, std::uint64_t{2}, result));
  EXPECT_EQ(result, 23u);

  EXPECT_TRUE(dxmt::CheckedMultiply(std::uint64_t{0}, maximum, result));
  EXPECT_EQ(result, 0u);
}

TEST(CheckedArithmetic, AlignsWithoutWrapping) {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t result = 31;

  EXPECT_TRUE(dxmt::CheckedAlign(maximum - 7, std::uint64_t{8}, result));
  EXPECT_EQ(result, maximum - 7);

  result = 31;
  EXPECT_FALSE(dxmt::CheckedAlign(maximum, std::uint64_t{2}, result));
  EXPECT_EQ(result, 31u);

  EXPECT_FALSE(dxmt::CheckedAlign(std::uint64_t{1}, std::uint64_t{0}, result));
  EXPECT_EQ(result, 31u);
}

TEST(CheckedArithmetic, RejectsNegativeAndNarrowingOperandsWithoutMutation) {
  std::uint8_t result = 19;

  EXPECT_FALSE(dxmt::CheckedAdd(-1, 1, result));
  EXPECT_EQ(result, 19u);
  EXPECT_FALSE(dxmt::CheckedMultiply(1, -1, result));
  EXPECT_EQ(result, 19u);
  EXPECT_FALSE(dxmt::CheckedAlign(256, 16, result));
  EXPECT_EQ(result, 19u);
  EXPECT_FALSE(dxmt::CheckedAlign(1, -2, result));
  EXPECT_EQ(result, 19u);
}

TEST(CheckedArithmetic, HandlesMixedWidthsAndNonPowerOfTwoAlignment) {
  std::uint16_t wide_result = 0;
  EXPECT_TRUE(dxmt::CheckedAdd(std::uint8_t{250}, std::uint64_t{5},
                               wide_result));
  EXPECT_EQ(wide_result, 255u);
  EXPECT_TRUE(dxmt::CheckedMultiply(std::uint32_t{15}, std::uint8_t{17},
                                    wide_result));
  EXPECT_EQ(wide_result, 255u);
  EXPECT_TRUE(dxmt::CheckedAlign(std::uint8_t{255}, std::uint32_t{24},
                                 wide_result));
  EXPECT_EQ(wide_result, 264u);
  EXPECT_TRUE(dxmt::CheckedAlign(std::uint16_t{240}, std::uint8_t{24},
                                 wide_result));
  EXPECT_EQ(wide_result, 240u);

  std::uint8_t narrow_result = 7;
  EXPECT_TRUE(dxmt::CheckedAdd(250, 5, narrow_result));
  EXPECT_EQ(narrow_result, 255u);
  EXPECT_FALSE(dxmt::CheckedAdd(250, 6, narrow_result));
  EXPECT_EQ(narrow_result, 255u);
}

TEST(CheckedArithmetic, PreservesNarrowResultsAcrossMultiplyAndAlignFailure) {
  std::uint8_t result = 13;
  EXPECT_FALSE(dxmt::CheckedMultiply(16, 16, result));
  EXPECT_EQ(result, 13u);
  EXPECT_FALSE(dxmt::CheckedMultiply(-1, 0, result));
  EXPECT_EQ(result, 13u);
  EXPECT_FALSE(dxmt::CheckedAlign(250, 8, result));
  EXPECT_EQ(result, 13u);

  EXPECT_TRUE(dxmt::CheckedAlign(0, 8, result));
  EXPECT_EQ(result, 0u);
}

TEST(LegacyBufferSlice, RejectsRangesThatTheShaderAbiWouldTruncate) {
  constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
  EXPECT_TRUE(dxmt::LegacyBufferSliceRepresentable(maximum, maximum));
  EXPECT_FALSE(dxmt::LegacyBufferSliceRepresentable(
      std::uint64_t{maximum} + 1, maximum));
  EXPECT_FALSE(dxmt::LegacyBufferSliceRepresentable(
      maximum, std::uint64_t{maximum} + 1));
}
