// Ex-Box 360: HDR screenshots made viewable. Pure, no SDL, no jxrlib.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A PS5 HDR screenshot (JPEG XR, pixel format 32bppRGB101010) holds HDR10:
// BT.2020 colours on the PQ (SMPTE ST 2084) curve, 10 bits per channel, packed
// as B in bits 0-9, G in 10-19 and R in 20-29 (checked against a real capture
// of the PS5's Settings screen). This turns it into ordinary sRGB for SDL:
// PQ to light in nits, 203 nits as paper white (ITU-R BT.2408), a soft
// shoulder for highlights up to four times that, BT.2020 to BT.709 primaries,
// then the sRGB curve.
#pragma once

#include <cstdint>

namespace hdr
{
// One packed pixel to 0xAARRGGBB (alpha 255).
std::uint32_t pq101010_to_argb(std::uint32_t packed);

// A whole image, box-filtered down (or copied) into a dw x dh ARGB8888 buffer.
// src_stride and dst_stride are in pixels. dw / dh are clamped to the source.
void convert(const std::uint32_t *src, int sw, int sh, int src_stride, std::uint32_t *dst, int dw, int dh,
             int dst_stride);
} // namespace hdr
