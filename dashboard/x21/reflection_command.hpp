// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gpu2d.h"
#include <cstring>
// Metadata is copied by prepare; caller can release its temporary after success.
// Call at reflect_cards boundary, after panes/body and before gamer-card identity.
inline ExboxGpuCommand exbox_nxe_reflection_command(const ExboxGpuNxeReflection& metadata,uint32_t mask_width,uint32_t mask_height,uint32_t mask_pitch){
 ExboxGpuCommand c{};c.kind=EXBOX_GPU_NXE_REFLECTION;const int d=metadata.density==2?2:1;c.dst=c.clip={130*d,852*d,1670*d,147*d};c.opacity=255;c.transform[8]=1;c.pixels=&metadata;c.bytes=sizeof metadata;c.width=mask_width;c.height=mask_height;c.pitch=mask_pitch;return c;
}

