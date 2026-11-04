// SPDX-License-Identifier: GPL-3.0-or-later
#include "marketplace_fetchers.hpp"
#include <rex/system/kernel_state.h>
#include <rex/types.h>
#include <map>
#include <mutex>
#include <string>
#include <cstring>
namespace exbox::editor {
namespace {
std::mutex cache_mutex;
std::map<std::string, uint32_t> fetchers;
bool plausible(uint32_t address) {
    // Match the title's admitted address ranges, not a new memory policy.
    return (address >= 0x82000000u && address < 0xA0000000u) ||
           (address >= 0x40000000u && address < 0x60000000u);
}
}
void marketplace_remember_fetcher(PPCRegister& fetcher, PPCRegister& name) {
    if (!plausible(fetcher.u32) || !plausible(name.u32)) return;
    const char* bytes = rex::system::kernel_memory()->TranslateVirtual<const char*>(name.u32);
    std::string key;
    if (!bytes) key = "?";
    else for (size_t i = 0; i < 96 && bytes[i]; ++i) key.push_back(bytes[i]);
    std::lock_guard<std::mutex> lock(cache_mutex);
    // Repeated names replace the previous pointer, just as a reloaded page does.
    fetchers[key] = fetcher.u32;
}
void marketplace_reset_fetchers(const char* prefix) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto* memory = rex::system::kernel_memory();
    const size_t length = std::strlen(prefix);
    for (const auto& [name, address] : fetchers) {
        if (name.compare(0, length, prefix) != 0 || !plausible(address)) continue;
        auto* state = memory->TranslateVirtual<rex::be<uint32_t>*>(address + 12u);
        // State 1 is an in-flight request; resetting it would corrupt its lifecycle.
        if (state && state->get() != 1u) *state = 0;
    }
}
}
