#include <dxmt_test.hpp>

#include "sha1/sha1_util.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <unordered_set>

namespace {

struct Sha1Case {
  std::string_view input;
  std::string_view digest;
};

class Sha1KnownVectorTest : public testing::TestWithParam<Sha1Case> {};

} // namespace

TEST_P(Sha1KnownVectorTest, MatchesPublishedDigest) {
  const auto &[input, expected] = GetParam();
  const auto digest = dxmt::Sha1HashState::compute(input.data(), input.size());
  EXPECT_EQ(digest.string(), expected);
}

INSTANTIATE_TEST_SUITE_P(
    StandardMessages, Sha1KnownVectorTest,
    testing::Values(Sha1Case{"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
                    Sha1Case{"abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
                    Sha1Case{"The quick brown fox jumps over the lazy dog",
                             "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"},
                    Sha1Case{"The quick brown fox jumps over the lazy cog",
                             "de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3"}));

TEST(Sha1, IncrementalUpdatesMatchOneShotBinaryInput) {
  constexpr std::array<uint8_t, 9> bytes = {0, 1, 2, 0, 4, 5, 0, 7, 8};
  dxmt::Sha1HashState incremental;
  incremental.update(bytes.data(), 3)
      .update(bytes.data() + 3, 0)
      .update(bytes.data() + 3, bytes.size() - 3);

  EXPECT_EQ(incremental.final(),
            dxmt::Sha1HashState::compute(bytes.data(), bytes.size()));
}

TEST(Sha1, AcceptsANullPointerForEmptyInput) {
  EXPECT_EQ(dxmt::Sha1HashState::compute(nullptr, 0).string(),
            "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST(Sha1, ObjectUpdateHashesTheCompleteObjectRepresentation) {
  constexpr uint32_t value = 0x12345678u;
  dxmt::Sha1HashState object_hash;
  object_hash.update(value);
  EXPECT_EQ(object_hash.final(),
            dxmt::Sha1HashState::compute(&value, sizeof(value)));
}

TEST(Sha1, HandlesMillionByteBoundaryAcrossUnevenChunks) {
  const std::string input(1'000'000, 'a');
  dxmt::Sha1HashState hash;
  for (size_t offset = 0; offset < input.size(); offset += 997) {
    const size_t size = std::min<size_t>(997, input.size() - offset);
    hash.update(input.data() + offset, size);
  }
  EXPECT_EQ(hash.final().string(), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST(Sha1Digest, SupportsEqualityAndHashContainers) {
  const auto first = dxmt::Sha1HashState::compute("first", 5);
  const auto first_again = dxmt::Sha1HashState::compute("first", 5);
  const auto second = dxmt::Sha1HashState::compute("second", 6);

  EXPECT_EQ(first, first_again);
  EXPECT_NE(first, second);
  std::unordered_set<dxmt::Sha1Digest> digests = {first, first_again, second};
  EXPECT_EQ(digests.size(), 2u);
  EXPECT_TRUE(digests.contains(first));
}
