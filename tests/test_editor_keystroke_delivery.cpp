// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/keystroke_delivery.hpp"
#include <array>
#include <cassert>
using namespace exbox::editor;
int main() {
    // Exercise every low-byte device mask; high bits must not change selection.
    for (uint32_t mask = 0; mask < 256; ++mask) {
        KeystrokeQueue q; q.press(0x5841, 7, 0);
        std::array<uint8_t, 12> out; out.fill(0xA5);
        std::array<uint8_t, 8> user; user.fill(0x5A);
        const auto before_out=out; const auto before_user=user;
        assert(!deliver_fallback(q, 0, 0, mask, out, user));
        assert(out==before_out && user==before_user); // SDK success wins.
        bool accepted=deliver_fallback(q, 0, 4306, mask|0x80000000U, out, user);
        assert(accepted == (mask==0 || (mask&1)));
        if (!accepted) {
            assert(out==before_out && user==before_user);
            assert(deliver_fallback(q,0,4306,1,out,user)); // Not consumed.
        }
        const std::array<uint8_t,8> expected{0x58,0x41,0,0,0,1,0,0};
        for(unsigned i=0;i<8;++i) assert(out[i]==expected[i]);
        for(unsigned i=8;i<12;++i) assert(out[i]==0xA5); // Adjacent guest bytes.
        for(unsigned i=0;i<4;++i) assert(user[i]==0);
        for(unsigned i=4;i<8;++i) assert(user[i]==0x5A);
        const auto written=out;
        assert(!deliver_fallback(q,1,4306,0,out,user)); assert(out==written);
    }
    KeystrokeQueue q; q.press(0x5842,9,0);
    std::array<uint8_t,8> out{};
    for(unsigned size=0;size<8;++size)
        assert(!deliver_fallback(q,0,1,0,std::span(out).first(size),{}));
    for(unsigned size=1;size<4;++size)
        assert(!deliver_fallback(q,0,1,0,out,std::span(out).first(size)));
    assert(deliver_fallback(q,0,1,0,out,{}));
    assert(out[1]==0x42 && out[5]==1);
    assert(deliver_fallback(q,400,1,0,out,{})); assert(out[5]==5);
    q.release(9); assert(deliver_fallback(q,401,1,0,out,{})); assert(out[5]==2);
}
