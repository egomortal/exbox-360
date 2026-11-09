// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "user_identity.hpp"
namespace exbox::editor {
// Display labels only: never rename the profile directory or change save identity.
// A wide capacity is measured in UTF-16 code units, not bytes. Filtered labels
// contain ASCII, so each character becomes one big-endian UTF-16 code unit.
inline IdentityStatus write_user_name(uint32_t user, std::string_view configured,
                                     uint32_t capacity, std::span<uint8_t> destination,
                                     bool wide) {
    if (user >= 4) return IdentityStatus::invalid_argument;
    if (user != 0) return IdentityStatus::no_such_user;
    if (wide && capacity < 16) return IdentityStatus::invalid_argument;
    const size_t units = std::min(capacity, uint32_t{16});
    const size_t stride = wide ? 2 : 1;
    if (destination.size() < units * stride) return IdentityStatus::invalid_argument;
    // The narrow export accepts zero capacity as a successful no-op.
    if (!units) return IdentityStatus::success;
    const auto label = display_name(configured);
    const size_t count = std::min(label.size(), units - 1);
    for (size_t i = 0; i <= count; ++i) {
        const uint8_t value = i == count ? 0 : static_cast<uint8_t>(label[i]);
        if (wide) destination[i * 2] = 0;
        destination[i * stride + stride - 1] = value;
    }
    return IdentityStatus::success;
}
}
