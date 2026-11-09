// SPDX-License-Identifier: GPL-3.0-or-later
#include "offline_hive.hpp"
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
// Remaining reference dependency: endpoint parsing only. None of the network
// fetch/install helpers declared by this header are called by this adapter.
#include "../../private/av2/title-port-source/kernel/xam/marketplace.h"
namespace exbox::editor {
using rex::X_RESULT;
namespace {
u32 complete_hive(HiveStatus status, mapped_void overlap) {
    uint32_t result = X_ERROR_INVALID_PARAMETER;
    switch (status) {
        case HiveStatus::success: result = X_ERROR_SUCCESS; break;
        case HiveStatus::not_found: result = X_ERROR_NOT_FOUND; break;
        case HiveStatus::insufficient_buffer: result = X_ERROR_INSUFFICIENT_BUFFER; break;
        case HiveStatus::invalid_span: break;
    }
    if (!overlap) return result;
    rex::system::kernel_state()->CompleteOverlappedImmediate(overlap.guest_address(), result);
    return X_ERROR_IO_PENDING;
}
}
u32 hive_narrow(mapped_string name, mapped_void output, u32 capacity, u32, mapped_void overlap) {
    const std::string_view key = name ? std::string_view(name.value()) : std::string_view();
    std::string host; uint16_t port = 0;
    if (key == "CatalogUriRoot" || key == "CatalogCDNUriRoot" ||
        key == "CatalogUriPort" || key == "CatalogCDNUriPort") {
        const auto endpoint = rex::kernel::xam::GetMarketplaceServer();
        host = endpoint.host; port = endpoint.port;
    }
    const auto value = offline_hive_value(key, host, port);
    const auto status = copy_hive_value(value, bool(output), capacity,
        {static_cast<uint8_t*>(output.host_address()), output ? size_t(capacity) : 0});
    return complete_hive(status, overlap);
}
u32 hive_wide(mapped_void, mapped_void, u32, u32, mapped_void overlap) {
    // No wide service configuration exists in the permanently offline mode.
    // Leave the supplied buffer untouched, even for a known online key.
    return complete_hive(HiveStatus::not_found, overlap);
}
}
REX_HOOK(__wrap___imp__XamGetLiveHiveValueA, exbox::editor::hive_narrow)
REX_HOOK(__wrap___imp__XamGetLiveHiveValueW, exbox::editor::hive_wide)
