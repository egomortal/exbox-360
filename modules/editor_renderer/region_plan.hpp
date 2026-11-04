// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace exbox::editor::render {
// Coordinates here are already host texels. Guest scaling belongs at the input
// adapter; this layer must not invent a scale or alter the allocator policy.
struct Extent { std::int32_t width, height; };
struct Region { std::int32_t left, top, right, bottom; };
enum class ClearCoverage { whole_target, rectangles, none };
struct ClearPlan {
    ClearCoverage coverage;
    std::vector<Region> rectangles;
};

// An empty input requests the whole allocation. A nonempty input that clips
// away requests no work. Conflating these cases would clear an unrelated target.
inline ClearPlan plan_clear(Extent allocation, std::span<const Region> requests) {
    if (allocation.width <= 0 || allocation.height <= 0)
        return {ClearCoverage::none, {}};
    if (requests.empty()) return {ClearCoverage::whole_target, {}};
    ClearPlan plan{ClearCoverage::none, {}};
    for (const auto& region : requests) {
        Region clipped{std::max(region.left, 0), std::max(region.top, 0),
                       std::min(region.right, allocation.width),
                       std::min(region.bottom, allocation.height)};
        if (clipped.left < clipped.right && clipped.top < clipped.bottom)
            plan.rectangles.push_back(clipped);
    }
    if (!plan.rectangles.empty()) plan.coverage = ClearCoverage::rectangles;
    return plan;
}

// ABI for the existing helper resolve shader, NOT the guest draw's 72-byte ABI.
// The renderer must bind this inside its existing 36-byte helper push range.
struct ResolveParameters {
    std::int32_t source_delta[2];
    std::uint32_t samples;
    std::uint32_t average;
    float scale[4];
};
static_assert(sizeof(ResolveParameters) == 32);

// A narrow host-space planner. The caller already chose source/destination
// regions and resources. Invalid/unsupported arithmetic stays explicit; the
// future adapter must preserve its reference fallback rather than silently skip.
inline std::optional<ResolveParameters> plan_resolve(
    std::int32_t source_x, std::int32_t source_y, Region destination,
    std::uint32_t samples, bool depth_source, float value_scale) {
    if (samples == 0 || destination.left >= destination.right ||
        destination.top >= destination.bottom) return std::nullopt;
    const std::int64_t dx = std::int64_t(source_x) - destination.left;
    const std::int64_t dy = std::int64_t(source_y) - destination.top;
    constexpr auto lo = std::numeric_limits<std::int32_t>::min();
    constexpr auto hi = std::numeric_limits<std::int32_t>::max();
    if (dx < lo || dx > hi || dy < lo || dy > hi) return std::nullopt;
    return ResolveParameters{{std::int32_t(dx), std::int32_t(dy)}, samples,
                             depth_source ? 0u : 1u,
                             {value_scale,value_scale,value_scale,value_scale}};
}
} // namespace exbox::editor::render
