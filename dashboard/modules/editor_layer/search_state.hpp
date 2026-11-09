// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>
namespace exbox::editor {
// Own host-side search state. No guest memory, GPU, or original title code.
class SearchState {
    std::mutex mutex_;
    bool open_=false,applied_=false,possessed_=false,rebuild_=false;
    std::string query_;
    std::vector<std::string> rows_;
public:
    void overlay(bool open,const char* query,const char* const* rows,int count) {
        std::lock_guard lock(mutex_);
        open_=open;query_=query?query:"";rows_.clear();
        for(int i=0;i<std::min(count,9);++i)rows_.emplace_back(rows && rows[i]?rows[i]:"");
    }
    void applied(bool value){std::lock_guard lock(mutex_);applied_=value;}
    void possessed(bool value){std::lock_guard lock(mutex_);possessed_=value;}
    void rebuild(){std::lock_guard lock(mutex_);rebuild_=true;}
    bool consume_rebuild(){std::lock_guard lock(mutex_);const bool pending=rebuild_;rebuild_=false;return pending;}
    int status(char* output,int capacity) {
        if(!output || capacity<8)return 0;
        std::lock_guard lock(mutex_);
        std::string line;
        if(open_) {
            line="Search: "+query_+"_";
            if(!query_.empty() && !rows_.empty())line+="  ("+rows_.front()+")";
        } else if(applied_ && !query_.empty()) {
            line="Filter: "+query_;
            line+=rows_.empty()?"  (Ctrl+F to edit)":"  ("+rows_.front()+", Ctrl+F to edit)";
        } else return 0;
        const auto size=std::min(line.size(),static_cast<size_t>(capacity-1));
        std::copy_n(line.data(),size,output);output[size]=0;return static_cast<int>(size);
    }
};
}
