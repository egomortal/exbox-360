// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/user_names.hpp"
#include <array>
#include <cassert>
using namespace exbox::editor;
int main() {
    for (bool wide : {false, true}) for (uint32_t capacity = 0; capacity <= 40; ++capacity) {
        std::array<uint8_t, 80> buffer; buffer.fill(0xcc);
        const auto output = std::span(buffer).subspan(8, 64);
        const auto result = write_user_name(0, "A\nB", capacity, output, wide);
        if (wide && capacity < 16) {
            assert(result == IdentityStatus::invalid_argument);
            for (auto b : buffer) assert(b == 0xcc);
            continue;
        }
        assert(result == IdentityStatus::success);
        const size_t chars = capacity ? std::min(size_t{2}, size_t(capacity - 1)) : 0;
        const size_t written = capacity ? (chars + 1) * (wide ? 2 : 1) : 0;
        for (size_t i = 0; i < buffer.size(); ++i) {
            if (i < 8 || i >= 8 + written) assert(buffer[i] == 0xcc);
        }
        if (capacity) {
            if (wide) { assert(buffer[8] == 0 && buffer[9] == 'A'); assert(buffer[12] == 0 && buffer[13] == 0); }
            else { assert(buffer[8 + chars] == 0); if(chars) assert(buffer[8] == 'A'); }
        }
    }
    std::array<uint8_t, 40> bytes; bytes.fill(0xcc);
    for (uint32_t user : {1U, 3U, 4U, UINT32_MAX}) {
        assert(write_user_name(user, "Name", 16, bytes, true) == (user < 4 ? IdentityStatus::no_such_user : IdentityStatus::invalid_argument));
        for (auto b : bytes) assert(b == 0xcc);
    }
    assert(write_user_name(0, "Name", 16, std::span(bytes).first(31), true) == IdentityStatus::invalid_argument);
    for (auto b : bytes) assert(b == 0xcc);
    assert(write_user_name(0, "12345678901234567890", 40, bytes, false) == IdentityStatus::success);
    assert(bytes[14] == '5' && bytes[15] == 0 && bytes[16] == 0xcc);
    bytes.fill(0xcc);
    assert(write_user_name(0, "", 16, bytes, true) == IdentityStatus::success);
    assert(bytes[0] == 0 && bytes[1] == 'U' && bytes[8] == 0 && bytes[9] == 0 && bytes[10] == 0xcc);
}
