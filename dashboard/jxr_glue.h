/* Ex-Box 360: the one jxrlib call the dashboard makes (PS5 build only).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NXE_JXR_GLUE_H
#define NXE_JXR_GLUE_H

#ifdef __cplusplus
extern "C" {
#endif

enum { NXE_JXR_OK = 0, NXE_JXR_ERROR = -1, NXE_JXR_FORMAT = -2 };

/* Decodes a JPEG XR file stored as 32bppRGB101010 (a PS5 HDR screenshot).
 * On NXE_JXR_OK, *pixels is malloc'd (width * height packed pixels; free it). */
int nxe_jxr_read_101010(const char *path, int *width, int *height, unsigned **pixels);

#ifdef __cplusplus
}
#endif

#endif
