// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace exbox_services::promotions {

enum class Mode { Off, Authentic, Local };

struct Candidate {
    std::string stable_id;
    bool owner_local=false;
    bool authentic=false;
    std::uint32_t era_mask=0;
    std::uint32_t page_mask=0;
    std::string game_id;
    std::int64_t begin_utc=0;
    std::int64_t end_utc=0;
    int priority=0;
};

struct Context {
    int era=0;
    int page=0;
    std::int64_t now_utc=0;
    std::string current_game_id;
    std::vector<std::string> recent_game_ids;
    std::uint64_t rotation_generation=0;
};

struct Pick {
    int index=-1;
    std::string stable_id;
    bool personalized=false;
    explicit operator bool() const { return index>=0; }
};

inline std::uint64_t stable_hash(const std::string& text) {
    std::uint64_t h=1469598103934665603ull;
    for(unsigned char c:text){h^=c;h*=1099511628211ull;}
    return h;
}

inline bool eligible(const Candidate& candidate,Mode mode,const Context& context) {
    if(mode==Mode::Off||!candidate.owner_local||candidate.stable_id.empty()||candidate.stable_id.size()>128||candidate.game_id.size()>128)return false;
    if(context.era<0||context.era>=32||context.page<0||context.page>=32)return false;
    if(!(candidate.era_mask&(1u<<unsigned(context.era)))||!(candidate.page_mask&(1u<<unsigned(context.page))))return false;
    if(candidate.begin_utc&&candidate.end_utc&&candidate.begin_utc>candidate.end_utc)return false;
    if(candidate.begin_utc&&context.now_utc<candidate.begin_utc)return false;
    if(candidate.end_utc&&context.now_utc>candidate.end_utc)return false;
    return mode!=Mode::Authentic||candidate.authentic;
}

// Selection is metadata-only. The existing verified promotion loader remains
// solely responsible for resolving owner-local artwork.
inline Pick choose(const std::vector<Candidate>& candidates,Mode mode,const Context& context,
                   const std::string& previous_id={}) {
    if(candidates.size()>256||context.recent_game_ids.size()>32||context.current_game_id.size()>128)return {};
    struct Scored { int index;std::int64_t score;std::uint64_t tie; };
    std::vector<Scored> scored;scored.reserve(candidates.size());
    for(std::size_t i=0;i<candidates.size();++i){const auto&candidate=candidates[i];if(!eligible(candidate,mode,context))continue;
        std::int64_t score=std::clamp(candidate.priority,-1000,1000)*1000ll;
        if(mode==Mode::Local&&!candidate.game_id.empty()){
            if(candidate.game_id==context.current_game_id)score+=500000;
            else {const auto found=std::find(context.recent_game_ids.begin(),context.recent_game_ids.end(),candidate.game_id);
                if(found!=context.recent_game_ids.end())score+=200000-1000*std::distance(context.recent_game_ids.begin(),found);}
        }
        const std::uint64_t tie=stable_hash(candidate.stable_id)^((context.rotation_generation+1)*0x9e3779b97f4a7c15ull);
        score+=std::int64_t(tie&0xffffu);if(candidate.authentic)score+=100;
        scored.push_back({int(i),score,tie});
    }
    if(scored.empty())return{};
    const bool alternative=std::any_of(scored.begin(),scored.end(),[&](const auto&row){return candidates[std::size_t(row.index)].stable_id!=previous_id;});
    if(alternative)scored.erase(std::remove_if(scored.begin(),scored.end(),[&](const auto&row){return candidates[std::size_t(row.index)].stable_id==previous_id;}),scored.end());
    const auto best=std::max_element(scored.begin(),scored.end(),[](const auto&a,const auto&b){return a.score==b.score?a.tie<b.tie:a.score<b.score;});
    const auto&candidate=candidates[std::size_t(best->index)];
    const bool personalized=mode==Mode::Local&&!candidate.game_id.empty()&&
        (candidate.game_id==context.current_game_id||std::find(context.recent_game_ids.begin(),context.recent_game_ids.end(),candidate.game_id)!=context.recent_game_ids.end());
    return {best->index,candidate.stable_id,personalized};
}

} // namespace exbox_services::promotions
