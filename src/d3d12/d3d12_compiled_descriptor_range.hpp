#pragma once

#include <cstdint>
#include <limits>

namespace dxmt::d3d12 {

inline bool TryResolveCompiledNativeDescriptorSpan(
    std::uint32_t heap_index, std::uint32_t heap_count,
    std::uint32_t range_offset, std::uint32_t local_offset,
    std::uint32_t access_count, std::uint32_t *resolved_base) {
  if (!access_count || range_offset >
                           std::numeric_limits<std::uint32_t>::max() -
                               local_offset)
    return false;

  const auto table_offset = range_offset + local_offset;
  if (heap_index > std::numeric_limits<std::uint32_t>::max() - table_offset)
    return false;

  const auto base = heap_index + table_offset;
  if (base >= heap_count || access_count > heap_count - base)
    return false;

  if (resolved_base)
    *resolved_base = base;
  return true;
}

} // namespace dxmt::d3d12
