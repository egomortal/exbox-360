// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "message_template.hpp"
#include <array>
#include <cstdio>
#include <optional>
#include <span>
namespace exbox::editor {
inline bool supported_integer_spec(std::string_view spec) {
    size_t at = 0;
    while (at < spec.size() && std::string_view("-+ #0").find(spec[at]) != std::string_view::npos) ++at;
    auto decimal = [&]() {
        unsigned value = 0;
        while (at < spec.size() && spec[at] >= '0' && spec[at] <= '9') {
            value = value * 10 + unsigned(spec[at++] - '0');
            if (value > 4096) return false;
        }
        return true;
    };
    if (!decimal()) return false;
    if (at < spec.size() && spec[at] == '.') { ++at; if (!decimal()) return false; }
    if (at < spec.size() && spec[at] == 'h') { ++at; if (at < spec.size() && spec[at] == 'h') ++at; }
    return at + 1 == spec.size() && std::string_view("diouxX").find(spec[at]) != std::string_view::npos;
}
inline std::optional<std::u16string> message_integer(std::string_view spec, uint32_t value) {
    if (!supported_integer_spec(spec)) return std::nullopt;
    const std::string format = "%" + std::string(spec);
    std::array<char, 64> scratch{};
    const char kind = spec.back();
    const int result = kind == 'd' || kind == 'i'
        ? std::snprintf(scratch.data(), scratch.size(), format.c_str(), static_cast<int32_t>(value))
        : std::snprintf(scratch.data(), scratch.size(), format.c_str(), value);
    if (result < 0) return std::nullopt;
    std::u16string output;
    for (const unsigned char value : std::string_view(scratch.data())) output.push_back(value);
    return output;
}
// Argument and string readers are supplied by the guest binding; pure tests
// need no guest addresses. Readers must apply the original2048-unit read caps.
template<class Argument, class ReadString>
std::optional<std::u16string> expand_message(std::u16string_view input, Argument argument, ReadString read) {
    std::u16string output;
    for (const auto& part : parse_message_template(input)) {
        if (!part.insertion) { output += part.text; continue; }
        std::string spec;
        for (auto unit : part.text) spec.push_back(static_cast<char>(unit & 255));
        const char kind = spec.empty() ? 's' : spec.back();
        const uint32_t value = argument(part.argument);
        if (kind == 'c') { output.push_back(static_cast<char16_t>(value)); continue; }
        if (kind == 's' || kind == 'S') {
            if (!value) output += u"(null)";
            else output += read(value, kind == 'S' || (spec.size() > 1 && spec[spec.size()-2] == 'h'));
            continue;
        }
        auto numeric = message_integer(spec, value);
        if (!numeric) return std::nullopt;
        output += *numeric;
    }
    return output;
}
inline std::optional<size_t> write_message(std::u16string_view text, uint32_t capacity, std::span<uint8_t> output) {
    if (!capacity || capacity > output.size()/2) return std::nullopt;
    const size_t count = std::min(text.size(), size_t(capacity-1));
    for (size_t at = 0; at <= count; ++at) {
        const uint16_t value = at == count ? 0 : text[at];
        output[at*2] = uint8_t(value >> 8); output[at*2+1] = uint8_t(value);
    }
    return count;
}
}
