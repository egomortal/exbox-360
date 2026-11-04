// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
namespace exbox::editor {
// Behavioral contract: identify the uniquely longest live sequence of per-frame
// heading setters, then recognize identical heading text on a replacement handle.
// Text is supplied already bounded/decoded by the memory-facing adapter.
class HeadingTracker {
    struct Observation {uint32_t handle=0,frame=0,streak=0;};
    std::array<Observation,8> observations_{};
    std::string heading_;
public:
    bool observe(uint32_t handle,uint32_t frame,std::string_view text) {
        if(!handle)return false;
        auto entry=std::find_if(observations_.begin(),observations_.end(),
            [handle](const auto& item){return item.handle==handle;});
        if(entry==observations_.end()) {
            entry=std::min_element(observations_.begin(),observations_.end(),
                [](const auto& a,const auto& b){return a.frame<b.frame;});
            *entry={handle,frame,0};
        } else if(uint32_t(entry->frame+1)==frame) {
            ++entry->streak;entry->frame=frame;
        } else if(entry->frame!=frame) {
            entry->streak=0;entry->frame=frame;
        }
        uint32_t longest=0;unsigned winners=0;
        for(const auto& item:observations_) {
            if(!item.handle || uint32_t(item.frame+1)<frame)continue;
            if(item.streak>longest){longest=item.streak;winners=1;}
            else if(item.streak==longest)++winners;
        }
        const bool leader=entry->streak>=2 && entry->streak>=longest && winners==1;
        if(leader && !text.empty())heading_.assign(text);
        return leader || (!heading_.empty() && text==heading_);
    }
};
} // namespace exbox::editor
