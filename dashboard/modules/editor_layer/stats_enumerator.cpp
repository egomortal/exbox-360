// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xtypes.h>
#include <array>
#include <cstring>
#include <limits>
namespace exbox::editor {
using rex::X_RESULT;
struct StatsResult {rex::be<uint32_t> views,address;};
struct StatsView {rex::be<uint32_t> id,total,count,rows;};
static_assert(sizeof(StatsResult)==8&&sizeof(StatsView)==16);
u32 enumerate_empty_stats(u32,u32 type,u64 pivot,u32 rows,u32 count,mapped_void specs,mapped_u32 size,mapped_u32 handle){
    if(!handle)return X_ERROR_INVALID_PARAMETER;
    *handle=0;
    if(!size)return X_ERROR_INVALID_PARAMETER;
    *size=0;
    if(!pivot||!specs||!rows||rows>100||!count||type>3)return X_ERROR_INVALID_PARAMETER;
    // Overflow and allocation failure are defensive extensions, not parity claims.
    if(count>(std::numeric_limits<uint32_t>::max()-8)/16)return X_ERROR_INVALID_PARAMETER;
    auto* k=rex::system::kernel_state();auto* m=k->memory();
    uint32_t bytes=uint32_t(count)*16,base=m->SystemHeapAlloc(bytes);
    if(!base)return 8;
    auto* views=m->TranslateVirtual<StatsView*>(base);std::memset(views,0,bytes);
    const auto* input=specs.as<const uint8_t*>();
    for(uint32_t i=0;i<count;++i){
        std::memcpy(&views[i].id,input+size_t(i)*136,4);
        const auto row=m->SystemHeapAlloc(48);if(!row)return 8;
        std::memset(m->TranslateVirtual<uint8_t*>(row),0,48);views[i].rows=row;
    }
    auto e=rex::system::make_object<rex::system::XStaticEnumerator<StatsResult>>(k,1);
    const auto status=e->Initialize(0xff,0xfb,0xb0023,0xb0024,0);if(XFAILED(status))return status;
    auto* result=e->AppendItem();result->views=count;result->address=base;
    *size=8+bytes;*handle=e->handle();return X_ERROR_SUCCESS;
}
}
REX_HOOK(__wrap___imp__XamUserCreateStatsEnumerator,exbox::editor::enumerate_empty_stats)
