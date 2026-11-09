// SPDX-License-Identifier: BSD-3-Clause
#pragma once
namespace av2::sort_filter_legend {
constexpr unsigned width = 256, height = 40;
constexpr int reference_width = 1280, reference_height = 720;
// Authored placement after the evidenced B slot, with a 24-pixel gap.
// This is not a claim of exact retail Y positioning.
constexpr int left = 239 + 256 + 24, top = 630;
constexpr const char* runtime_path = "/app0/assets/ui/sort-filter-legend.rle";
constexpr bool visible(int catalog_scope) { return catalog_scope >= 0; }
struct Rect { int left, top, right, bottom; bool valid; };
inline Rect fit(int x, int y, int width_pixels, int height_pixels) {
  if (width_pixels <= 0 || height_pixels <= 0) return {0, 0, 0, 0, false};
  const int l = x + left * width_pixels / reference_width;
  const int t = y + top * height_pixels / reference_height;
  return {l, t, l + int(width) * width_pixels / reference_width,
          t + int(height) * height_pixels / reference_height, true};
}
}
extern "C" bool av2_sort_filter_legend_active();
