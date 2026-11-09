// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/profile_layout.hpp"
#include <array>
#include <cassert>
using namespace exbox::editor;
int main() {
    std::array<uint32_t, 33> ids{};
    assert(profile_buffer_plan({},true,0,false).status == ProfileBufferStatus::invalid_argument);
    assert(profile_buffer_plan(ids,true,0,false).status == ProfileBufferStatus::invalid_argument);
    auto one = std::span(ids).first(1);
    assert(profile_buffer_plan(one,false,0,false).status == ProfileBufferStatus::invalid_argument);
    assert(profile_buffer_plan(one,true,1,false).status == ProfileBufferStatus::invalid_argument);
    for(uint32_t type=0;type<16;++type) for(uint32_t length=0;length<=4095;++length) {
        ids[0]=(type<<28)|(length<<16)|0xffffU;
        const uint32_t expected=48+((type==4||type==6)?length:0);
        auto q=profile_buffer_plan(one,true,0,false);
        assert(q.status==ProfileBufferStatus::insufficient_buffer && q.update_size);
        assert(q.header_bytes==48 && q.total_bytes==expected);
        q=profile_buffer_plan(one,true,expected-1,true);
        assert(q.status==ProfileBufferStatus::insufficient_buffer && !q.update_size);
        q=profile_buffer_plan(one,true,expected,true);
        assert(q.status==ProfileBufferStatus::ready && !q.update_size);
        q=profile_buffer_plan(one,true,UINT32_MAX,true);
        assert(q.status==ProfileBufferStatus::ready && q.total_bytes==expected);
    }
    ids.fill(0x6fff0001U);
    auto q=profile_buffer_plan(std::span(ids).first(32),true,0,true);
    assert(q.header_bytes==1288 && q.total_bytes==132328 && q.update_size);
    // Mixed fixed-size, UTF16 and binary declarations preserve input order
    // for the later serializer; layout calculation does not mutate IDs.
    const std::array<uint32_t,3> mixed{0x10000001,0x40040002,0x63e80003};
    q=profile_buffer_plan(mixed,true,1132,true);
    assert(q.total_bytes==1132 && q.header_bytes==128 && q.status==ProfileBufferStatus::ready);
}
