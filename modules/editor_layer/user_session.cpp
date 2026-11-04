// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
namespace exbox::editor {
using rex::X_RESULT;
// Compatibility sentinels only. Never dereference or treat these as SDK handles.
constexpr uint32_t session_token = 0xcafedead;
constexpr uint32_t session_object_token = 0xdeadf00d;
u32 session_create(mapped_u32 output) {
    if (!output) return X_ERROR_INVALID_PARAMETER;
    *output = session_token; return X_ERROR_SUCCESS;
}
u32 session_reference(u32 token, mapped_u32 output) {
    if (token != session_token || !output) return X_ERROR_INVALID_PARAMETER;
    *output = session_object_token; return X_ERROR_SUCCESS;
}
u32 manifest_acknowledge(u32, mapped_void, mapped_void overlap) {
    // Matches the offline reference's acknowledgement; does not inspect bytes.
    if (!overlap) return X_ERROR_SUCCESS;
    rex::system::kernel_state()->CompleteOverlappedImmediate(overlap.guest_address(), X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
}
}
REX_HOOK(__wrap___imp__XamSessionCreateHandle, exbox::editor::session_create)
REX_HOOK(__wrap___imp__XamSessionRefObjByHandle, exbox::editor::session_reference)
REX_HOOK(__wrap___imp__XamUserValidateAvatarManifest, exbox::editor::manifest_acknowledge)
