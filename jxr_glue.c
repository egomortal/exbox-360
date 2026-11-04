/* Ex-Box 360: read a PS5 HDR screenshot (JPEG XR) with Microsoft's jxrlib.
 * Built only into the PS5 dashboard (NXE_JXR); see hdr_image.cpp for the colour.
 * SPDX-License-Identifier: GPL-3.0-or-later (jxrlib itself: BSD-2-Clause) */
#include "jxr_glue.h"

#include <JXRGlue.h>
#include <stdlib.h>
#include <string.h>

int nxe_jxr_read_101010(const char *path, int *width, int *height, unsigned **pixels)
{
    PKFactory *factory = NULL;
    PKCodecFactory *codecs = NULL;
    PKImageDecode *decoder = NULL;
    PKPixelFormatGUID format;
    PKRect all;
    I32 w = 0, h = 0;
    unsigned *buffer = NULL;
    int result = NXE_JXR_ERROR;

    *width = *height = 0;
    *pixels = NULL;
    if (Failed(PKCreateFactory(&factory, PK_SDK_VERSION))) goto done;
    if (Failed(PKCreateCodecFactory(&codecs, WMP_SDK_VERSION))) goto done;
    if (Failed(codecs->CreateDecoderFromFile(path, &decoder))) goto done;
    if (Failed(decoder->GetSize(decoder, &w, &h)) || w <= 0 || h <= 0 || w > 8192 || h > 8192) goto done;
    if (Failed(decoder->GetPixelFormat(decoder, &format))) goto done;
    /* PS5 HDR captures are HDR10 packed 10:10:10; anything else is not handled here. */
    if (memcmp(&format, &GUID_PKPixelFormat32bppRGB101010, sizeof format) != 0) {
        result = NXE_JXR_FORMAT;
        goto done;
    }
    buffer = (unsigned *)malloc((size_t)w * (size_t)h * 4u);
    if (!buffer) goto done;
    all.X = 0;
    all.Y = 0;
    all.Width = w;
    all.Height = h;
    if (Failed(decoder->Copy(decoder, &all, (U8 *)buffer, (U32)w * 4u))) goto done;
    *width = w;
    *height = h;
    *pixels = buffer;
    buffer = NULL;
    result = NXE_JXR_OK;
done:
    free(buffer);
    if (decoder) decoder->Release(&decoder);
    if (codecs) codecs->Release(&codecs);
    if (factory) factory->Release(&factory);
    return result;
}
