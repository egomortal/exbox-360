// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/xui_handles.hpp"
#include <cassert>
#include <map>
using namespace exbox::editor;
int main() {
    constexpr uint32_t handle=(7U<<16)|5U;
    std::map<uint32_t,uint32_t> words{{100,6},{200,1000},{1040,7},{1044,2000},
                                    {2004,0},{2032,3000}};
    auto read=[&](uint32_t address)->std::optional<uint32_t>{
        const auto found=words.find(address);
        if(found==words.end())return {};return found->second;
    };
    const XuiHandleLayout layout{200,100};
    auto get=[&](uint32_t h=handle){return xui_element(h,read,layout);};
    assert(get()==3000);assert(!get(0));assert(!get((6U<<16)|5));
    assert(!get((7U<<16)|6));
    words.erase(2004);assert(!get());words[2004]=0;
    words[2004]=2100;words[2104]=0;words[2132]=4000;assert(get()==4000);
    words[2104]=2000;assert(!get()); // Cycle is invalid, not a UI element.
    words[2104]=UINT32_MAX-1;assert(!get());
    words[200]=UINT32_MAX-10;assert(!get());words[200]=1000;
    words[2004]=0;words[2032]=0;assert(!get());
    words[2032]=3000;
    for(uint32_t i=0;i<66;++i)words[2000+i*64+4]=2000+(i+1)*64;
    assert(!get()); // Bounded chain, even when all links are readable.
    // Exercise the next page: index256 selects the second page-table word.
    words[100]=257;words[204]=5000;words[5000]=9;words[5004]=6000;
    words[6004]=0;words[6032]=7000;assert(get((9U<<16)|256)==7000);
}
