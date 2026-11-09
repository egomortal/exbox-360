// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/profile_write.hpp"
#include <array>
#include <cassert>
using namespace exbox::editor;
int main() {
    for(unsigned type=0;type<256;++type) assert(profile_binary_write(uint8_t(type)) == (type==0 || type==6));
    std::array<uint8_t,5> input{0,255,17,128,0};
    auto snapshot=profile_payload(input,0,true); input.fill(99);
    assert((snapshot==std::vector<uint8_t>{0,255,17,128,0}));
    for(size_t size:{size_t{0},size_t{1},size_t{1000},size_t{4095}}) {
        auto zeros=profile_payload({},size,false); assert(zeros.size()==size);
        for(auto value:zeros)assert(value==0);
    }
    assert(profile_payload({},1000,true).empty());
}
