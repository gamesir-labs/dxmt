#pragma once

#include <cstddef>
#include <limits>
#include <utility>

namespace dxmt {

template <typename T>
constexpr bool
ArgumentBufferByteSize(std::size_t count, std::size_t &size) {
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
    return false;
  size = count * sizeof(T);
  return true;
}

template <typename T, typename Slice>
T *MappedArgumentBufferSlice(Slice &slice, std::size_t count) {
  std::size_t required_size = 0;
  if (!ArgumentBufferByteSize<T>(count, required_size) ||
      !slice.mapped || !slice.gpu_buffer || slice.length < required_size)
    return nullptr;

  return static_cast<T *>(slice.mapped);
}

template <typename T, bool ComputeCommandEncoder = false, typename Context,
          typename Writer>
bool TryWriteMappedArgumentBuffer(Context &context, std::size_t offset,
                                  Writer &&writer) {
  auto *destination =
      context.template getMappedArgumentBuffer<T, ComputeCommandEncoder>(
          offset);
  if (!destination)
    return false;

  std::forward<Writer>(writer)(*destination);
  return true;
}

} // namespace dxmt
