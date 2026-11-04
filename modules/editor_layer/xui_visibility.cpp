// SPDX-License-Identifier: GPL-3.0-or-later
#include "xui_visibility.hpp"
#include "xui_memory.hpp"
#include <rex/ppc/context.h>
namespace av2 { void note_title_hook(const char*); }
extern "C" bool __wrap__Z21AE_HideIdentifyingGrpRN3rex3ppc8RegisterE(PPCRegister& widget) {
    av2::note_title_hook("AE_HideIdentifyingGrp");
    using Memory=exbox::editor::XuiMemory;
    exbox::editor::hide_identifying(widget.u32,Memory::word,
      [](uint32_t a,uint32_t av,uint32_t b,uint32_t bv){
        auto first=Memory::range(a,4,rex::memory::kMemoryProtectWrite);
        auto second=Memory::range(b,4,rex::memory::kMemoryProtectWrite);
        if(first.empty() || second.empty())return false;
        Memory::put(first,av);Memory::put(second,bv);return true;
      });
    return false;
}
