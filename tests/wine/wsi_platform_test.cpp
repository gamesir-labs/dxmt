#include <dxmt_test.hpp>

#include <wsi_platform.hpp>

#include <array>
#include <cstdint>
#include <cstring>

TEST(WsiPlatform, AllocatesArbitrarySizesAtTheRequestedAlignment) {
  constexpr std::size_t alignment = 4096;
  constexpr std::size_t size = alignment + 1;

  void *memory = dxmt::wsi::aligned_malloc(size, alignment);
  ASSERT_NE(memory, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(memory) % alignment, 0u);

  std::memset(memory, 0xa5, size);
  EXPECT_EQ(static_cast<unsigned char *>(memory)[size - 1], 0xa5u);
  dxmt::wsi::aligned_free(memory);
}

TEST(WsiPlatform, SupportsCommonValidAlignmentAndSizeCombinations) {
  constexpr std::array<std::size_t, 4> alignments = {
      sizeof(void *), 16u, 64u, 4096u};

  for (const std::size_t alignment : alignments) {
    for (const std::size_t size : {std::size_t{1}, alignment,
                                   alignment + 3u}) {
      void *memory = dxmt::wsi::aligned_malloc(size, alignment);
      ASSERT_NE(memory, nullptr);
      EXPECT_EQ(reinterpret_cast<std::uintptr_t>(memory) % alignment, 0u);

      std::memset(memory, 0x5a, size);
      EXPECT_EQ(static_cast<unsigned char *>(memory)[0], 0x5au);
      EXPECT_EQ(static_cast<unsigned char *>(memory)[size - 1], 0x5au);
      dxmt::wsi::aligned_free(memory);
    }
  }

  dxmt::wsi::aligned_free(nullptr);
}
