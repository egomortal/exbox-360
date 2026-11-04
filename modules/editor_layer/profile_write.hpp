// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace exbox::editor {
inline bool profile_binary_write(uint8_t type) { return type == 0 || type == 6; }
// Own the payload before adding it to the SDK profile. Null guest data is an
// explicit zero-filled payload, not an absent setting or deletion request.
inline std::vector<uint8_t> profile_payload(std::span<const uint8_t> bytes,
                                            size_t zero_count, bool present) {
    if (!present) return std::vector<uint8_t>(zero_count, 0);
    return {bytes.begin(), bytes.end()};
}
}
