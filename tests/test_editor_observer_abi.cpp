// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/ppc/context.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
namespace { std::vector<std::string> events; }
namespace av2 { void note_title_hook(const char* name) { events.emplace_back(name); } }
extern "C" void observe_poll(PPCRegister&,PPCRegister&) asm("__wrap__Z13AE_MktNetPollRN3rex3ppc8RegisterES2_");
extern "C" void observe_headers(PPCRegister&,PPCRegister&) asm("__wrap__Z16AE_MktNetHeadersRN3rex3ppc8RegisterES2_");
extern "C" void observe_close(PPCRegister&) asm("__wrap__Z14AE_MktNetCloseRN3rex3ppc8RegisterE");
int main() {
    constexpr std::array<uint64_t,6> patterns={0,~uint64_t(0),0x945065f0,
        0x8000000000000000ULL,0x00000000ffffffffULL,0xdeadbeef01234567ULL};
    for (auto value:patterns) {
        PPCRegister a{},b{};a.u64=value;b.u64=~value;
        observe_poll(a,b);observe_headers(a,b);observe_close(a);
        assert(a.u64==value && b.u64==~value);
    }
    assert(events.size()==patterns.size()*3);
    for(size_t i=0;i<events.size();i+=3) {
        assert(events[i]=="AE_MktNetPoll");
        assert(events[i+1]=="AE_MktNetHeaders");
        assert(events[i+2]=="AE_MktNetClose");
    }
}
