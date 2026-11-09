// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <string_view>
namespace exbox::editor {
// Preserve the reference's deliberately shallow signature check. This is routing,
// not PNG validation: do not silently reject previously accepted payloads here.
inline bool image_prefix(std::span<const uint8_t> bytes) {
    return bytes.size() > 8 && bytes[0] == 0x89 && bytes[1] == 'P' &&
           bytes[2] == 'N' && bytes[3] == 'G';
}
inline std::string_view gamer_image_destination(std::span<const uint8_t> bytes) {
    if (bytes.size() < 24 || !image_prefix(bytes)) return {};
    const uint32_t width = (uint32_t(bytes[16]) << 24) | (uint32_t(bytes[17]) << 16) |
                           (uint32_t(bytes[18]) << 8) | uint32_t(bytes[19]);
    return width >= 64 ? "gamerpic.png" : "gamerpic_small.png";
}
inline std::string_view tile_image_destination(uint32_t tile_type,
                                               std::span<const uint8_t> bytes) {
    return tile_type == 15 && image_prefix(bytes) ? "fullbody.png" : std::string_view{};
}
}
