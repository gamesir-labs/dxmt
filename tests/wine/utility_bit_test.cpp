#include <dxmt_test.hpp>

#include "util_bit.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

struct BitCountCase {
  uint32_t value;
  uint32_t population;
  uint32_t leading_zeroes;
  uint32_t trailing_zeroes;
};

class BitCountTest : public ::testing::TestWithParam<BitCountCase> {};

TEST_P(BitCountTest, MatchesIntegerBitOperations) {
  const auto test = GetParam();

  EXPECT_EQ(dxmt::bit::popcnt(test.value), test.population);
  EXPECT_EQ(dxmt::bit::lzcnt(test.value), test.leading_zeroes);
  EXPECT_EQ(dxmt::bit::tzcnt(test.value), test.trailing_zeroes);
}

INSTANTIATE_TEST_SUITE_P(
    BoundaryValues, BitCountTest,
    ::testing::Values(BitCountCase{0, 0, 32, 32}, BitCountCase{1, 1, 31, 0},
                      BitCountCase{0x80000000u, 1, 0, 31},
                      BitCountCase{0x00f00010u, 5, 8, 4},
                      BitCountCase{std::numeric_limits<uint32_t>::max(), 32, 0,
                                   0}));

TEST(BitOperations, ExtractsAndPacksFields) {
  EXPECT_EQ(dxmt::bit::extract<uint32_t>(0xd6u, 1, 4), 0xbu);

  uint32_t packed = 0;
  uint32_t shift = 0;
  EXPECT_EQ(dxmt::bit::pack(packed, shift, 5u, 3), 0u);
  EXPECT_EQ(dxmt::bit::pack(packed, shift, 2u, 2), 0u);
  EXPECT_EQ(packed, 0x15u);

  uint32_t unpacked = 0;
  shift = 0;
  EXPECT_EQ(dxmt::bit::unpack(unpacked, packed, shift, 3), 0u);
  EXPECT_EQ(unpacked, 5u);
  EXPECT_EQ(dxmt::bit::unpack(unpacked, packed, shift, 2), 0u);
  EXPECT_EQ(unpacked, 2u);
}

TEST(BitOperations, PreservesObjectRepresentationWhenCasting) {
  constexpr uint32_t bits = 0x3f800000u;
  EXPECT_FLOAT_EQ(dxmt::bit::cast<float>(bits), 1.0f);
  EXPECT_EQ(dxmt::bit::cast<uint32_t>(1.0f), bits);
}

struct alignas(16) ComparableBytes {
  std::array<uint8_t, 48> bytes = {};
};

TEST(BitOperations, ComparesEveryByteOfAlignedObjects) {
  ComparableBytes left;
  ComparableBytes right;
  EXPECT_TRUE(dxmt::bit::bcmpeq(&left, &right));

  right.bytes.front() = 1;
  EXPECT_FALSE(dxmt::bit::bcmpeq(&left, &right));
  right = {};
  right.bytes.back() = 1;
  EXPECT_FALSE(dxmt::bit::bcmpeq(&left, &right));
}

class DynamicBitPositionTest : public ::testing::TestWithParam<uint32_t> {};

TEST_P(DynamicBitPositionTest, ReadsAndWritesAcrossStorageWords) {
  const auto position = GetParam();
  dxmt::bit::bitvector bits;

  EXPECT_FALSE(bits.any());
  bits.set(position, true);
  EXPECT_TRUE(bits.get(position));
  EXPECT_EQ(bits.bitCount(), size_t(position + 1));
  EXPECT_EQ(bits.dwordCount(), size_t(position / 32 + 1));
  EXPECT_TRUE(bits.exchange(position, false));
  EXPECT_FALSE(bits.get(position));
  bits.flip(position);
  EXPECT_TRUE(bits.get(position));
}

INSTANTIATE_TEST_SUITE_P(WordBoundaries, DynamicBitPositionTest,
                         ::testing::Values(0u, 1u, 31u, 32u, 33u, 63u, 64u,
                                           95u),
                         [](const ::testing::TestParamInfo<uint32_t> &info) {
                           return "Bit" + std::to_string(info.param);
                         });

TEST(FixedBitset, MasksUnusedBitsAndSupportsBulkOperations) {
  dxmt::bit::bitset<70> bits;
  static_assert(bits.bitCount() == 70);
  static_assert(bits.qwordCount() == 2);

  bits.setAll();
  EXPECT_EQ(bits.qword(0), std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(bits.qword(1), 0x3fu);

  bits.clearAll();
  bits.setN(66);
  EXPECT_EQ(bits.qword(0), std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(bits.qword(1), 0x3u);
  EXPECT_TRUE(bits.exchange(65, false));
  EXPECT_FALSE(bits.get(65));
  bits.flip(65);
  EXPECT_TRUE(bits.get(65));
}

TEST(DynamicBitvector, BulkOperationsRespectLogicalSize) {
  dxmt::bit::bitvector bits;
  bits.setN(35);
  EXPECT_EQ(bits.bitCount(), 35u);
  EXPECT_EQ(bits.dword(0), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(bits.dword(1), 0x7u);

  bits.clearAll();
  EXPECT_FALSE(bits.any());
  bits.setAll();
  EXPECT_EQ(bits.dword(0), std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(bits.dword(1), 0x7u);
}

TEST(BitMask, IteratesSetBitsInAscendingOrder) {
  std::vector<uint32_t> positions;
  for (const auto position : dxmt::bit::BitMask(0x80000109u))
    positions.push_back(position);

  EXPECT_EQ(positions, (std::vector<uint32_t>{0, 3, 8, 31}));
}

} // namespace
