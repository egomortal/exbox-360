// SPDX-License-Identifier: GPL-3.0-or-later
#include "hdr_image.hpp"

#include <algorithm>
#include <cmath>

namespace hdr
{
namespace
{
// PQ code (0..1023) to tone-mapped linear light relative to paper white.
struct Tables
{
    float light[1024];
    std::uint8_t srgb[4096]; // linear 0..1 in 4096 steps to sRGB 8-bit
    Tables()
    {
        constexpr double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
        constexpr double white = 203.0, shoulder = 4.0;
        for (int i = 0; i < 1024; ++i)
        {
            const double t = std::pow(i / 1023.0, 1.0 / m2);
            const double nits = 10000.0 * std::pow(std::max(t - c1, 0.0) / (c2 - c3 * t), 1.0 / m1);
            const double x = nits / white;
            light[i] = float(x * (1.0 + x / (shoulder * shoulder)) / (1.0 + x));
        }
        for (int i = 0; i < 4096; ++i)
        {
            const double v = i / 4095.0;
            const double s = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
            srgb[i] = std::uint8_t(std::clamp(int(s * 255.0 + 0.5), 0, 255));
        }
    }
};
const Tables &tables()
{
    static const Tables t;
    return t;
}
// BT.2020 to BT.709 (linear light).
constexpr float k2020to709[9] = {1.6605f, -0.5876f, -0.0728f, -0.1246f, 1.1329f, -0.0083f, -0.0182f, -0.1006f, 1.1187f};

std::uint8_t encode(float v)
{
    const int i = int(std::clamp(v, 0.0f, 1.0f) * 4095.0f + 0.5f);
    return tables().srgb[i];
}
std::uint32_t pack(float r, float g, float b)
{
    const float R = k2020to709[0] * r + k2020to709[1] * g + k2020to709[2] * b;
    const float G = k2020to709[3] * r + k2020to709[4] * g + k2020to709[5] * b;
    const float B = k2020to709[6] * r + k2020to709[7] * g + k2020to709[8] * b;
    return 0xff000000u | std::uint32_t(encode(R)) << 16 | std::uint32_t(encode(G)) << 8 | encode(B);
}
} // namespace

std::uint32_t pq101010_to_argb(std::uint32_t p)
{
    const Tables &t = tables();
    return pack(t.light[(p >> 20) & 1023], t.light[(p >> 10) & 1023], t.light[p & 1023]);
}

void convert(const std::uint32_t *src, int sw, int sh, int src_stride, std::uint32_t *dst, int dw, int dh,
             int dst_stride)
{
    if (!src || !dst || sw <= 0 || sh <= 0)
        return;
    dw = std::clamp(dw, 1, sw);
    dh = std::clamp(dh, 1, sh);
    const Tables &t = tables();
    for (int y = 0; y < dh; ++y)
    {
        // Each destination pixel averages the light of the source pixels it covers.
        const int y0 = int(std::int64_t(y) * sh / dh), y1 = std::max(y0 + 1, int(std::int64_t(y + 1) * sh / dh));
        for (int x = 0; x < dw; ++x)
        {
            const int x0 = int(std::int64_t(x) * sw / dw), x1 = std::max(x0 + 1, int(std::int64_t(x + 1) * sw / dw));
            float r = 0, g = 0, b = 0;
            for (int yy = y0; yy < y1; ++yy)
            {
                const std::uint32_t *row = src + std::int64_t(yy) * src_stride;
                for (int xx = x0; xx < x1; ++xx)
                {
                    const std::uint32_t p = row[xx];
                    r += t.light[(p >> 20) & 1023];
                    g += t.light[(p >> 10) & 1023];
                    b += t.light[p & 1023];
                }
            }
            const float n = 1.0f / float((y1 - y0) * (x1 - x0));
            dst[std::int64_t(y) * dst_stride + x] = pack(r * n, g * n, b * n);
        }
    }
}
} // namespace hdr
