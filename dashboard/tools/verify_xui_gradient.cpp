#include "../xui_gradient.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

using namespace xui::early_xam_gradient;

int main(int argc, char** argv) {
    Ramp128 ramp;
    const Stop monochrome[] = {{0, 0xff000000}, {1, 0xffffffff}};
    assert(BuildRamp128(monochrome, 2, ramp));
    assert(ramp[0] == 0xff000000 && ramp[64] == 0xff808080);
    assert(ramp[127] == 0xfffdfdfd);
    for (std::size_t i = 0; i < ramp.size(); ++i) {
        const auto exact = SampleClampedLinear(ramp, (float(i) + 0.5f) / 128);
        assert(exact == UnpackNormalized(ramp[i]));
    }
    assert(SampleClampedLinear(ramp, -10) == UnpackNormalized(ramp.front()));
    assert(SampleClampedLinear(ramp, 10) == UnpackNormalized(ramp.back()));
    assert(PackD3DColor({-1, 2, 0, 1}) == 0xff00ff00);
    assert(PackD3DColor({std::numeric_limits<float>::quiet_NaN(), 0, 0, 1}) == 0xff000000);
    assert(PackD3DColor({0, 0.5f, 1, 0.5f}) == 0x800080ff);
    for (unsigned byte = 0; byte < 256; ++byte) {
        const std::uint32_t gray = byte * 0x01010101u;
        assert(PackD3DColor(UnpackNormalized(gray)) == gray);
    }
    const Stop duplicates[] = {{0,0xff000000},{.5f,0xffff0000},{.5f,0xff00ff00},{1,0xffffffff}};
    assert(BuildRamp128(duplicates,4,ramp) && ramp[64] == 0xffff0000);
    const Stop dense[] = {{0,0xff000000},{.001f,0xffffffff},{.002f,0xff000000},{1,0xffffffff}};
    assert(BuildRamp128(dense,4,ramp) && ramp[1] == 0xff000000);
    const Stop invalid[] = {{1,0},{0,0}};
    assert(!BuildRamp128(invalid,2,ramp));
    assert(!BuildRamp128(nullptr,0,ramp));
    if (argc == 3) {
        std::ifstream input(argv[1]);
        std::ofstream output(argv[2],std::ios::binary);
        std::size_t count;
        unsigned sets = 0;
        while (input >> count) {
            std::vector<Stop> stops(count);
            for (auto& stop : stops) input >> stop.position >> stop.argb;
            assert(input && BuildRamp128(stops.data(),stops.size(),ramp));
            output.write(reinterpret_cast<const char*>(ramp.data()),sizeof(ramp));
            ++sets;
        }
        std::cout << "source_stop_sets=" << sets << '\n';
    }
    std::cout << "gradient_ramp_and_sampling_tests=pass\n";
}
