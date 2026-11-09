// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace exbox::editor {
template<class Services>
uint32_t open_notifications(Services& services,uint64_t areas,uint32_t system,uint32_t version) {
    const auto handle=services.create(areas,system,version);
    if (handle && (areas==0 || (areas&2)!=0)) {
        // Local guest compatibility only; no host networking is performed.
        services.emit(0x02000001,0x001510f0);
        services.emit(10,1);
    }
    return handle;
}
}
