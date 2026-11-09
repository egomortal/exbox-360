// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
namespace exbox::editor {
namespace {
u32 finish_local_overlay() {
    // Preserve the guest's open/close event contract without creating a store,
    // performing a purchase, or connecting to any service.
    auto* kernel = rex::system::kernel_state();
    kernel->BroadcastNotification(9, 1);
    kernel->BroadcastNotification(9, 0);
    return 0;
}
}
u32 local_marketplace(u32, u32, u64, u32, u32, u32) { return finish_local_overlay(); }
u32 local_nui_marketplace(u32, u32, u32, u64, u32, u32, u32) { return finish_local_overlay(); }
}
REX_HOOK(__wrap___imp__XamShowMarketplaceUI, exbox::editor::local_marketplace)
REX_HOOK(__wrap___imp__XamShowNuiMarketplaceUI, exbox::editor::local_nui_marketplace)
