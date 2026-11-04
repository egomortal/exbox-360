// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "xui_handles.hpp"
namespace exbox::editor {
// Preserve the title's unrelated flags; only opacity and visibility change.
// Reader/writer own guest-memory validation. This hook never consumes the
// original guest call, including on invalid input or an inaccessible widget.
template<class ReadWord,class WriteWord>
unsigned hide_identifying(uint32_t widget,ReadWord read,WriteWord write) {
    if(!widget || widget>UINT32_MAX-12U)return 0;
    unsigned changed=0;
    for(const uint32_t offset:{12U,8U}) {
        const auto handle=read(widget+offset);
        if(!handle)continue;
        const auto element=xui_element(*handle,read);
        if(!element || *element>UINT32_MAX-180U)continue;
        const auto flags=read(*element+180U);
        if(!flags)continue;
        // The adapter preflights both destinations before committing writes.
        if(write(*element+36U,0,*element+180U,*flags&~1U))++changed;
    }
    return changed;
}
}
