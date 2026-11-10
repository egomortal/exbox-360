#include "appearance.hpp"
#include <cassert>
#include <vector>
using namespace editor_dashboard::appearance;
int main(){
    std::array<std::uint8_t,1000> b{};
    b[3]=7;b[4]=0x3e;b[5]=0x80; // 0.25
    b[8]=0xbf; // -0.5
    b[0x123]=2;b[0x124]=0x12;b[0x125]=0x34;b[0x131]=2;
    b[0xfc]=0xff;b[0xfd]=0x12;b[0xfe]=0x34;b[0xff]=0x56;
    auto p=parse(b);assert(p&&p->version==7&&p->weight==.25f&&p->height==-.5f);
    assert(p->body.id.pack_index()==0x1234&&p->body.categories==2);
    assert(p->colors[0]==0xff123456&&coherent(*p,b));
    auto changed=b;changed[999]=1;assert(!coherent(*p,changed));
    auto q=parse(changed);assert(q&&!q->same_commit(*p));
    assert(!parse(ByteView(b).first(999)));
    std::vector<std::uint8_t> oversized(1001);assert(!parse(oversized));
    b[4]=0x7f;b[5]=0xc0;assert(!parse(b)); // NaN proportion
    b[4]=0x3e;b[5]=0x80;b[0x4c]=0x7f;b[0x4d]=0x80;assert(!parse(b)); // infinite texture transform
}
