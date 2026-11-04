// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <rex/ppc/context.h>
namespace exbox::editor {
bool patch_null_operation(PPCRegister& object);
void patch_tile_buffer(PPCRegister& size);
void patch_tile_hint(PPCRegister& label);
}
