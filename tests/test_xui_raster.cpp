#include "../xui_composite.hpp"
#include "../xui_raster_avx2.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <random>
int main() {
    if (!xui::supports_avx2()) return 0;
    std::mt19937 rng(360);std::array<uint32_t,4097> table;
    for (auto &v:table) v=rng();
    size_t checked=0;
    for (int mode=0;mode<=6;mode++) for (int test=0;test<2000;test++) {
        uint32_t pixels[16],original[16];float coverage[16];
        for (int i=0;i<16;i++) {pixels[i]=original[i]=rng()|0xff000000;coverage[i]=test%3?float(rng()%10001)/10000:1;}
        const float opacity=float(rng()%10001)/10000;const uint32_t solid=rng();
        const int type=test%3;const float gx=.001f,hx=.0007f,gc=-.15f,hc=.2f,gy=.02f,hy=.03f;
        assert(xui::raster_avx2(pixels,coverage,16,11,gy,hy,gx,hx,gc,hc,type?table.data():nullptr,type==2?3:2,solid,opacity,mode)==16);
        for (int i=0;i<16;i++) {
            const float u=gx*(float(11+i)+.5f)+gy+gc,v=hx*(float(11+i)+.5f)+hy+hc;
            const float phase=type==2?2*std::sqrt(u*u+v*v):u+.5f;
            const uint32_t source=type?table[size_t(std::clamp(int(phase*4096+.5f),0,4096))]:solid;
            const uint32_t expected=xui::composite(original[i],source,opacity,coverage[i],mode);
            if (pixels[i]!=expected) {std::fprintf(stderr,"mode %d test %d pixel %d: %08x != %08x\n",mode,test,i,pixels[i],expected);return 1;}
            checked++;
        }
    }
    std::printf("%zu scalar/AVX2 pixel matches across every blend mode\n",checked);
}
