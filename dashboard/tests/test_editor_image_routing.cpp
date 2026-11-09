// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/image_routing.hpp"
#include <array>
#include <cassert>
#include <cstdint>
using namespace exbox::editor;
int main() {
    std::array<uint8_t,32> bytes{};
    bytes[0]=0x89;bytes[1]='P';bytes[2]='N';bytes[3]='G';
    // Every truncated view: never inspect width until all24 bytes are present.
    for(size_t count=0;count<=bytes.size();++count) {
        auto view=std::span<const uint8_t>(bytes).first(count);
        assert(image_prefix(view)==(count>8));
        assert(gamer_image_destination(view)==(count>=24?"gamerpic_small.png":""));
        assert(tile_image_destination(15,view)==(count>8?"fullbody.png":""));
        assert(tile_image_destination(14,view).empty());
    }
    // Exhaustive one-byte signature corruption; the remaining PNG signature
    // bytes are intentionally ignored by the original routing contract.
    for(unsigned index=0;index<4;++index)for(unsigned value=0;value<256;++value) {
        auto changed=bytes;changed[index]=static_cast<uint8_t>(value);
        assert(image_prefix(changed)==(changed[index]==bytes[index]));
    }
    for(uint32_t width=0;width<65536;++width) {
        bytes[16]=static_cast<uint8_t>(width>>24);bytes[17]=static_cast<uint8_t>(width>>16);
        bytes[18]=static_cast<uint8_t>(width>>8);bytes[19]=static_cast<uint8_t>(width);
        const auto before=bytes;
        assert(gamer_image_destination(bytes)==(width>=64?"gamerpic.png":"gamerpic_small.png"));
        assert(bytes==before); // Routing must not alter original compressed bytes.
    }
    for(uint32_t width:{65536u,0x80000000u,0xffffffffu}) {
        bytes[16]=static_cast<uint8_t>(width>>24);bytes[17]=static_cast<uint8_t>(width>>16);
        bytes[18]=static_cast<uint8_t>(width>>8);bytes[19]=static_cast<uint8_t>(width);
        assert(gamer_image_destination(bytes)=="gamerpic.png");
    }
    for(uint32_t type:{0u,14u,16u,0xffffffffu})assert(tile_image_destination(type,bytes).empty());
}
