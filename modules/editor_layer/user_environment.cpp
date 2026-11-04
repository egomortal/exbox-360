// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/cvar.h>
// Preserve existing configuration keys and defaults; online behavior is disabled.
REXCVAR_DEFINE_STRING(user_name,"User","Kernel","Local display name, maximum15 characters.");
REXCVAR_DEFINE_STRING(user_gamerpic,"","Kernel","Optional local gamer-picture path.");
REXCVAR_DEFINE_BOOL(online_mode,false,"Kernel","Legacy compatibility setting; ignored offline.");
REXCVAR_DEFINE_STRING(avatar_marketplace_key,"","Kernel","Legacy compatibility value; no service is enabled.");
REXCVAR_DEFINE_STRING(avatar_marketplace_server,"http://127.0.0.1:8080","Kernel","Legacy endpoint parsed for compatibility; no connection is made here.");
bool OnlineMode(){return false;}
