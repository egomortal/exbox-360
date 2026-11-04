// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/keystroke_queue.hpp"
#include <cassert>
using exbox::editor::KeystrokeQueue;
int main() {
    KeystrokeQueue q;
    assert(!q.poll(0));q.press(10,100,1000);q.press(10,100,1001);
    auto e=q.poll(1001);assert(e && e->key==10 && e->flags==1);
    assert(!q.poll(1399));e=q.poll(1400);assert(e && e->flags==5);
    assert(!q.poll(1499));e=q.poll(1500);assert(e && e->flags==5);
    q.release(100);e=q.poll(1600);assert(e && e->flags==2);assert(!q.poll(9999));
    q.press(20,200,2000);q.press(21,200,2000);q.release(200);
    for(auto expected:{exbox::editor::Stroke{20,1},{21,1},{20,2},{21,2}}) {
        e=q.poll(2000);assert(e && e->key==expected.key && e->flags==expected.flags);
    }
    assert(!q.poll(5000));q.press(30,300,6000);q.poll(6000);
    assert(!q.poll(5999));q.release(999);e=q.poll(6400);assert(e && e->flags==5);
}
