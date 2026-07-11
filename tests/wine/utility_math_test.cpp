#include <dxmt_test.hpp>

#include "util_math.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

struct AlignmentCase {
  uint64_t value;
  uint64_t alignment;
  uint64_t up;
  uint64_t down;
};

class AlignmentTest : public ::testing::TestWithParam<AlignmentCase> {};

TEST_P(AlignmentTest, RoundsToRequestedPowerOfTwo) {
  const auto test = GetParam();
  EXPECT_EQ(dxmt::align(test.value, test.alignment), test.up);
  EXPECT_EQ(dxmt::alignDown(test.value, test.alignment), test.down);
}

INSTANTIATE_TEST_SUITE_P(
    Boundaries, AlignmentTest,
    ::testing::Values(AlignmentCase{0, 1, 0, 0}, AlignmentCase{1, 1, 1, 1},
                      AlignmentCase{1, 8, 8, 0}, AlignmentCase{8, 8, 8, 8},
                      AlignmentCase{9, 8, 16, 8},
                      AlignmentCase{4095, 4096, 4096, 0},
                      AlignmentCase{4097, 4096, 8192, 4096}));

TEST(MathUtilities, ClampIncludesBothBounds) {
  EXPECT_EQ(dxmt::clamp(-1, 0, 10), 0);
  EXPECT_EQ(dxmt::clamp(0, 0, 10), 0);
  EXPECT_EQ(dxmt::clamp(5, 0, 10), 5);
  EXPECT_EQ(dxmt::clamp(10, 0, 10), 10);
  EXPECT_EQ(dxmt::clamp(11, 0, 10), 10);
}

TEST(MathUtilities, FloatingClampHandlesNonFiniteValues) {
  EXPECT_FLOAT_EQ(
      dxmt::fclamp(-std::numeric_limits<float>::infinity(), -2.0f, 3.0f),
      -2.0f);
  EXPECT_FLOAT_EQ(
      dxmt::fclamp(std::numeric_limits<float>::infinity(), -2.0f, 3.0f), 3.0f);
  EXPECT_FLOAT_EQ(
      dxmt::fclamp(std::numeric_limits<float>::quiet_NaN(), -2.0f, 3.0f),
      -2.0f);
}

TEST(MathUtilities, IntegerDivisionRoundsUp) {
  EXPECT_EQ(dxmt::divCeil(0u, 4u), 0u);
  EXPECT_EQ(dxmt::divCeil(1u, 4u), 1u);
  EXPECT_EQ(dxmt::divCeil(4u, 4u), 1u);
  EXPECT_EQ(dxmt::divCeil(5u, 4u), 2u);
}

} // namespace
