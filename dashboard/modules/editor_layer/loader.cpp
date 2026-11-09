// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
// The observed host contract terminates the current guest title for every
// requested path. It does not launch a second title or return a guest result.
// Save finalization and our return ticket remain the frontend's responsibility.
extern "C" void __imp__XamLoaderLaunchTitleEx(PPCContext&,uint8_t*) {
    rex::system::kernel_state()->TerminateTitle();
}
