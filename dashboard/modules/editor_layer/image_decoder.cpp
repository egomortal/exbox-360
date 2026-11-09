// SPDX-License-Identifier: GPL-3.0-or-later
// Keep the exact pinned decoder and original enabled formats. Static linkage
// confines its other entry points; only these two compatibility calls escape.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define stbi_load_from_memory exbox_pinned_load
#define stbi_image_free exbox_pinned_free
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "../../third_party/stb-image-2.28/stb_image.h"
#pragma clang diagnostic pop
#undef stbi_load_from_memory
#undef stbi_image_free
// Linker wrapping permits paired testing before the reference object is removed.
// No resizing, conversion policy or allocator hook is added here.
extern "C" unsigned char* __wrap_stbi_load_from_memory(
    const unsigned char* data, int length, int* width, int* height,
    int* channels, int requested_channels) {
    return exbox_pinned_load(data,length,width,height,channels,requested_channels);
}
extern "C" void __wrap_stbi_image_free(void* pixels) {
    exbox_pinned_free(pixels);
}
