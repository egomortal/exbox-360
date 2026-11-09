// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/gpu_presenter/surface_contract.hpp"
#include <cassert>
#include <iostream>
using namespace exbox::presenter;
template<class F> void rejected(F f) {
    bool caught=false;try{f();}catch(const std::exception&){caught=true;}assert(caught);
}
int main() {
    Surface s{1919,1079,{1920*1079,1920},{1920*540,1920},Packing::nv12,
              Matrix::bt709,Range::limited,Transfer::sdr,false};
    validate(s); // odd visible extent, padded planes, ceil chroma rows
    auto bad=s;bad.y.stride=1919;rejected([&]{validate(bad);});
    bad=s;bad.uv.bytes--;rejected([&]{validate(bad);});
    bad=s;bad.matrix=Matrix::unknown;rejected([&]{validate(bad);});
    bad=s;bad.width=0xffffffff;rejected([&]{validate(bad);});
    Surface hdr{3840,2160,{7680*2160,7680},{7680*1080,7680},Packing::ten_low,
                Matrix::bt2020,Range::limited,Transfer::pq,true};
    validate(hdr);hdr.packing=Packing::ten_high;validate(hdr);
    assert(desired_output(hdr,true,0x20000000)==Output::pq);
    assert(desired_output(hdr,false,0x20000000)==Output::sdr);
    assert(desired_output(hdr,true,0)==Output::sdr);
    assert(needs_tone_map(hdr,Output::sdr));
    assert(!needs_tone_map(hdr,Output::pq));
    bad=hdr;bad.primaries2020=false;rejected([&]{validate(bad);});
    bad=hdr;bad.packing=Packing::nv12;rejected([&]{validate(bad);});
    validate_scanout(Output::pq,0x8100070422000000ULL,true,2);
    validate_scanout(Output::sdr,0x8000000022000000ULL,true,1);
    rejected([&]{validate_scanout(Output::pq,0x8100070422000000ULL,false,2);});
    rejected([&]{validate_scanout(Output::pq,0x8100070422000000ULL,true,1);});
    DecoderLease lease;lease.acquire();lease.submit(9);
    assert(!lease.release(8));rejected([&]{lease.acquire();});
    assert(lease.release(9));rejected([&]{lease.release(9);});
    lease.acquire();assert(lease.release(9)); // cancellation before submission
    std::cout<<"PASS surface metadata, padded planes, HDR/scanout policy, decoder lifetime\n";
}
