// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
namespace exbox::editor {
// Preserve the observed title hook's ASCII folding, including replacement of
// non-ASCII code units by spaces. This is not a new font or Unicode renderer.
inline std::string heading_text(std::span<const uint8_t> bytes) {
    if(bytes.size()<130 || bytes[0]!=0 || bytes[1]<32 || bytes[1]>=127)return {};
    std::string text;
    for(size_t i=0;i<60;++i) {
        const uint8_t hi=bytes[i*2],lo=bytes[i*2+1];
        if(!hi && !lo)break;
        text.push_back(!hi && lo>=32 && lo<127?static_cast<char>(lo):' ');
    }
    return text;
}
// Encode host status bytes using the existing byte-to-UTF16BE convention.
// Caller publishes the new guest pointer only after this buffer is complete.
inline size_t encode_heading(std::string_view text,std::span<uint8_t> output) {
    if(output.size()<2)return 0;
    const size_t count=std::min({text.size(),size_t(126),output.size()/2-1});
    for(size_t i=0;i<count;++i){output[i*2]=0;output[i*2+1]=static_cast<uint8_t>(text[i]);}
    output[count*2]=output[count*2+1]=0;
    return count;
}
}
