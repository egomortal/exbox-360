#pragma once
#include <charconv>
#include <chrono>
#include <type_traits>
#include <fast_float/fast_float.h>

namespace av2::compat {
// Locale-independent bounded parsing. Preserve std::from_chars failure semantics:
// fast_float may write infinity/zero on range error, so commit only on success.
template<class T>
std::from_chars_result float_from_chars(const char* first, const char* last, T& value) {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
  T parsed{};
  const auto result = fast_float::from_chars(first, last, parsed, fast_float::chars_format::general);
  if (result.ec == std::errc{}) value = parsed;
  return {result.ptr, result.ec};
}
} // namespace av2::compat

namespace rex::chrono_compat {
// Only the SDK's explicit host/guest/steady clock pairs are supported. Unsupported
// pairs fail at compile time; do not add missing standard-library templates to std.
template<class Destination, class Source> struct clock_time_conversion;
template<class Destination, class Source, class Duration>
auto clock_cast(const std::chrono::time_point<Source, Duration>& time) {
  if constexpr (std::is_same_v<Destination, Source>) return time;
  else return clock_time_conversion<Destination, Source>{}(time);
}
} // namespace rex::chrono_compat
