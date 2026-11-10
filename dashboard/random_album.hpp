#pragma once
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace random_album {
// UI-thread owned. Keys use the same album identity as the library's grouping.
// A choice is recorded only after playback succeeds, not merely on activation.
class Picker {
    std::mt19937_64 random_;
    std::deque<std::string> recent_;
public:
    explicit Picker(std::uint64_t seed):random_(seed){}
    int choose(const std::vector<std::string>& keys) {
        if(keys.empty())return -1;
        std::vector<std::string> excluded;
        const std::size_t limit=std::min<std::size_t>(3,keys.size()-1);
        for(const auto& key:recent_)
            if(excluded.size()<limit && std::find(keys.begin(),keys.end(),key)!=keys.end())excluded.push_back(key);
        std::vector<int> allowed;
        for(std::size_t i=0;i<keys.size();++i)
            if(std::find(excluded.begin(),excluded.end(),keys[i])==excluded.end())allowed.push_back(int(i));
        if(allowed.empty())return -1; // Defensive: callers supply unique group keys.
        std::uniform_int_distribution<std::size_t> pick(0,allowed.size()-1);
        return allowed[pick(random_)];
    }
    void played(const std::string& key) {
        recent_.erase(std::remove(recent_.begin(),recent_.end(),key),recent_.end());
        recent_.push_front(key);
        if(recent_.size()>3)recent_.pop_back();
    }
    const std::deque<std::string>& recent() const {return recent_;}
};
}
