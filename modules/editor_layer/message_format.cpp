// SPDX-License-Identifier: GPL-3.0-or-later
#include "message_values.hpp"
#include "xui_memory.hpp"
#include <rex/ppc/context.h>
#include <cstdio>
namespace exbox::editor {
namespace {
std::u16string guest_text(uint32_t address, bool narrow, bool& valid) {
    std::u16string text;
    const size_t stride = narrow ? 1 : 2;
    for (size_t units = 0; units < 2048;) {
        const uint64_t at = uint64_t(address) + units * stride;
        if (at > UINT32_MAX) { valid = false; return {}; }
        // Read within page boundaries, stopping at NUL without requiring the
        // following page. An unaligned wide unit can span two validated pages.
        const size_t page_units = std::max(size_t{1}, (4096 - size_t(at & 4095)) / stride);
        const size_t chunk = std::min(size_t{2048} - units, page_units);
        const auto bytes = XuiMemory::range(uint32_t(at), chunk * stride, rex::memory::kMemoryProtectRead);
        if (bytes.empty()) { valid = false; return {}; }
        for (size_t i = 0; i < chunk; ++i) {
            const char16_t value = narrow ? bytes[i] : (uint16_t(bytes[2*i]) << 8) | bytes[2*i+1];
            if (!value) return text;
            text.push_back(value);
        }
        units += chunk;
    }
    return text;
}
bool writable(uint32_t address, size_t count) {
    if (uint64_t(address) + count > uint64_t(UINT32_MAX) + 1) return false;
    for (size_t offset = 0; offset < count;) {
        const uint32_t at = address + uint32_t(offset);
        const size_t chunk = std::min(count-offset, size_t{4096} - size_t(at & 4095));
        if (XuiMemory::range(at, chunk, rex::memory::kMemoryProtectWrite).empty()) return false;
        offset += chunk;
    }
    return true;
}
}
void format_message(PPCContext& context, uint8_t* base) {
    const uint32_t output = context.r3.u32, capacity = context.r4.u32, format = context.r5.u32;
    context.r3.u64 = 0;
    if (!output || !capacity || !format) return;
    bool valid = true;
    auto input = guest_text(format, false, valid);
    if (!valid) { std::fprintf(stderr, "Q13 FORMAT REJECT template memory\n"); return; }
    const std::array<uint32_t,5> registers{context.r6.u32,context.r7.u32,context.r8.u32,context.r9.u32,context.r10.u32};
    auto argument = [&](uint32_t index) {
        if (index < registers.size()) return registers[index];
        const uint64_t at = uint64_t(context.r1.u32) + 0x54 + (index-5)*8;
        if (at > UINT32_MAX) { valid = false; return uint32_t{0}; }
        auto value = XuiMemory::word(uint32_t(at));
        if (!value) { valid = false; return uint32_t{0}; }
        return *value;
    };
    auto read = [&](uint32_t address, bool narrow) { return guest_text(address,narrow,valid); };
    auto expanded = expand_message(input,argument,read);
    if (!valid || !expanded) { std::fprintf(stderr, "Q13 FORMAT REJECT argument or unsupported spec\n"); return; }
    const size_t units = std::min(expanded->size(),size_t(capacity-1)) + 1;
    if (!writable(output, units*2)) { std::fprintf(stderr, "Q13 FORMAT REJECT output memory\n"); return; }
    // Only validate/write through the terminating NUL; caller capacity beyond
    // that must stay untouched, including buffers ending at a mapped page.
    const auto count = write_message(*expanded,uint32_t(units),{base+output,units*2});
    if (count) context.r3.u64 = *count;
}
}
extern "C" void __wrap___imp__XamFormatMessage(PPCContext& context,uint8_t* base) {
    exbox::editor::format_message(context,base);
}
