// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>
namespace exbox::editor {
// Snapshot guest UTF16 bytes before deferred completion. Keep byte order and
// code units exactly; this is the observed default-text acceptance policy.
inline std::vector<uint8_t> keyboard_default(std::span<const uint8_t> input) {
    std::vector<uint8_t> result;
    for(std::size_t i=0;i+1<input.size();i+=2) {
        if(input[i]==0 && input[i+1]==0)break;
        result.push_back(input[i]);result.push_back(input[i+1]);
    }
    return result;
}
inline bool keyboard_copy(std::span<const uint8_t> snapshot,
                          std::span<uint8_t> output,uint32_t capacity) {
    if(!capacity || uint64_t(capacity)*2>output.size())return false;
    const std::size_t chars=std::min(snapshot.size()/2,std::size_t(capacity-1));
    // snapshot is independent of guest output, including when default==output.
    for(std::size_t i=0;i<chars*2;++i)output[i]=snapshot[i];
    output[chars*2]=0;output[chars*2+1]=0;return true;
}
}
