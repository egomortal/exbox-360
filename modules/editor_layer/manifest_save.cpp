// SPDX-License-Identifier: GPL-3.0-or-later
#include "manifest_cache.hpp"
#include "../avatar_editor/native/avatar_save_store.h"
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
#include <cstring>
namespace exbox::editor {
using rex::X_RESULT;
u32 save_manifest(u32, u32 metadata_address, mapped_void overlap) {
    uint32_t result = X_ERROR_SUCCESS;
    if (metadata_address) {
        const auto* bytes = rex::system::kernel_memory()->TranslateVirtual<const uint8_t*>(metadata_address);
        // X_AVATAR_METADATA: body_component.asset_id occupies 16 bytes at
        // 0x120. The test asserts this against the pinned source layout.
        bool has_body = false;
        for (unsigned i = 0; i < 16; ++i) has_body |= bytes[0x120u + i] != 0;
        if (has_body) {
            std::memcpy(manifest_bytes(), bytes, sizeof(ManifestBytes));
            manifest_present() = true;
            // Readers must see the new cache even if persistence fails.
            if (!av2::native::persist_avatar_manifest(manifest_bytes(), sizeof(ManifestBytes), true))
                result = X_ERROR_FUNCTION_FAILED;
        }
    }
    if (!overlap) return result;
    rex::system::kernel_state()->CompleteOverlappedImmediate(overlap.guest_address(), result);
    return X_ERROR_IO_PENDING;
}
}
REX_HOOK(__wrap___imp__XamAvatarSetManifest, exbox::editor::save_manifest)
