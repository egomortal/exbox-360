// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
namespace exbox::editor {
enum class ProfileBufferStatus { ready, invalid_argument, insufficient_buffer };
struct ProfileBufferPlan {
    ProfileBufferStatus status;
    uint32_t header_bytes;
    uint32_t total_bytes;
    bool update_size;
};
// XUID/flags validation and profile lookup belong to the runtime adapter.
// This pure step mirrors only size negotiation, before user/settings lookup.
inline ProfileBufferPlan profile_buffer_plan(std::span<const uint32_t> ids,
                                             bool size_present, uint32_t supplied,
                                             bool output_present) {
    if (ids.empty() || ids.size() > 32 || !size_present || (supplied && !output_present))
        return {ProfileBufferStatus::invalid_argument, 0, 0, false};
    const auto header = uint32_t{8} + uint32_t(ids.size()) * 40U;
    uint32_t total = header;
    for (const auto id : ids) {
        const auto type = id >> 28;
        // Packed key: type bits28..31; declared payload length bits16..27.
        if (type == 4 || type == 6) total += (id >> 16) & 0xfffU;
    }
    if (!output_present || supplied < total)
        return {ProfileBufferStatus::insufficient_buffer, header, total, supplied == 0};
    return {ProfileBufferStatus::ready, header, total, false};
}
}
