/*
 * This file is part of DXMT.
 *
 * The MD5 round is based on the algorithm by Ron Rivest. DXBC uses the same
 * compression function with a container-specific final block encoding.
 */

#include "dxbc_checksum.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dxmt {
namespace {

constexpr std::size_t kBlockSize = 64;
constexpr std::size_t kChecksumOffset = 4;
constexpr std::size_t kChecksumSize = 16;
constexpr std::size_t kHashedDataOffset = 20;

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

constexpr std::array<unsigned int, 64> kRoundShifts = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

std::uint32_t LoadLe32(const std::uint8_t *data) {
  return std::uint32_t(data[0]) | (std::uint32_t(data[1]) << 8) |
         (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
}

void StoreLe32(std::uint8_t *data, std::uint32_t value) {
  data[0] = static_cast<std::uint8_t>(value);
  data[1] = static_cast<std::uint8_t>(value >> 8);
  data[2] = static_cast<std::uint8_t>(value >> 16);
  data[3] = static_cast<std::uint8_t>(value >> 24);
}

std::uint32_t RotateLeft(std::uint32_t value, unsigned int shift) {
  return (value << shift) | (value >> (32 - shift));
}

class DxbcMd5 {
public:
  void Update(const std::uint8_t *data, std::size_t size) {
    bit_count_ += static_cast<std::uint32_t>(size << 3);

    if (pending_size_) {
      const std::size_t copied = std::min(size, kBlockSize - pending_size_);
      std::memcpy(pending_.data() + pending_size_, data, copied);
      pending_size_ += copied;
      data += copied;
      size -= copied;
      if (pending_size_ == kBlockSize) {
        Transform(pending_.data());
        pending_size_ = 0;
      }
    }

    while (size >= kBlockSize) {
      Transform(data);
      data += kBlockSize;
      size -= kBlockSize;
    }

    if (size) {
      std::memcpy(pending_.data(), data, size);
      pending_size_ = size;
    }
  }

  std::array<std::uint8_t, kChecksumSize> Final() {
    std::array<std::uint8_t, kBlockSize> block = {};
    if (pending_size_ <= 55) {
      StoreLe32(block.data(), bit_count_);
      std::memcpy(block.data() + 4, pending_.data(), pending_size_);
      block[4 + pending_size_] = 0x80;
    } else {
      std::memcpy(block.data(), pending_.data(), pending_size_);
      block[pending_size_] = 0x80;
      Transform(block.data());
      block = {};
      StoreLe32(block.data(), bit_count_);
    }
    StoreLe32(block.data() + 60, (bit_count_ >> 2) | 1);
    Transform(block.data());

    std::array<std::uint8_t, kChecksumSize> digest = {};
    for (std::size_t index = 0; index < state_.size(); ++index)
      StoreLe32(digest.data() + index * 4, state_[index]);
    return digest;
  }

private:
  void Transform(const std::uint8_t *block) {
    std::array<std::uint32_t, 16> words = {};
    for (std::size_t index = 0; index < words.size(); ++index)
      words[index] = LoadLe32(block + index * 4);

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    for (std::uint32_t index = 0; index < 64; ++index) {
      std::uint32_t function = 0;
      std::uint32_t word_index = 0;
      if (index < 16) {
        function = (b & c) | (~b & d);
        word_index = index;
      } else if (index < 32) {
        function = (d & b) | (~d & c);
        word_index = (5 * index + 1) % 16;
      } else if (index < 48) {
        function = b ^ c ^ d;
        word_index = (3 * index + 5) % 16;
      } else {
        function = c ^ (b | ~d);
        word_index = (7 * index) % 16;
      }

      const std::uint32_t next_d = d;
      d = c;
      c = b;
      b += RotateLeft(a + function + kRoundConstants[index] + words[word_index],
                      kRoundShifts[index]);
      a = next_d;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
  }

  std::array<std::uint32_t, 4> state_ = {0x67452301, 0xefcdab89, 0x98badcfe,
                                         0x10325476};
  std::array<std::uint8_t, kBlockSize> pending_ = {};
  std::size_t pending_size_ = 0;
  std::uint32_t bit_count_ = 0;
};

} // namespace

bool ValidateDxbcChecksum(const void *data, std::size_t size) {
  if (!data || size <= kHashedDataOffset)
    return false;

  const auto *bytes = static_cast<const std::uint8_t *>(data);
  DxbcMd5 hash;
  hash.Update(bytes + kHashedDataOffset, size - kHashedDataOffset);
  const auto checksum = hash.Final();
  return !std::memcmp(bytes + kChecksumOffset, checksum.data(),
                      checksum.size());
}

} // namespace dxmt
