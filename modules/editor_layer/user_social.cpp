// SPDX-License-Identifier: GPL-3.0-or-later
#include "user_access.hpp"
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
namespace exbox::editor {
using rex::X_RESULT;
using rex::X_HRESULT;
u32 user_friends(u32 user, u32, u32, mapped_u32 output, u32 overlap) {
    if (bool(output) == bool(overlap)) return X_ERROR_INVALID_PARAMETER;
    const auto status = local_user(user);
    const uint32_t result = status == AccessStatus::success ? X_ERROR_SUCCESS :
        status == AccessStatus::no_such_user ? X_ERROR_NO_SUCH_USER : X_ERROR_INVALID_PARAMETER;
    if (output) { *output = 0; return result; }
    rex::system::kernel_state()->CompleteOverlappedImmediateEx(overlap,
        result == X_ERROR_SUCCESS ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED,
        X_HRESULT_FROM_WIN32(result), 0);
    return X_ERROR_IO_PENDING;
}
u32 user_signin_ui(u32, u32) {
    auto* kernel = rex::system::kernel_state();
    // Local compatibility events only; no network connection or account UI.
    kernel->BroadcastNotification(10, 1);
    kernel->BroadcastNotification(9, 0);
    kernel->BroadcastNotification(20, 1);
    return X_ERROR_SUCCESS;
}
}
REX_HOOK(__wrap___imp__XamUserAreUsersFriends, exbox::editor::user_friends)
REX_HOOK(__wrap___imp__XamShowSigninUI, exbox::editor::user_signin_ui)
