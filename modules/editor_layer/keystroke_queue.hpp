// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <deque>
#include <optional>
namespace exbox::editor {
struct Stroke { uint16_t key, flags; };
// Caller serializes access and supplies monotonic milliseconds. Time injection
// makes repeats deterministic in tests without sleeping or querying guest time.
class KeystrokeQueue {
    struct Hold { uint16_t pad; uint32_t source; uint64_t began, repeated; };
    std::deque<Stroke> pending_;
    std::deque<Hold> holds_;
public:
    void press(uint16_t pad,uint32_t source,uint64_t now) {
        for(const auto& hold:holds_)if(hold.pad==pad)return;
        holds_.push_back({pad,source,now,now});pending_.push_back({pad,1});
    }
    void release(uint32_t source) {
        for(auto it=holds_.begin();it!=holds_.end();) {
            if(it->source!=source){++it;continue;}
            pending_.push_back({it->pad,2});it=holds_.erase(it);
        }
    }
    std::optional<Stroke> poll(uint64_t now) {
        if(!pending_.empty()) {
            const auto event=pending_.front();pending_.pop_front();return event;
        }
        for(auto& hold:holds_) {
            if(now<hold.began || now<hold.repeated)continue;
            if(now-hold.began<400 || now-hold.repeated<100)continue;
            hold.repeated=now;return Stroke{hold.pad,5};
        }
        return std::nullopt;
    }
};
}
