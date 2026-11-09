// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/string.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xtypes.h>
#include <array>
#include <vector>
#include <string>
#include <algorithm>
namespace exbox::editor {
using rex::X_RESULT;
struct Achievement {uint32_t id,image,score,flags;std::array<std::u16string,3> text;};
class AchievementPages final:public rex::system::XEnumerator {
public:
 AchievementPages(rex::system::KernelState* k,uint32_t count,uint32_t flags):XEnumerator(k,count,36+((flags&7)?464:0)),flags_(flags){}
 std::vector<Achievement> entries;
 uint32_t WriteItems(uint32_t address,uint8_t* out,uint32_t* written) override {
    auto count=std::min(entries.size()-cursor_,items_per_enumerate());if(!count)return X_ERROR_NO_MORE_FILES;
    size_t strings=items_per_enumerate()*36,remaining=count*464;
    auto word=[&](size_t offset,uint32_t v){out[offset]=v>>24;out[offset+1]=v>>16;out[offset+2]=v>>8;out[offset+3]=v;};
    for(size_t i=0;i<count;++i){const auto& e=entries[cursor_++];size_t row=i*36;word(row,e.id);
        for(size_t field=0;field<3;++field){
            if(!(flags_&(1u<<field))){word(row+4+field*4,0);continue;}
            const auto& text=e.text[field];size_t bytes=(text.size()+1)*2;
            if(bytes>remaining)return X_ERROR_INSUFFICIENT_BUFFER; // Defensive, outside valid-source parity.
            word(row+4+field*4,address+uint32_t(strings));
            for(char16_t c:text){out[strings++]=c>>8;out[strings++]=c;}
            out[strings++]=0;out[strings++]=0;remaining-=bytes;
        }
        word(row+16,e.image);word(row+20,e.score);word(row+24,0);word(row+28,0);word(row+32,e.flags);
    }
    if(written)*written=uint32_t(count);return X_ERROR_SUCCESS;
 }
private:uint32_t flags_;size_t cursor_=0;
};
u32 enumerate_achievements(u32,u32 user,u32,u32 flags,u32,u32 count,mapped_u32 size,mapped_u32 handle){
 if(!count||!size||!handle||user>=4)return X_ERROR_INVALID_PARAMETER;
 *size=uint32_t(36+((flags&7)?464:0))*uint32_t(count);
 auto* k=rex::system::kernel_state();auto e=rex::system::make_object<AchievementPages>(k,count,flags);
 auto status=e->Initialize(user,0xfb,0xb000a,0xb000b,0);if(XFAILED(status))return status;
 const auto db=k->title_xdbf();if(db.is_valid()){
    auto language=db.GetExistingLanguage(static_cast<rex::system::XLanguage>(rex::cvar::Query<uint32_t>("user_language")));
    for(const auto& source:db.GetAchievements()){
        Achievement a{source.id,source.image_id,source.gamerscore,source.flags,{}};
        a.text[0]=rex::string::to_utf16(db.GetStringTableEntry(language,source.label_id));
        a.text[1]=rex::string::to_utf16(db.GetStringTableEntry(language,source.description_id));
        a.text[2]=rex::string::to_utf16(db.GetStringTableEntry(language,source.unachieved_id));
        e->entries.push_back(std::move(a));
    }
 }
 *handle=e->handle();return X_ERROR_SUCCESS;
}
}
REX_HOOK(__wrap___imp__XamUserCreateAchievementEnumerator,exbox::editor::enumerate_achievements)
