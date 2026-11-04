// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace exbox::editor {
struct MessagePart {
    bool insertion = false;
    uint32_t argument = 0; // Zero-based guest vararg index; valid range 0..98.
    std::u16string text;  // Literal UTF16 or the insert's format specification.
};
inline std::vector<MessagePart> parse_message_template(std::u16string_view text) {
    std::vector<MessagePart> parts;
    auto literal = [&](std::u16string_view value) {
        if (value.empty()) return;
        if (parts.empty() || parts.back().insertion) parts.push_back({});
        parts.back().text.append(value);
    };
    size_t cursor = 0;
    while (cursor < text.size()) {
        const size_t escape = text.find(u'%', cursor);
        if (escape == text.npos) { literal(text.substr(cursor)); break; }
        literal(text.substr(cursor, escape - cursor));
        cursor = escape + 1;
        if (cursor == text.size()) break;
        const char16_t marker = text[cursor++];
        if (marker == u'0') break;
        if (marker >= u'1' && marker <= u'9') {
            uint32_t number = uint32_t(marker - u'0');
            if (cursor < text.size() && text[cursor] >= u'0' && text[cursor] <= u'9')
                number = number * 10 + uint32_t(text[cursor++] - u'0');
            std::u16string spec = u"s";
            if (cursor < text.size() && text[cursor] == u'!') {
                const size_t close = text.find(u'!', cursor + 1);
                if (close != text.npos) {
                    spec = text.substr(cursor + 1, close - cursor - 1);
                    cursor = close + 1;
                }
            }
            parts.push_back({true, number - 1, std::move(spec)});
            continue;
        }
        switch (marker) {
            case u'%': literal(u"%"); break;
            case u'!': literal(u"!"); break;
            case u'n': literal(u"\r\n"); break;
            case u'r': literal(u"\r"); break;
            case u't': literal(u"\t"); break;
            default: literal(text.substr(escape, 2)); break;
        }
    }
    return parts;
}
}
