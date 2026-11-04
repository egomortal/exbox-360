// SPDX-License-Identifier: GPL-3.0-or-later
#include "xui_heading.hpp"
#include "xui_text.hpp"
#include "xui_memory.hpp"
#include <rex/ppc/context.h>
#include <atomic>
namespace av2 {void note_title_hook(const char*);}
namespace rex::kernel::xam {int GetAvatarCatalogSearchScope();}
namespace ae_search {
int GetSearchStatusLine(char*,int);
void SetSearchLabelPossessed(bool);
enum class StorePage {kNone,kGamesList,kItems};
StorePage CurrentStorePage();
}
extern std::atomic<uint32_t> g_aeFrame;
namespace {
exbox::editor::HeadingTracker headings;
uint32_t scratch=0;
bool possessed=false;
using Memory=exbox::editor::XuiMemory;
void possession(bool value){if(possessed!=value){possessed=value;ae_search::SetSearchLabelPossessed(value);}}
}
extern "C" void __wrap__Z18AE_SniffXuiSetTextRN3rex3ppc8RegisterES2_(PPCRegister& handle,PPCRegister& message) {
    av2::note_title_hook("AE_SniffXuiSetText");
    const uint32_t msg=message.u32;
    if(!msg || msg>UINT32_MAX-16)return;
    const auto id=Memory::word(msg+4),params=Memory::word(msg+16);
    if(!id || !params || !*params || *params>UINT32_MAX-12)return;
    const auto text=Memory::word(*params+12);
    if(!text)return;
    const auto bytes=Memory::range(*text,130,rex::memory::kMemoryProtectRead);
    const auto label=exbox::editor::heading_text(bytes);
    if(label.empty() || label.starts_with("Page ") || *id!=2016)return;
    if(!headings.observe(handle.u32,g_aeFrame.load(std::memory_order_relaxed),label))return;
    char buffer[96]{};
    const int reported=ae_search::GetSearchStatusLine(buffer,sizeof(buffer));
    std::string replacement;
    if(reported>0)replacement.assign(buffer,std::min(reported,95));
    else if(rex::kernel::xam::GetAvatarCatalogSearchScope()>=0 || ae_search::CurrentStorePage()!=ae_search::StorePage::kNone)
        replacement=label+" (Ctrl+F to search)";
    if(replacement.empty()){possession(false);return;}
    if(!scratch)scratch=rex::system::kernel_memory()->SystemHeapAlloc(512);
    auto output=Memory::range(scratch,254,rex::memory::kMemoryProtectWrite);
    auto pointer=Memory::range(*params+12,4,rex::memory::kMemoryProtectWrite);
    if(output.empty() || pointer.empty())return;
    exbox::editor::encode_heading(replacement,output);
    Memory::put(pointer,scratch);possession(true);
}
extern "C" void __real__Z15AE_XuiFrameTickv();
extern "C" void __wrap__Z15AE_XuiFrameTickv() {
    __real__Z15AE_XuiFrameTickv();
    // Until the full frame controller is replaced, keep our possession lifecycle
    // synchronized at the same post-tick point as the reference UI.
    char status[96]{};
    if(ae_search::GetSearchStatusLine(status,sizeof(status))<=0 &&
       rex::kernel::xam::GetAvatarCatalogSearchScope()<0)possession(false);
}
