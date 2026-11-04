// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <rex/ppc/context.h>
namespace exbox::editor {
// Return true only when the caller should bypass the guest implementation.
bool marketplace_ownership_status(PPCRegister& object);
// Installed closet membership; preserve unrelated resource-manager objects.
bool marketplace_ownership_contains(PPCRegister& object, PPCRegister& asset);
bool marketplace_local_access(PPCRegister& result);
// Retain the original guest-to-host ninth-argument stack convention.
void marketplace_rating_argument(PPCRegister& stack);
}
