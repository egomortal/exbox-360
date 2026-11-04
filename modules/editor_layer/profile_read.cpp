// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile_layout.hpp"
#include <array>
#include <cstring>
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xio.h>
#include <rex/system/xtypes.h>
// Explicit remaining reference dependency: avatar manifest hydration. The user
// read adapter is independent; this helper is not claimed replaced or retired.
namespace rex::kernel::xam { void EnsureAvatarProfileSetting(rex::system::xam::UserProfile*); }
namespace exbox::editor {
using rex::X_RESULT;
using rex::system::xam::UserProfile;
using rex::system::xam::X_USER_PROFILE_SETTING;
namespace {
u32 finish_read(uint32_t result, uint32_t overlap) {
    if (!overlap) return result;
    rex::system::kernel_state()->CompleteOverlappedImmediate(overlap, result);
    return X_ERROR_IO_PENDING;
}
u32 read_settings(u32 user, u32 xuid_count, mapped_u64 xuids, u32 count,
                  mapped_u32 ids, mapped_u32 size, u32 flags,
                  mapped_void output, mapped_void overlap) {
    auto* kernel = rex::system::kernel_state();
    auto* profile = kernel->user_profile();
    // Reference asserts these unsupported inputs. Reject them defensively.
    if (flags || xuid_count > 1 || (xuid_count == 0 && xuids) ||
        (xuid_count == 1 && (!xuids || uint64_t(*xuids) != profile->xuid())))
        return X_ERROR_INVALID_PARAMETER;
    if (count < 1 || count > 32 || !size || !ids) return X_ERROR_INVALID_PARAMETER;
    std::array<uint32_t, 32> keys{};
    const auto* input = static_cast<const rex::be<uint32_t>*>(ids.host_address());
    for (uint32_t i = 0; i < count; ++i) keys[i] = input[i];
    const auto available = uint32_t(*size);
    const auto plan = profile_buffer_plan(std::span(keys).first(count), true, available, bool(output));
    if (plan.status == ProfileBufferStatus::invalid_argument) return X_ERROR_INVALID_PARAMETER;
    if (plan.status == ProfileBufferStatus::insufficient_buffer) {
        if (plan.update_size) *size = plan.total_bytes;
        return X_ERROR_INSUFFICIENT_BUFFER;
    }
    if (!xuid_count && user) return finish_read(X_ERROR_NO_SUCH_USER, overlap.guest_address());
    rex::kernel::xam::EnsureAvatarProfileSetting(profile);
    // Validate the whole list before touching the caller's output buffer.
    for (uint32_t i = 0; i < count; ++i)
        if (!profile->GetSetting(keys[i])) return finish_read(X_ERROR_INVALID_PARAMETER, overlap.guest_address());
    auto* bytes = static_cast<uint8_t*>(output.host_address());
    auto* header = reinterpret_cast<rex::be<uint32_t>*>(bytes);
    header[0] = count; header[1] = output.guest_address() + 8;
    auto* rows = reinterpret_cast<X_USER_PROFILE_SETTING*>(bytes + 8);
    UserProfile::SettingByteStream stream(output.guest_address(), bytes, available, plan.header_bytes);
    for (uint32_t i = 0; i < count; ++i) {
        auto* setting = profile->GetSetting(keys[i]);
        auto& row = rows[i]; std::memset(&row, 0, sizeof(row));
        row.from = !setting->is_set ? 0 : setting->is_title_specific() ? 2 : 1;
        if (xuid_count) row.xuid = profile->xuid(); else row.user_index = user;
        row.setting_id = keys[i];
        if (setting->is_set) setting->Append(&row.data, &stream);
    }
    return finish_read(X_ERROR_SUCCESS, overlap.guest_address());
}
}
u32 profile_read(u32, u32 user, u32 xuid_count, mapped_u64 xuids, u32 count,
                 mapped_u32 ids, mapped_u32 size, mapped_void output, mapped_void overlap) {
    return read_settings(user, xuid_count, xuids, count, ids, size, 0, output, overlap);
}
u32 profile_read_ex(u32, u32 user, u32 xuid_count, mapped_u64 xuids, u32 count,
                    mapped_u32 ids, mapped_u32 size, u32 flags, mapped_void output, mapped_void overlap) {
    return read_settings(user, xuid_count, xuids, count, ids, size, flags, output, overlap);
}
}
REX_HOOK(__wrap___imp__XamUserReadProfileSettings, exbox::editor::profile_read)
REX_HOOK(__wrap___imp__XamUserReadProfileSettingsEx, exbox::editor::profile_read_ex)
