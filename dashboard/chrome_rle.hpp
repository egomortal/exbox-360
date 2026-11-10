// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <cstring>
namespace chrome_rle {
// Original lossless DWORD codec. High bit selects a repeated word; other
// packets contain literal words. Packets are bounded by the decoded extent.
inline bool pack(const std::uint32_t *in,size_t count,size_t limit,std::vector<std::uint32_t>&out) {
    if (!in || !count || count>0x7fffffffu) return false;
    size_t words=0;
    auto walk=[&](std::uint32_t *dst) {
        size_t at=0,w=0;
        while(at<count) {
            size_t run=1;while(at+run<count&&in[at+run]==in[at])++run;
            if(run>=3) {if(dst){dst[w]=0x80000000u|std::uint32_t(run);dst[w+1]=in[at];}w+=2;at+=run;}
            else {
                const size_t first=at;at+=run;
                while(at<count){run=1;while(at+run<count&&run<3&&in[at+run]==in[at])++run;if(run>=3)break;at+=run;}
                const size_t n=at-first;if(dst){dst[w]=std::uint32_t(n);std::memcpy(dst+w+1,in+first,n*4);}w+=n+1;
            }
        }
        return w;
    };
    words=walk(nullptr);if(words>limit/4)return false;
    std::vector<std::uint32_t> result(words);if(result.capacity()>limit/4)return false;walk(result.data());out.swap(result);return true;
}
inline bool valid(const std::vector<std::uint32_t>&in,size_t count) {
    size_t i=0,n=0;
    while(i<in.size()) {const auto h=in[i++];const size_t len=h&0x7fffffffu;if(!len||len>count-n)return false;
        const size_t stored=(h>>31)?1:len;if(stored>in.size()-i)return false;i+=stored;n+=len;}
    return n==count;
}
namespace detail {
inline void write_validated(const std::vector<std::uint32_t>&in,std::uint32_t*out) {
    size_t i=0,n=0;while(i<in.size()){const auto h=in[i++];const size_t len=h&0x7fffffffu;
        if(h>>31)std::fill_n(out+n,len,in[i++]);else{std::memcpy(out+n,in.data()+i,len*4);i+=len;}n+=len;}
}
}
inline bool decode(const std::vector<std::uint32_t>&in,std::uint32_t*out,size_t count) {
    // Untrusted/test packet input is fully validated before any output write.
    if(!out||!valid(in,count))return false;
    detail::write_validated(in,out);return true;
}
// No serialized input or mutable packet view. Assignment generates and validates
// packets once; the private immutable representation can then restore in one pass.
class Backing {
    std::vector<std::uint32_t> words_;size_t count_=0;
public:
    bool assign(const std::uint32_t *in,size_t count,size_t limit) {
        std::vector<std::uint32_t> next;
        if(!pack(in,count,limit,next)||!valid(next,count))return false;
        words_.swap(next);count_=count;return true;
    }
    bool empty()const{return words_.empty();}
    size_t bytes()const{return words_.capacity()*sizeof(std::uint32_t);}
    void clear(){std::vector<std::uint32_t>().swap(words_);count_=0;}
    bool restore(std::uint32_t*out,size_t count)const {
        if(!out||empty()||count!=count_)return false;
        detail::write_validated(words_,out);return true;
    }
};
}
