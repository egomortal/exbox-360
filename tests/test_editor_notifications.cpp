// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/notifications.hpp"
#include <cassert>
#include <utility>
#include <vector>
struct Fake {
    uint32_t handle=7;uint64_t mask=0;uint32_t system=0,version=0;
    std::vector<std::pair<uint32_t,uint32_t>> events;
    uint32_t create(uint64_t m,uint32_t s,uint32_t v) {mask=m;system=s;version=v;return handle;}
    void emit(uint32_t a,uint32_t b){events.emplace_back(a,b);}
};
int main() {
    for(uint64_t mask:{0ULL,1ULL,2ULL,3ULL,0x100000000ULL,0x100000002ULL}) {
        for(uint32_t handle:{0U,7U}) {
            Fake f;f.handle=handle;
            assert(exbox::editor::open_notifications(f,mask,1,9)==handle);
            assert(f.mask==mask && f.system==1 && f.version==9);
            if(handle && (mask==0 || (mask&2))) {
                assert(f.events.size()==2);
                assert(f.events[0]==std::make_pair(0x02000001U,0x001510f0U));
                assert(f.events[1]==std::make_pair(10U,1U));
            } else assert(f.events.empty());
        }
    }
}
