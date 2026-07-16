#include <dxmt_test.hpp>

#include <wsi_platform.hpp>

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
