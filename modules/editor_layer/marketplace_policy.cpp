// SPDX-License-Identifier: GPL-3.0-or-later
#include "marketplace_policy.hpp"
#include <rex/system/kernel_state.h>
#include <rex/types.h>
#include "kernel/xam/avatars/closet.h"
namespace exbox::editor {
bool marketplace_ownership_status(PPCRegister& object) {
    // This resource-manager class also serves unrelated objects. Only the
    // marketplace history instance can be reported complete by this hook.
    if (object.u32 != 0x945065F0u) return false;
    object.u32 = 0;
    return true;
}
bool marketplace_ownership_contains(PPCRegister& object, PPCRegister& asset) {
    if (object.u32 != 0x945065F0u) return false;
    const auto* id = rex::system::kernel_memory()->TranslateVirtual<const rex::avatars::AssetId*>(asset.u32);
    object.u32 = id && rex::avatars::GetCloset().Find(*id) ? 1u : 0u;
    return true;
}
bool marketplace_local_access(PPCRegister& result) {
    result.u32 = 0;
    return true;
}
void marketplace_rating_argument(PPCRegister& stack) {
    auto* memory = rex::system::kernel_memory();
    const auto* source = memory->TranslateVirtual<const rex::be<uint32_t>*>(stack.u32 + 0x54u);
    auto* destination = memory->TranslateVirtual<rex::be<uint32_t>*>(stack.u32 + 0x58u);
    // Match the pinned source's null-translation sentinel and write guard.
    const uint32_t value = source ? source->get() : 0xDEADBEEFu;
    if (destination) *destination = value;
}
}
