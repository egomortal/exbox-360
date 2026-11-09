// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace av2::closet_filter {

// Stable identity: a searched picker row is not an enum ordinal.
inline int SortModeFromLabel(std::string_view label) {
  constexpr std::string_view labels[]{"Name", "Type", "Source game", "Category", "Color"};
  for (int i = 0; i < 5; ++i) if (label == labels[i]) return i;
  return -1;
}

// Change only the existing English legend caption. The source control keeps
// its owner-supplied icon, layout and font; no source art is embedded here.
// Returns output bytes including the UTF-16BE terminator, or zero unchanged.
inline std::size_t TryRewriteMarketplaceLegend(bool active, const uint8_t* input,
    std::size_t input_bytes, uint8_t* output, std::size_t output_bytes) {
  if (!active || !input || !output || !input_bytes || input_bytes % 2) return 0;
  const auto unit = [&](std::size_t index) {
    return uint16_t(uint16_t(input[index * 2]) << 8 | input[index * 2 + 1]);
  };
  const std::size_t capacity = input_bytes / 2 < 64 ? input_bytes / 2 : 64;
  std::size_t length = 0;
  while (length < capacity && unit(length)) ++length;
  if (length == capacity) return 0;
  std::size_t prefix = 0;
  if (length && unit(0) >= 0xE000 && unit(0) <= 0xF8FF) {
    prefix = 1;
    while (prefix < length && unit(prefix) == ' ') ++prefix;
  }
  const auto matches = [&](std::string_view label) {
    if (length - prefix != label.size()) return false;
    for (std::size_t i = 0; i < label.size(); ++i)
      if (unit(prefix + i) != uint8_t(label[i])) return false;
    return true;
  };
  if (!matches("Marketplace") && !matches("Avatar Marketplace")) return 0;
  constexpr std::string_view replacement = "Sort / Filter";
  const std::size_t bytes = (prefix + replacement.size() + 1) * 2;
  if (bytes > output_bytes) return 0;
  if (prefix) std::memcpy(output, input, prefix * 2);
  for (std::size_t i = 0; i < replacement.size(); ++i) {
    output[(prefix + i) * 2] = 0;
    output[(prefix + i) * 2 + 1] = uint8_t(replacement[i]);
  }
  output[bytes - 2] = output[bytes - 1] = 0;
  return bytes;
}

}  // namespace av2::closet_filter
