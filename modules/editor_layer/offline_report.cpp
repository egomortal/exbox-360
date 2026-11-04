// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
#include <cstring>
namespace exbox::editor {
u32 local_report_storage() {
    // Both export names share one process-lifetime guest buffer. This is inert
    // offline storage, not a telemetry sender. Preserve writes on repeat calls.
    // A failed allocation leaves zero so a later call can retry.
    static uint32_t address = 0;
    if (!address) {
        auto* memory = rex::system::kernel_state()->memory();
        address = memory->SystemHeapAlloc(1024);
        if (address) std::memset(memory->TranslateVirtual<uint8_t*>(address), 0, 1024);
    }
    return address;
}
}
REX_HOOK(__wrap___imp__XamWebInstrumentationCreateReport, exbox::editor::local_report_storage)
REX_HOOK(__wrap___imp__XamWebInstrumentationCreateSampledReport, exbox::editor::local_report_storage)
