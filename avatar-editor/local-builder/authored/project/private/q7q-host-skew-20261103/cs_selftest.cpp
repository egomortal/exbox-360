// Run only inside the first main guest XThread, before title code executes.
// A fixed allocation must succeed before touching the requested address.
#include "ae_pch.h"
#include <rex/system/xthread.h>
#include <rex/system/kernel_state.h>
#include "boot_log.h"
#include <cstring>
extern "C" void __imp__RtlInitializeCriticalSection(PPCContext&,uint8_t*);
extern "C" void __imp__RtlEnterCriticalSection(PPCContext&,uint8_t*);
extern "C" void __imp__RtlLeaveCriticalSection(PPCContext&,uint8_t*);
extern "C" bool av2_q7q_cs_selftest(){
 using namespace rex::memory;
 constexpr uint32_t address=0xFFCAB000u;
 auto* thread=rex::system::XThread::GetCurrentThread();
 auto* memory=rex::system::kernel_memory();
 if(!thread||!memory)return false;
 auto* heap=memory->LookupHeap(address);
 if(!heap||!heap->AllocFixed(address,0x1000,0x1000,kMemoryAllocationReserve|kMemoryAllocationCommit,kMemoryProtectRead|kMemoryProtectWrite)){
  av2_boot_log("cs-selftest","cannot reserve requested page=%08x; existing guest bytes untouched",address);return false;
 }
 auto* base=memory->virtual_membase();
 auto* host=memory->TranslateVirtual(address);
 auto* marshalled=GuestPtr(base,address);
 bool passed=host==REX_RAW_ADDR(address)&&host==marshalled;
 if(passed){
  REX_STORE_U32(address,uint32_t(0x12345678));
  passed=rex::memory::load_and_swap<uint32_t>(host)==0x12345678;
 }
 if(passed){
  PPCContext ctx{};ctx.r3.u64=address;__imp__RtlInitializeCriticalSection(ctx,base);
  int32_t lock=0;std::memcpy(&lock,host+0x10,4);
  passed=lock==-1&&REX_LOAD_U32(address+0x14)==0&&REX_LOAD_U32(address+0x18)==0;
  if(passed){
   ctx.r3.u64=address;__imp__RtlEnterCriticalSection(ctx,base);
   std::memcpy(&lock,host+0x10,4);
   passed=lock==0&&REX_LOAD_U32(address+0x14)==1&&REX_LOAD_U32(address+0x18)==thread->guest_object();
   // Do not turn a detected failure into the RTL Leave ownership assertion.
   // A corrupt owner/count is diagnostic state: retain this owned test page
   // until process exit rather than freeing a potentially referenced object.
   if(REX_LOAD_U32(address+0x14)==0 || REX_LOAD_U32(address+0x18)!=thread->guest_object()){
    av2_boot_log("cs-selftest","FAIL invalid post-enter ownership/count; retain page=%08x for process exit",address);
    return false;
   }
   ctx.r3.u64=address;__imp__RtlLeaveCriticalSection(ctx,base);
   std::memcpy(&lock,host+0x10,4);
   passed=passed&&lock==-1&&REX_LOAD_U32(address+0x14)==0&&REX_LOAD_U32(address+0x18)==0;
  }
 }
 const bool released=heap->Release(address);
 av2_boot_log("cs-selftest","%s guest=%08x cpu=%p TranslateVirtual=%p GuestPtr=%p released=%d",passed&&released?"PASS":"FAIL",address,REX_RAW_ADDR(address),host,marshalled,int(released));
 return passed&&released;
}
