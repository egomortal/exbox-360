// SPDX-License-Identifier: GPL-3.0-or-later
#include "user_access.hpp"
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
namespace exbox::editor {
using rex::X_RESULT;
namespace {
u32 access_error(AccessStatus status) {
    switch (status) {
        case AccessStatus::success: return X_ERROR_SUCCESS;
        case AccessStatus::invalid_argument: return X_ERROR_INVALID_PARAMETER;
        case AccessStatus::no_such_user: return X_ERROR_NO_SUCH_USER;
    }
    return X_ERROR_INVALID_PARAMETER;
}
u32 write_access(AccessReply reply, mapped_u32 out) {
    if (!reply.write) return access_error(reply.status);
    if (!out) return X_ERROR_INVALID_PARAMETER;
    *out = reply.first;
    return X_ERROR_SUCCESS;
}
}
u32 user_privilege(u32 user, u32, mapped_u32 out) { return write_access(privilege(user), out); }
u32 restriction_flags(u32 user, mapped_u32 out) { return write_access(restriction(user, false), out); }
u32 restriction_rating(u32 user, u32, mapped_u32 first, mapped_u32 second) {
    const auto reply = restriction(user, true);
    if (!reply.write) return access_error(reply.status);
    // Validate both before writing either, including the defensive null case.
    if (!first || !second) return X_ERROR_INVALID_PARAMETER;
    *first = reply.first; *second = reply.second;
    return X_ERROR_SUCCESS;
}
u32 restriction_access(u32, u32, u32, u32, u32, mapped_u32 out, u32 overlapped) {
    const auto result = write_access(access_check(), out);
    if (result != X_ERROR_SUCCESS) return result;
    if (overlapped) rex::system::kernel_state()->CompleteOverlappedImmediate(overlapped, X_ERROR_SUCCESS);
    return X_ERROR_SUCCESS;
}
u32 user_online(u32) { return online_compatibility(); }
u32 user_membership(u32 user) {
    const auto status = local_user(user);
    return status == AccessStatus::success ? 6U : access_error(status);
}
}
REX_HOOK(__wrap___imp__XamUserCheckPrivilege, exbox::editor::user_privilege)
REX_HOOK(__wrap___imp__XamUserContentRestrictionGetFlags, exbox::editor::restriction_flags)
REX_HOOK(__wrap___imp__XamUserContentRestrictionGetRating, exbox::editor::restriction_rating)
REX_HOOK(__wrap___imp__XamUserContentRestrictionCheckAccess, exbox::editor::restriction_access)
REX_HOOK(__wrap___imp__XamUserIsOnlineEnabled, exbox::editor::user_online)
REX_HOOK(__wrap___imp__XamUserGetMembershipTier, exbox::editor::user_membership)
