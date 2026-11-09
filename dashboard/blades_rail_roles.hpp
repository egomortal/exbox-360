#pragma once
#include <cmath>
#include <cstdint>

// Source census: all sixteen recovered Blades dashmain.xur revisions.
// Original point sizes are revision data. Native pixel sizes are a separate
// calibration against finished reference frames, not a claim about Xbox DPI.
namespace blades_rails {
constexpr int family(int build) {
    return build<5759?0:build<6683?1:2;
}

constexpr int source_points(int build) {
    constexpr int points[]={23,21,18};
    return points[family(build)];
}

constexpr int native_pixels(int build,bool selected) {
    // Selected source labels scale by 1.1. Integer pixel rounding is explicit.
    constexpr int pixels[3][2]={{50,55},{49,54},{39,43}};
    return pixels[family(build)][selected];
}

struct Pixel { std::uint8_t gray,alpha; };
inline Pixel composite_coverage(std::uint8_t ink_coverage,std::uint8_t shadow_coverage) {
    // Keep straight-alpha RGB: a completed label will be blended once more
    // onto its blade. Premultiplied cache pixels would darken that second blit.
    const float ink=float(ink_coverage)/255*(150.f/255);
    const float shadow=float(shadow_coverage)/255*(128.f/255);
    const float visible_shadow=shadow*(1-ink);
    const float alpha=ink+visible_shadow;
    if (alpha<=0) return {0,0};
    return {std::uint8_t(std::lround((15*ink+235*visible_shadow)/alpha)),
            std::uint8_t(std::lround(alpha*255))};
}
}
