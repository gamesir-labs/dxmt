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

TEST(LegacyBufferSlice, RejectsRangesThatTheShaderAbiWouldTruncate) {
  constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
  EXPECT_TRUE(dxmt::LegacyBufferSliceRepresentable(maximum, maximum));
  EXPECT_FALSE(dxmt::LegacyBufferSliceRepresentable(
      std::uint64_t{maximum} + 1, maximum));
  EXPECT_FALSE(dxmt::LegacyBufferSliceRepresentable(
      maximum, std::uint64_t{maximum} + 1));
}
