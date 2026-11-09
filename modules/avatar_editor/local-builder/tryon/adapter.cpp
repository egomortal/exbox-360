// Standalone 890 child: only proven clothing browse calls may defer.
#include "ae_pch.h"
#include "wearable.h"
#include <cerrno>
#include <cstring>
#include <mutex>
#include <atomic>
#include <type_traits>
#include <time.h>
extern "C" void __imp__sub_920BF260(PPCContext&,uint8_t*);
extern "C" void __imp__sub_92166DB0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_920ECC18(PPCContext&,uint8_t*);
extern "C" void __imp__sub_920ECA30(PPCContext&,uint8_t*);
#ifdef AV2_TRYON_TEST
extern "C" bool av2_tryon_test_clock(uint64_t*) noexcept;
#endif
namespace {
using namespace closet_tryon;
constexpr uint32_t global=0x922e5e90,manager=global+6004;
constexpr uint32_t working=global+1956,baseline=global+2956;
static_assert(std::is_trivially_copyable_v<PPCContext>);
std::atomic<bool> disabled{false};
std::mutex mutex;
#ifdef AV2_TRYON_TEST
std::atomic<bool> fail_lock{false};
#endif
std::unique_lock<std::mutex> acquire() {
#ifdef AV2_TRYON_TEST
 if(fail_lock.exchange(false))throw 19;
#endif
 return std::unique_lock<std::mutex>(mutex);
}
Pending pending;
uint64_t epoch=1;
uint32_t last_loop=0;
uintptr_t last_execution=0;
struct Arm {Scope scope{};uint32_t owner=0;uint64_t time=0;bool valid=false;} arm;
struct Errno {int value=errno;~Errno(){errno=value;}};
struct ContextRestore {
 PPCContext& ctx;uint32_t hardware_csr;unsigned char bytes[sizeof(PPCContext)];
 explicit ContextRestore(PPCContext& c):ctx(c),hardware_csr(c.fpscr.getcsr()){std::memcpy(bytes,&c,sizeof(c));}
 ~ContextRestore(){std::memcpy(&ctx,bytes,sizeof(ctx));ctx.fpscr.setcsr(hardware_csr);}
};
bool now(uint64_t& out) noexcept {
 Errno preserve;
#ifdef AV2_TRYON_TEST
 return av2_tryon_test_clock(&out);
#else
 timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t) || t.tv_sec<0 ||
   t.tv_nsec<0 || t.tv_nsec>=1000000000)return false;
 if(uint64_t(t.tv_sec)>(UINT64_MAX-uint64_t(t.tv_nsec))/1000000000)return false;
 out=uint64_t(t.tv_sec)*1000000000+uint64_t(t.tv_nsec);return true;
#endif
}
Metadata copy(uint8_t* base,uint32_t at){Metadata m;std::memcpy(m.data(),base+at,m.size());return m;}
Scope scope(PPCContext& ctx,uint8_t* base) {
 return {global,last_loop,REX_LOAD_U32(manager+37092),
         uint64_t(reinterpret_cast<uintptr_t>(&ctx)),epoch};
}

bool ignored_return_chain(PPCContext& ctx,uint8_t* base) {
 const uint32_t sp=ctx.r1.u32;
 if((sp&15) || sp>UINT32_MAX-112)return false;
 if(REX_LOAD_U32(sp+104)!=0x920DC3C8)return false;
 const uint32_t frame=sp+112;
 if(REX_LOAD_U32(sp)!=frame || frame>UINT32_MAX-1120)return false;
 const auto ignored=[](uint32_t pc){return pc==0x920D8AC4;};
 const uint32_t caller=REX_LOAD_U32(frame+1112);
 if(ignored(caller))return true;
 if(caller!=0x920DC5D4)return false;
 const uint32_t outer=frame+1120;
 if(REX_LOAD_U32(frame)!=outer || outer>UINT32_MAX-128)return false;
 return ignored(REX_LOAD_U32(outer+120));
}
// Host lock only; no guest function is invoked here.
bool observe(PPCContext& ctx,uint8_t* base) {
 if(disabled.load() || ctx.r3.u32!=global || ctx.r5.u32 || ctx.lr!=0x920DB2E8 ||
    last_execution!=reinterpret_cast<uintptr_t>(&ctx) || !last_loop ||
    !ignored_return_chain(ctx,base)) {
  pending.cancel();arm.valid=false;return false;
 }
 const auto target=copy(base,working),applied=copy(base,baseline);
 if(!ctx.r4.u32 || copy(base,ctx.r4.u32)!=target || !wearable_only(applied,target)) {
  pending.cancel();arm.valid=false;return false;
 }
  uint64_t time;const auto current_scope=scope(ctx,base);
 // S+96 is DB2A8's saved caller r31, proven DC340 owner+1648.
 if(!arm.valid || !(arm.scope==current_scope) ||
    arm.owner>UINT32_MAX-1648 || REX_LOAD_U32(manager+37100)!=arm.owner ||
    REX_LOAD_U64(ctx.r1.u32+96)!=uint64_t(arm.owner)+1648 ||
    !now(time) || time<arm.time || time-arm.time>=Pending::delay_ns) {
  pending.cancel();arm.valid=false;return false;
 }
 return pending.observe(current_scope,target,applied,arm.time,true);
}
void transition(PPCContext& ctx) noexcept {
 if(ctx.r3.u32!=manager)return;
 Errno preserve;try {auto lock=acquire();pending.cancel();arm.valid=false;
 // Exhaustion permanently refuses admission rather than reusing an epoch.
 if(epoch==UINT64_MAX || epoch==0)epoch=0;else ++epoch;
 last_loop=0;last_execution=0;
 }catch(...){disabled.store(true);}
}
}
extern "C" void sub_920BF260(PPCContext& ctx,uint8_t* base) {
 bool defer=false;
 {
  Errno preserve;
  try {auto lock=acquire();defer=observe(ctx,base);}
  catch(...){disabled.store(true);}
 }
 // Proven outer callers overwrite r3. Leave the complete context untouched.
 if(defer)return;
 __imp__sub_920BF260(ctx,base);
}
extern "C" void sub_92166DB0(PPCContext& ctx,uint8_t* base) {
 const auto caller=ctx.lr;const uint32_t loop=ctx.r31.u32;
 __imp__sub_92166DB0(ctx,base);
 if(caller!=0x920ADCCC || disabled.load())return;
 Errno preserve;bool fire=false;
 try {
  auto lock=acquire();
  const auto execution=reinterpret_cast<uintptr_t>(&ctx);
  if(last_loop!=loop || last_execution!=execution){pending.cancel();arm.valid=false;}
  last_loop=loop;last_execution=execution;
  if(!pending.active() && !arm.valid)return;
  if(!loop || REX_LOAD_U32(loop+24)!=0){pending.cancel();arm.valid=false;return;}
  if(!pending.active())return;
  uint64_t time;
  if(!now(time) || !arm.valid || time<arm.time ||
     !(arm.scope==scope(ctx,base)) || REX_LOAD_U32(manager+37100)!=arm.owner)
   {pending.cancel();arm.valid=false;return;}
  const auto target=copy(base,working),applied=copy(base,baseline);
  fire=pending.take(scope(ctx,base),target,applied,time,
                    wearable_only(applied,target));
  if(!pending.active())arm.valid=false;
 }catch(...){disabled.store(true);return;}
 if(fire) {
  ContextRestore restore(ctx);
  ctx.r3.u64=global;ctx.r4.u64=working;ctx.r5.u64=0;
  __imp__sub_920BF260(ctx,base);
 }
}
extern "C" void sub_920ECC18(PPCContext& ctx,uint8_t* base) {
 transition(ctx);__imp__sub_920ECC18(ctx,base);
}
extern "C" void sub_920ECA30(PPCContext& ctx,uint8_t* base) {
 transition(ctx);__imp__sub_920ECA30(ctx,base);
}
#ifdef AV2_TRYON_TEST
extern "C" bool av2_tryon_test_observe(PPCContext& ctx,uint8_t* base) {
 Errno preserve;auto lock=acquire();return observe(ctx,base);
}
extern "C" void av2_tryon_test_fail_lock(){fail_lock.store(true);}
extern "C" bool av2_tryon_test_disabled(){return disabled.load();}
extern "C" bool av2_tryon_test_active(){auto lock=acquire();return pending.active();}
extern "C" void av2_tryon_test_reset(){disabled.store(false);auto lock=acquire();pending.cancel();arm={};epoch=1;last_loop=0;last_execution=0;}
#endif






extern "C" void __imp__sub_920FC8A8(PPCContext&,uint8_t*);
extern "C" void sub_920FC8A8(PPCContext& ctx,uint8_t* base) {
 if(ctx.lr==0x920D8D8C) {
  Errno preserve;
    try {
   auto lock=acquire();pending.cancel();arm.valid=false;
   const uint32_t pointer=ctx.r3.u32;
   if(!disabled.load() && pointer>=1640 && pointer-1640<=UINT32_MAX-1648 &&
      last_loop && last_execution==reinterpret_cast<uintptr_t>(&ctx) && REX_LOAD_U32(last_loop+24)==0) {
    const uint32_t owner=pointer-1640;const auto current_scope=scope(ctx,base);
    uint64_t time;
    if(current_scope && REX_LOAD_U32(manager+37100)==owner && now(time))
     arm={current_scope,owner,time,true};
   }
  }catch(...){disabled.store(true);}
 }
 __imp__sub_920FC8A8(ctx,base);
}


