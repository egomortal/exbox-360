// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile_write.hpp"
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xtypes.h>
namespace exbox::editor {
using rex::X_RESULT;
u32 profile_write(u32, u32 user, u32 count, mapped_void settings, mapped_void overlap) {
    if (!count || !settings) return X_ERROR_INVALID_PARAMETER;
    auto* kernel = rex::system::kernel_state();
    uint32_t result = X_ERROR_NO_SUCH_USER;
    if (user == 0) {
        using namespace rex::system::xam;
        const auto* rows = static_cast<const X_USER_PROFILE_SETTING*>(settings.host_address());
        for (uint32_t i = 0; i < count; ++i) {
            const auto& row = rows[i];
            if (!profile_binary_write(row.data.type)) continue;
            const uint32_t address = row.data.binary.ptr;
            const uint32_t length = row.data.binary.size;
            const auto* source = address ? kernel->memory()->TranslateVirtual<const uint8_t*>(address) : nullptr;
            const auto payload = profile_payload({source, address ? length : 0U}, length, address != 0);
            kernel->user_profile()->AddSetting(std::make_unique<UserProfile::BinarySetting>(uint32_t(row.setting_id), payload));
        }
        result = X_ERROR_SUCCESS;
    }
    if (!overlap) return result;
    kernel->CompleteOverlappedImmediate(overlap.guest_address(), result);
    return X_ERROR_IO_PENDING;
}
}
REX_HOOK(__wrap___imp__XamUserWriteProfileSettings, exbox::editor::profile_write)
