// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace exbox::editor {
enum class AccessStatus { success, invalid_argument, no_such_user };
struct AccessReply { AccessStatus status; bool write; uint32_t first; uint32_t second; };
// Pure offline compatibility policy. No network, account or profile mutation.
inline AccessStatus local_user(uint32_t user) {
    if (user >= 4) return AccessStatus::invalid_argument;
    return user ? AccessStatus::no_such_user : AccessStatus::success;
}
inline AccessReply privilege(uint32_t user) {
    const auto status = user == 255 ? AccessStatus::success : local_user(user);
    return {status, status == AccessStatus::success, 1, 0};
}
inline AccessReply restriction(uint32_t user, bool rating) {
    const auto status = user ? AccessStatus::no_such_user : AccessStatus::success;
    return {status, status == AccessStatus::success, rating ? 63U : 0U, 0};
}
inline AccessReply access_check() { return {AccessStatus::success, true, 1, 0}; }
inline uint32_t online_compatibility() { return 1; }
}
