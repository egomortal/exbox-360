// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/hook.h>
#include <rex/system/xtypes.h>
namespace exbox::editor {
u32 compatibility_tile_key(mapped_u32, mapped_u32 first, mapped_u32 second, mapped_u32 third) {
    // These are offline compatibility tokens, not decoded game tile metadata.
    // Validate outputs before writing; invalid pointers are outside reference parity.
    if (!first || !second || !third) return 87;
    *first = 0xC0DE0001u;
    *second = 0xC0DE0002u;
    *third = 0xC0DE0003u;
    return 0;
}
}
REX_HOOK(__wrap___imp__XamParseGamerTileKey, exbox::editor::compatibility_tile_key)
