// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_routing.hpp"
#include "../avatar_editor/native/avatar_save_store.h"
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
namespace exbox::editor {
using rex::X_RESULT;
namespace {
std::span<const uint8_t> payload(mapped_void pointer, uint32_t size) {
    return pointer ? std::span<const uint8_t>(pointer.as<const uint8_t*>(), size)
                   : std::span<const uint8_t>{};
}
u32 complete_image(uint32_t result, uint32_t overlap) {
    if (!overlap) return result;
    rex::system::kernel_state()->CompleteOverlappedImmediate(overlap, result);
    return X_ERROR_IO_PENDING;
}
bool save_gamer_image(std::span<const uint8_t> bytes) {
    const auto name=gamer_image_destination(bytes);
    return !name.empty() && av2::native::persist_avatar_image(name.data(),bytes.data(),bytes.size());
}
}
u32 write_gamer_images(u32, u32, u32, u32, u32, u32,
                       mapped_void first, u32 first_size, mapped_void second,
                       u32 second_size, mapped_void overlap) {
    // Evaluate both in order, even if the first succeeds. Width picks the name;
    // two images of the same size class intentionally overwrite in this order.
    const bool first_saved=save_gamer_image(payload(first,first_size));
    const bool second_saved=save_gamer_image(payload(second,second_size));
    return complete_image(first_saved || second_saved ? X_ERROR_SUCCESS : X_ERROR_INVALID_PARAMETER,
                          overlap.guest_address());
}
u32 write_image_tile(u32 type, u32, u32, u32, mapped_void data, u32 size, mapped_void overlap) {
    const auto bytes=payload(data,size);
    const auto name=tile_image_destination(type,bytes);
    uint32_t result=X_ERROR_SUCCESS;
    if (!name.empty() && !av2::native::persist_avatar_image(name.data(),bytes.data(),bytes.size()))
        result=X_ERROR_FUNCTION_FAILED;
    // Nonmatching payloads are acknowledged without writing, as in reference.
    return complete_image(result,overlap.guest_address());
}
u32 acknowledge_gamer_tile(u32, u32, u32, u32, u32, u32 overlap) {
    return complete_image(X_ERROR_SUCCESS,overlap);
}
}
REX_HOOK(__wrap___imp__XamWriteGamerTileEx, exbox::editor::write_gamer_images)
REX_HOOK(__wrap___imp__XamWriteTile, exbox::editor::write_image_tile)
REX_HOOK(__wrap___imp__XamWriteGamerTile, exbox::editor::acknowledge_gamer_tile)
