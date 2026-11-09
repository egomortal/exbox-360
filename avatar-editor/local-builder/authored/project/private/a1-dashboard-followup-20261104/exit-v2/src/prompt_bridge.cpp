#include "normal_exit_api.h"
#include "prompt_api.h"
#include "prompt_state.hpp"
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <atomic>
#include <mutex>
#ifdef A1_HOST_DIAGNOSTIC
#include <cstdio>
#endif
static const uint8_t* GuestDataPtrProbe(uint32_t addr, size_t bytes) {
  // The zero page is committed but fenced off, so anything in the first 64 KB reads as absent.
  if (addr < 0x10000 || !bytes) return nullptr;
  auto* mem = rex::system::kernel_memory();
  if (!mem) return nullptr;
  const auto readable = [&](uint32_t ea) {
    auto* heap = mem->LookupHeap(ea);
    rex::memory::HeapAllocationInfo info{};
    uint32_t size = 0, protect = 0;
    // A free page is rejected by its zero region size first: QueryRegionInfo would walk the whole free run.
    return heap && heap->QuerySize(ea, &size) && size && heap->QueryRegionInfo(ea, &info) &&
           (info.state & rex::memory::kMemoryAllocationCommit) && heap->QueryProtect(ea, &protect) &&
           (protect & rex::memory::kMemoryProtectRead);
  };
  const uint32_t last = addr + uint32_t(bytes - 1);
  if (last < addr || !readable(addr) || !readable(last)) return nullptr;
  return mem->TranslateVirtual<const uint8_t*>(addr);
}

namespace {
std::atomic<uint64_t> published{0},presented{0};
std::mutex state_mutex;
av2::back_prompt::State prompt;
}
extern "C" uint64_t av2_back_prompt_snapshot(){return published.load(std::memory_order_acquire);}
extern "C" void av2_back_prompt_presented(uint64_t s){if(s)presented.store(s,std::memory_order_release);}
extern "C" void __real_sub_9216A890(PPCContext&,uint8_t*);
extern "C" void sub_920EBD10(PPCContext&,uint8_t*);
extern "C" void sub_920C1E58(PPCContext&,uint8_t*);
extern "C" void __wrap_sub_9216A890(PPCContext&ctx,uint8_t*base){
 const auto*stroke=GuestDataPtrProbe(ctx.r3.u32,8);
 if(!stroke||uint32_t(ctx.lr)!=0x920B15E8u||stroke[6]!=0){__real_sub_9216A890(ctx,base);return;}
 const uint16_t key=(uint16_t(stroke[0])<<8)|stroke[1],flags=(uint16_t(stroke[4])<<8)|stroke[5];
 av2::back_prompt::Event event;uint64_t before=0,after=0;
 { std::lock_guard<std::mutex> lock(state_mutex);
 uint32_t state=0;
 if(prompt.snapshot()||(key==0x5801&&(flags&1)&&!(flags&6))){
  PPCContext query=ctx;query.r3.u64=0x922E7604u;sub_920EBD10(query,base);state=query.r3.u32;
 }
 before=prompt.snapshot();
 event=prompt.key(key,flags,state,presented.load(std::memory_order_acquire));
 after=prompt.snapshot();
 published.store(after,std::memory_order_release);
 } // Never retain the mutex across guest navigation or self-termination.
 if(!event.consumed){
#ifdef A1_HOST_DIAGNOSTIC
  if(before&&(flags&2))std::fprintf(stderr,"A1 MODAL paired-release key=%04x\n",key);
#endif
  __real_sub_9216A890(ctx,base);return;
 }
#ifdef A1_HOST_DIAGNOSTIC
 if(before!=after||event.action!=av2::back_prompt::Action::None)
  std::fprintf(stderr,"A1 MODAL key=%04x flags=%04x before=%llu after=%llu action=%u\n",key,flags,(unsigned long long)before,(unsigned long long)after,unsigned(event.action));
#endif
 ctx.r3.u64=0; // S_OK: consumed before XUI, so no underlying press/release leaks.
 if(event.action==av2::back_prompt::Action::Save){
  // Exact original root-Back navigation branch, called only after explicit Save.
  PPCContext save=ctx;save.r3.u64=0x922E5E90u;save.r4.u64=0;sub_920C1E58(save,base);
 }else if(event.action==av2::back_prompt::Action::Discard){
  // Same owned normal title completion used by XamLoaderLaunchTitleEx(NULL).
  // Never uses av2_boot_request_stop: frontend must run the P8 drain/save/ticket path.
  av2_normal_exit_route(4);
  rex::system::kernel_state()->TerminateTitle();
 }
}
