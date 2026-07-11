#pragma once

#include <concepts>
#include <limits>
#include <utility>

namespace dxmt {

template <std::integral L, std::integral R, std::unsigned_integral T>
constexpr bool
CheckedAdd(L lhs, R rhs, T &result) {
  if (!std::in_range<T>(lhs) || !std::in_range<T>(rhs))
    return false;
  const T converted_lhs = static_cast<T>(lhs);
  const T converted_rhs = static_cast<T>(rhs);
  if (converted_rhs > std::numeric_limits<T>::max() - converted_lhs)
    return false;
  result = converted_lhs + converted_rhs;
  return true;
}

template <std::integral L, std::integral R, std::unsigned_integral T>
constexpr bool
CheckedMultiply(L lhs, R rhs, T &result) {
  if (!std::in_range<T>(lhs) || !std::in_range<T>(rhs))
    return false;
  const T converted_lhs = static_cast<T>(lhs);
  const T converted_rhs = static_cast<T>(rhs);
  if (converted_lhs &&
      converted_rhs > std::numeric_limits<T>::max() / converted_lhs)
    return false;
  result = converted_lhs * converted_rhs;
  return true;
}

template <std::integral V, std::integral A, std::unsigned_integral T>
constexpr bool
CheckedAlign(V value, A alignment, T &result) {
  if (!std::in_range<T>(value) || !std::in_range<T>(alignment))
    return false;
  const T converted_value = static_cast<T>(value);
  const T converted_alignment = static_cast<T>(alignment);
  if (!converted_alignment)
    return false;

  const T remainder = converted_value % converted_alignment;
  if (!remainder) {
    result = converted_value;
    return true;
  }
  return CheckedAdd(converted_value, converted_alignment - remainder, result);
}

} // namespace dxmt
