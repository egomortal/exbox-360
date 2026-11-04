// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
namespace exbox::editor {
// Probe every 4KiB boundary and the final byte, including sub-page ranges.
// The callback verifies allocation/protection; this policy handles arithmetic.
template<class Probe>
bool xui_range_allowed(uint32_t address,std::size_t length,Probe probe) {
    if(address<65536 || !length || length>65536 || length-1>UINT32_MAX-address)return false;
    const uint32_t last=address+static_cast<uint32_t>(length-1);
    uint32_t at=address;
    for(;;) {
        if(!probe(at))return false;
        if(at==last)return true;
        const uint64_t next=(uint64_t(at)|4095U)+1;
        at=next>last?last:static_cast<uint32_t>(next);
    }
}
}
