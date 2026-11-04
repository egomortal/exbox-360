// SPDX-License-Identifier: GPL-3.0-or-later
#include "patches_policy.hpp"
#include "guards.hpp"
#include "gpu_edge_runtime.h"
#include <rex/system/kernel_state.h>
#include <rex/kernel/xam/tile_icon.h>
namespace exbox::editor {
bool patch_null_operation(PPCRegister& object) {
    return null_operation_guard(object.u32, nullptr);
}
void patch_tile_buffer(PPCRegister& size) {
    if (size.u32 == 0x4000u) size.u32 = 0x10000u;
}
void patch_tile_hint(PPCRegister& label) {
    const auto* text = rex::system::kernel_memory()->TranslateVirtual<const uint8_t*>(label.u32 - 36u);
    if (!text) return;
    constexpr char preview[] = "PREVIEW";
    uint32_t pixels = 128u;
    // Guest labels use UTF-16BE. Only the first seven exact ASCII code units
    // select the larger preview; suffixes do not affect the pinned behavior.
    for (unsigned i = 0; i < 7; ++i) {
        if (text[2*i] || text[2*i+1] != uint8_t(preview[i])) {
            pixels = 64u;
            break;
        }
    }
    rex::kernel::xam::SetTileSizeHint(pixels);
    av2::active_gpu_edge().tile_size(pixels);
}
}
