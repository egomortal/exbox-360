// HDR10 (PQ, BT.2020, 10-bit packed) to sRGB: the conversion the Pictures
// viewer uses for PS5 HDR screenshots. Build with `make test`.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../hdr_image.hpp"

#include <cstdio>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

static std::uint32_t px(unsigned r, unsigned g, unsigned b)
{
    return (r & 1023) << 20 | (g & 1023) << 10 | (b & 1023);
}
static int R(std::uint32_t c)
{
    return int((c >> 16) & 255);
}
static int G(std::uint32_t c)
{
    return int((c >> 8) & 255);
}
static int B(std::uint32_t c)
{
    return int(c & 255);
}

int main()
{
    using hdr::pq101010_to_argb;
    // Black is black, and opaque.
    CHECK(pq101010_to_argb(px(0, 0, 0)) == 0xff000000u);
    // PQ code 592 is about 203 nits, paper white: a neutral light grey, not clipped.
    const std::uint32_t white = pq101010_to_argb(px(592, 592, 592));
    CHECK(R(white) == G(white) && G(white) == B(white));
    CHECK(R(white) > 170 && R(white) < 215);
    // Brighter highlights roll off toward white without wrapping.
    const std::uint32_t bright = pq101010_to_argb(px(800, 800, 800));
    CHECK(R(bright) > R(white) && R(bright) <= 255);
    CHECK(pq101010_to_argb(px(1023, 1023, 1023)) == 0xffffffffu || R(pq101010_to_argb(px(1023, 1023, 1023))) >= 250);
    // Monotonic in each channel.
    int last = -1;
    bool rising = true;
    for (unsigned v = 0; v < 1024; v += 16)
    {
        const int r = R(pq101010_to_argb(px(v, v, v)));
        rising = rising && r >= last;
        last = r;
    }
    CHECK(rising);
    // Channel placement: R in bits 20-29, B in 0-9.
    const std::uint32_t red = pq101010_to_argb(px(600, 0, 0));
    CHECK(R(red) > 150 && G(red) == 0 && B(red) == 0); // BT.2020 red is outside BT.709: clipped, still red
    const std::uint32_t blue = pq101010_to_argb(px(0, 0, 600));
    CHECK(B(blue) > 150 && R(blue) == 0);
    const std::uint32_t green = pq101010_to_argb(px(0, 600, 0));
    CHECK(G(green) > 150 && R(green) == 0);

    // A 4x2 image halved: each output pixel is the average of a 2x2 block.
    const std::vector<std::uint32_t> src = {px(592, 592, 592), px(592, 592, 592), px(0, 0, 0), px(0, 0, 0),
                                            px(592, 592, 592), px(592, 592, 592), px(0, 0, 0), px(0, 0, 0)};
    std::vector<std::uint32_t> dst(2, 0);
    hdr::convert(src.data(), 4, 2, 4, dst.data(), 2, 1, 2);
    CHECK(dst[0] == white && dst[1] == 0xff000000u);
    // Same size copies pixel for pixel; a destination larger than the source is clamped.
    std::vector<std::uint32_t> same(8, 0);
    hdr::convert(src.data(), 4, 2, 4, same.data(), 9, 9, 4);
    CHECK(same[0] == white && same[3] == 0xff000000u && same[4] == white);
    // A half-white, half-black block averages light, not codes: lighter than code 296.
    const std::vector<std::uint32_t> mix = {px(592, 592, 592), px(0, 0, 0)};
    std::uint32_t one = 0;
    hdr::convert(mix.data(), 2, 1, 2, &one, 1, 1, 1);
    CHECK(R(one) > R(pq101010_to_argb(px(296, 296, 296))));
    // Nothing to do: nothing written.
    std::uint32_t untouched = 7;
    hdr::convert(nullptr, 4, 2, 4, &untouched, 1, 1, 1);
    CHECK(untouched == 7);

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
