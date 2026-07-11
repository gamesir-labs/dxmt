#pragma once

#include <cstdint>
#include <limits>

namespace dxmt {

// The legacy Metal shader ABI stores both buffer byte offsets and byte ranges
// in 32-bit fields. Native descriptor-table shaders use a separate 64-bit ABI.
constexpr bool
LegacyBufferSliceRepresentable(std::uint64_t byte_offset,
                               std::uint64_t byte_length) {
  return byte_offset <= std::numeric_limits<std::uint32_t>::max() &&
         byte_length <= std::numeric_limits<std::uint32_t>::max();
}

} // namespace dxmt
