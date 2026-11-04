// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <rex/ppc/context.h>
namespace exbox::editor {
// Both adapters must share this cache when runtime binding is enabled.
void marketplace_remember_fetcher(PPCRegister& fetcher, PPCRegister& name);
void marketplace_reset_fetchers(const char* prefix);
}
