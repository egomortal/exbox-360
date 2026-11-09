// SPDX-License-Identifier: GPL-3.0-or-later
#include "award_record.hpp"
#include "../../private/av2/title-port-source/kernel/xam/avatars/closet.h"
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xtypes.h>
namespace exbox::editor {
using rex::X_RESULT;
u32 enumerate_awards(u32 user,u32 title,u32,u32,u32,u32 count,mapped_u32 size,mapped_u32 handle){
    if(!handle||!count||user>=4){if(size)*size=0;return X_ERROR_INVALID_PARAMETER;}
    if(size)*size=uint32_t(52)*uint32_t(count);
    auto* kernel=rex::system::kernel_state();
    auto enumerator=rex::system::make_object<rex::system::XStaticEnumerator<AwardRecord>>(kernel,count);
    const auto status=enumerator->Initialize(user,0xfb,0x20005,0x20007,0);
    if(XFAILED(status))return status;
    for(const auto& item:rex::avatars::GetCloset().items()){
        const auto* bytes=reinterpret_cast<const uint8_t*>(&item.id);
        if(auto record=award_record(std::span<const uint8_t,16>(bytes,16),title))
            *enumerator->AppendItem()=*record;
    }
    *handle=enumerator->handle();return X_ERROR_SUCCESS;
}
}
REX_HOOK(__wrap___imp__XamUserCreateAvatarAssetEnumerator,exbox::editor::enumerate_awards)
