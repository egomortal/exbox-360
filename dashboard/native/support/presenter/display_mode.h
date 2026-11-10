/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef EXBOX_DISPLAY_MODE_H
#define EXBOX_DISPLAY_MODE_H
#include <stdint.h>
#include <stddef.h>

typedef struct ExboxDisplayMode { uint32_t width, height, refresh_millihz; } ExboxDisplayMode;
/* SDR1080 is the default. A requested4K mode may fall back to1080, never
 * upscale a1080 preference or accept a lower-refresh mode implicitly. */
static inline size_t exbox_select_display_mode(const ExboxDisplayMode *modes, size_t count, unsigned scale) {
    size_t selected = SIZE_MAX;
    unsigned best = 0;
    uint32_t refresh = 0;
    if (!modes || (scale != 1 && scale != 2)) return SIZE_MAX;
    for (size_t i = 0; i < count; ++i) {
        const ExboxDisplayMode *m = &modes[i];
        if (m->refresh_millihz < 59000 || m->refresh_millihz > 60000) continue;
        unsigned rank = m->width == 1920 && m->height == 1080 ? 1 :
            (scale == 2 && m->width == 3840 && m->height == 2160 ? 2 : 0);
        if (rank && (rank > best || (rank == best && m->refresh_millihz > refresh))) {
            selected = i; best = rank; refresh = m->refresh_millihz;
        }
    }
    return selected;
}
#endif
