// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
namespace exbox::editor {
enum class HiveStatus { success, not_found, insufficient_buffer, invalid_span };
inline std::string offline_hive_value(std::string_view key, std::string_view host, uint16_t port) {
    if (key == "AvatarPhotoBoothEnabled") return "1";
    if (key == "CatalogUriRoot" || key == "CatalogCDNUriRoot") return std::string(host);
    if (key == "CatalogUriPort" || key == "CatalogCDNUriPort") return std::to_string(port);
    return {};
}
// Capacity is in bytes for the narrow export. An absent/zero output returns
// not-found even for a known key; preserve this unusual reference convention.
inline HiveStatus copy_hive_value(std::string_view value, bool present, uint32_t capacity,
                                 std::span<uint8_t> destination) {
    if (value.empty() || !present || !capacity) return HiveStatus::not_found;
    if (value.size() >= capacity) return HiveStatus::insufficient_buffer;
    if (destination.size() <= value.size()) return HiveStatus::invalid_span;
    std::copy(value.begin(), value.end(), destination.begin());
    destination[value.size()] = 0;
    return HiveStatus::success;
}
}
