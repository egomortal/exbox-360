// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace exbox::editor {
// Reference integer area average: alpha-weight RGB, average alpha separately.
// This deliberately preserves truncation rather than adding rounding/gamma.
inline std::vector<uint8_t> area_tile(std::span<const uint8_t> rgba,uint32_t w,uint32_t h,uint32_t target) {
    if(!w||!h||!target||rgba.size()/4<uint64_t(w)*h)return {};
    std::vector<uint8_t> out(size_t(target)*target*4);
    for(uint32_t y=0;y<target;++y)for(uint32_t x=0;x<target;++x){
        uint32_t x0=uint64_t(x)*w/target,x1=uint64_t(x+1)*w/target;
        uint32_t y0=uint64_t(y)*h/target,y1=uint64_t(y+1)*h/target;
        if(x1<=x0)x1=x0+1;if(y1<=y0)y1=y0+1;
        uint64_t r=0,g=0,b=0,a=0,n=0;
        for(uint32_t sy=y0;sy<y1;++sy)for(uint32_t sx=x0;sx<x1;++sx){
            const auto i=(size_t(sy)*w+sx)*4;const uint64_t alpha=rgba[i+3];
            r+=rgba[i]*alpha;g+=rgba[i+1]*alpha;b+=rgba[i+2]*alpha;a+=alpha;++n;
        }
        auto i=(size_t(y)*target+x)*4;
        out[i]=a?r/a:0;out[i+1]=a?g/a:0;out[i+2]=a?b/a:0;out[i+3]=a/n;
    }
    return out;
}
inline std::vector<uint32_t> tile_size_ladder(uint32_t hint){return hint>=128?std::vector<uint32_t>{128,96,64}:std::vector<uint32_t>{64};}
}
