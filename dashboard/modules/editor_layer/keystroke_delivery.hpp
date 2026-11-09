// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "keystroke_queue.hpp"
#include <cstdint>
#include <span>

namespace exbox::editor {
// The SDK is called first by the platform adapter. Its result and output remain
// authoritative whenever a real controller supplied a stroke. This function
// owns only the fallback serialization, with no dependency on guest memory.
// Empty spans represent null guest pointers; the adapter validates translations.
inline bool deliver_fallback(KeystrokeQueue& queue, uint64_t now,
                             uint32_t sdk_result, uint32_t query_flags,
                             std::span<uint8_t> output,
                             std::span<uint8_t> user_index) {
    if (sdk_result == 0 || output.size() < 8) return false;
    // A zero device mask requests any device. Bit zero requests gamepads.
    if ((query_flags & 255U) != 0 && (query_flags & 1U) == 0) return false;
    if (!user_index.empty() && user_index.size() < 4) return false;
    const auto stroke = queue.poll(now);
    if (!stroke) return false;
    // XINPUT_KEYSTROKE is eight bytes, with big-endian 16-bit fields on PPC.
    output[0] = static_cast<uint8_t>(stroke->key >> 8);
    output[1] = static_cast<uint8_t>(stroke->key);
    output[2] = output[3] = 0; // Unicode: gamepad events have no text character.
    output[4] = static_cast<uint8_t>(stroke->flags >> 8);
    output[5] = static_cast<uint8_t>(stroke->flags);
    output[6] = output[7] = 0; // Local user zero; no keyboard HID identifier.
    if (!user_index.empty())
        for (unsigned i = 0; i != 4; ++i) user_index[i] = 0;
    return true; // Adapter changes r3 to success only on this path.
}
} // namespace exbox::editor
