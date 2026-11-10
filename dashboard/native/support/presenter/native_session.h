/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include "gpu_budget.h"
#ifdef __cplusplus
extern "C" {
#endif
/* One owner: open on startup, present serially, close after presenter joins.
 * Open: 1 ready, 0 failed and released, -1 ownership unknown.
 * Close: 1 released, 0 retain resources and request safe title exit. */
/* Set before opening.1=1080p default;2=4K with1080p fallback. */
int exbox_native_set_render_scale(unsigned scale);
int exbox_native_open(int* width,int* height);
/* GPU open defers CPU upload allocation; both routes share one ledger. */
int exbox_native_open_gpu(int* width,int* height);
int exbox_native_open_with_budget(int*,int*,ExboxGpuBudget*,int cpu_upload);
void* exbox_native_upload(size_t* bytes,int* pitch);
/* Serial owner, before GPU creation/preparation: refuses pending upload or
 * unknown completion. Invalidates old upload pointers; fallback reallocates. */
int exbox_native_retire_upload(void);
int exbox_native_present(void);
int exbox_native_close(void);
#ifdef __cplusplus
}
#endif
