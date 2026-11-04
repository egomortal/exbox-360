// SPDX-License-Identifier: GPL-3.0-or-later
#include "user_identity.hpp"
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xtypes.h>
namespace exbox::editor {
using rex::X_RESULT;
using rex::X_HRESULT;
uint32_t identity_hresult(IdentityStatus value) {
    switch(value) {
        case IdentityStatus::success:return X_E_SUCCESS;
        case IdentityStatus::invalid_argument:return X_E_INVALIDARG;
        case IdentityStatus::no_such_user:return X_E_NO_SUCH_USER;
    }
    return X_E_INVALIDARG;
}
i32 user_xuid(u32 user,u32 mask,mapped_u64 output) {
    if(!output)return static_cast<i32>(X_E_INVALIDARG);
    // Original title uses only these masks. Invalid masks are a defensive error
    // here rather than the reference's host assertion; no valid case changes.
    if(mask!=1 && mask!=2 && mask!=3 && mask!=4 && mask!=7)return static_cast<i32>(X_E_INVALIDARG);
    const auto& profile=rex::system::kernel_state()->user_profile();
    const auto result=identity(user,mask,profile->type(),profile->xuid(),true);
    *output=result.xuid;
    return static_cast<i32>(identity_hresult(result.status));
}
u32 user_signin(u32 user){return signin_state(user);}
i32 user_info(u32 user,u32,mapped_void output) {
    if(!output)return static_cast<i32>(X_E_INVALIDARG);
    std::string configured;
    if(rex::cvar::GetFlagInfo("user_name"))configured=rex::cvar::Query<std::string>("user_name");
    const auto result=signin_info(user,configured,{static_cast<uint8_t*>(output.host_address()),40});
    return static_cast<i32>(identity_hresult(result));
}
}
// SDK typed thunks preserve each export's signed versus unsigned return ABI.
REX_HOOK(__wrap___imp__XamUserGetXUID,exbox::editor::user_xuid)
REX_HOOK(__wrap___imp__XamUserGetSigninState,exbox::editor::user_signin)
REX_HOOK(__wrap___imp__XamUserGetSigninInfo,exbox::editor::user_info)
