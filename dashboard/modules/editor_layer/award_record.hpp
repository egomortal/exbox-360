// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <span>
#include <optional>
#include <cstdint>
#include <algorithm>
namespace exbox::editor {
using AwardRecord=std::array<uint8_t,52>;
inline std::optional<AwardRecord> award_record(std::span<const uint8_t,16> id,uint32_t filter) {
    if((id[6]&15)!=1)return std::nullopt;
    uint32_t title=(uint32_t(id[12])<<24)|(uint32_t(id[13])<<16)|(uint32_t(id[14])<<8)|id[15];
    if(filter&&title!=filter)return std::nullopt;
    AwardRecord out{};
    std::copy(id.begin()+12,id.end(),out.begin());
    std::copy(id.begin(),id.end(),out.begin()+4);
    out[45]=2; // Big-endian granted flag0x00020000 at offset44.
    return out;
}
}
