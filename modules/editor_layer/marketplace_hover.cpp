// SPDX-License-Identifier: GPL-3.0-or-later
#include "marketplace_hover.hpp"
#include <rex/system/kernel_state.h>
#include <rex/types.h>
#include <atomic>
namespace exbox::editor {
void marketplace_hover_generation(PPCRegister&) {
    // Preserve process lifetime, including consuming the first call if guest
    // translation is unavailable. Later calls must not overwrite generation.
    static std::atomic<bool> seeded{false};
    if (seeded.exchange(true)) return;
    auto* generation = rex::system::kernel_memory()->TranslateVirtual<rex::be<uint32_t>*>(0x9452E398u);
    if (generation) *generation = 1u;
}
}
