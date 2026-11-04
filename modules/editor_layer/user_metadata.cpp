// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/cvar.h>
#include <rex/hook.h>
namespace exbox::editor {
u32 metadata_country(u32) { return uint8_t(rex::cvar::Query<uint32_t>("user_country")); }
u32 metadata_language(u32) { return uint8_t(rex::cvar::Query<uint32_t>("user_language")); }
u32 metadata_allowed(u32, u32, mapped_u32 output) { if (output) *output = 1; return 0; }
u32 metadata_sync(u32) { return 0; }
u32 metadata_zero() { return 0; }
}
REX_HOOK(__wrap___imp__XamUserGetOnlineCountryFromXUID, exbox::editor::metadata_country)
REX_HOOK(__wrap___imp__XamUserGetOnlineLanguageFromXUID, exbox::editor::metadata_language)
REX_HOOK(__wrap___imp__XamUserIsUnsafeProgrammingAllowed, exbox::editor::metadata_allowed)
REX_HOOK(__wrap___imp__XamUserProfileSync, exbox::editor::metadata_sync)
REX_HOOK(__wrap___imp__XamIsNuiAutomationEnabled, exbox::editor::metadata_zero)
REX_HOOK(__wrap___imp__XamNuiIsDeviceReady, exbox::editor::metadata_zero)
REX_HOOK(__wrap___imp__XamGetDashContext, exbox::editor::metadata_zero)
