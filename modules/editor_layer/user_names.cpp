// SPDX-License-Identifier: GPL-3.0-or-later
#include "user_names.hpp"
#include <rex/cvar.h>
#include <rex/hook.h>
namespace exbox::editor {
uint32_t identity_hresult(IdentityStatus);
namespace {
u32 name_result(u32 user, mapped_void output, u32 capacity, bool wide) {
    std::string configured;
    if (rex::cvar::GetFlagInfo("user_name")) configured = rex::cvar::Query<std::string>("user_name");
    const size_t bytes = output ? size_t(std::min(capacity, uint32_t{16})) * (wide ? 2 : 1) : 0;
    const auto result = write_user_name(user, configured, capacity,
        {static_cast<uint8_t*>(output.host_address()), bytes}, wide);
    return identity_hresult(result);
}
}
// These two exports return unsigned words (unlike the signed XUID/info exports).
u32 user_name(u32 user, mapped_void output, u32 capacity) { return name_result(user, output, capacity, false); }
u32 user_gamertag(u32 user, mapped_void output, u32 capacity) { return name_result(user, output, capacity, true); }
}
REX_HOOK(__wrap___imp__XamUserGetName, exbox::editor::user_name)
REX_HOOK(__wrap___imp__XamUserGetGamerTag, exbox::editor::user_gamertag)
