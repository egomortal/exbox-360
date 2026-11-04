// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace exbox::editor {
// Defensive, never observed in the natural traces. This is deliberately separate
// from ownership-status policy and does not modify any guest register or memory.
using GuardLog = void (*)(const char*);
inline bool null_operation_guard(uint32_t guest_object, GuardLog log) {
    if (guest_object) return false;
    if (log) log("editor: defensive null operation guard");
    return true;
}
}
