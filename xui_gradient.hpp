#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace xui::early_xam_gradient {

struct Stop {
    float position;
    std::uint32_t argb;
};

using Ramp128 = std::array<std::uint32_t, 128>;
using Color = std::array<float, 4>;

inline std::uint32_t FloatBits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

inline float BitsFloat(std::uint32_t value) {
    float result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

inline std::uint32_t PackD3DColor(const Color& rgba) {
    constexpr int shifts[] = {16, 8, 0, 24};
    constexpr float scale = -255.0f / 4194304.0f;
    const float maximum = BitsFloat(0x404000ff);
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < rgba.size(); ++i) {
        float encoded = std::fma(-rgba[i], scale, 3.0f);
        if (!(encoded >= 3.0f)) encoded = 3.0f;
        encoded = std::min(encoded, maximum);
        result |= (FloatBits(encoded) & 255) << shifts[i];
    }
    return result;
}

inline Color UnpackNormalized(std::uint32_t argb) {
    constexpr float normalize = 1.0f / 255.0f;
    return {float((argb >> 16) & 255) * normalize,
            float((argb >> 8) & 255) * normalize,
            float(argb & 255) * normalize,
            float(argb >> 24) * normalize};
}

// Original early XAM CPU algorithm; dense stops advance once per texel.
inline bool BuildRamp128(const Stop* stops, std::size_t count, Ramp128& output) {
    if (!stops || count < 2) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(stops[i].position)) return false;
        if (i && stops[i].position < stops[i - 1].position) return false;
    }
    std::size_t lower = 0, upper = 1;
    for (std::size_t index = 0; index < output.size(); ++index) {
        const float phase = float(index) / float(output.size());
        if (phase > stops[upper].position) {
            lower = upper;
            if (upper + 1 < count) ++upper;
        }
        const float span = stops[upper].position - stops[lower].position;
        float factor;
        if (upper == lower || span < 0.0001f) factor = 1.0f;
        else if (phase < stops[lower].position) factor = 0.0f;
        else factor = (phase - stops[lower].position) / span;
        const Color first = UnpackNormalized(stops[lower].argb);
        const Color second = UnpackNormalized(stops[upper].argb);
        Color value;
        for (std::size_t channel = 0; channel < value.size(); ++channel) {
            const float difference = second[channel] - first[channel];
            value[channel] = std::fma(difference, factor, first[channel]);
        }
        output[index] = PackD3DColor(value);
    }
    return true;
}

// Mathematical reference only; Xenos interpolation precision is unverified.
inline Color SampleClampedLinear(const Ramp128& ramp, float phase) {
    if (!std::isfinite(phase)) return {};
    const float coordinate = std::clamp(phase * float(ramp.size()) - 0.5f,
                                        0.0f, float(ramp.size() - 1));
    const auto left = std::size_t(std::floor(coordinate));
    const auto right = std::min(left + 1, ramp.size() - 1);
    const float fraction = coordinate - float(left);
    Color first = UnpackNormalized(ramp[left]);
    const Color second = UnpackNormalized(ramp[right]);
    for (std::size_t i = 0; i < first.size(); ++i)
        first[i] += (second[i] - first[i]) * fraction;
    return first;
}

}
