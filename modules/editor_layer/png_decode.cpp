// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/hook.h>
#include <rex/system/xtypes.h>
#include <cstdint>
#include <memory>
// Decoder symbols currently come from the linked reference object. This adapter
// replaces export policy only; codec ownership must move before object retirement.
extern "C" unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern "C" void stbi_image_free(void*);
namespace exbox::editor {
u32 decode_guest_image(mapped_void source, u32 length, mapped_void target, u32 capacity, u32, u32, u32) {
    if (!source || !target || length < 8) return 0x80070057u;
    int width=0, height=0, channels=0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> rgba(
        stbi_load_from_memory(source.as<const unsigned char*>(), static_cast<int>(length),
                              &width, &height, &channels, 4), &stbi_image_free);
    if (!rgba) return 0x80004005u;
    const uint64_t bytes = uint64_t(width) * uint64_t(height) * 4;
    if (bytes > capacity) return 0x8007007au;
    auto* output=target.as<unsigned char*>();
    // Exact byte permutation: no filtering, alpha multiplication, colour-space
    // conversion or resizing. Guest code expects big-endian ARGB here.
    for (uint64_t pixel=0;pixel<bytes;pixel+=4) {
        output[pixel]=rgba.get()[pixel+3];
        output[pixel+1]=rgba.get()[pixel];
        output[pixel+2]=rgba.get()[pixel+1];
        output[pixel+3]=rgba.get()[pixel+2];
    }
    return 0;
}
}
REX_HOOK(__wrap___imp__XamPngDecode, exbox::editor::decode_guest_image)
