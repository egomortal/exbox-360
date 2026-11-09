#include "ae_pch.h"
#include "wearable.h"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
extern "C" void sub_920BF260(PPCContext&,uint8_t*);
extern "C" void sub_92166DB0(PPCContext&,uint8_t*);
extern "C" void sub_920ECC18(PPCContext&,uint8_t*);
extern "C" void sub_920ECA30(PPCContext&,uint8_t*);
extern "C" void sub_920FC8A8(PPCContext&,uint8_t*);
static unsigned arms=0;
extern "C" void __imp__sub_920FC8A8(PPCContext& c,uint8_t*){++arms;c.r3.u64=123;errno=83;}
extern "C" bool av2_tryon_test_observe(PPCContext&,uint8_t*);
extern "C" bool av2_tryon_test_active();
extern "C" void av2_tryon_test_reset();
extern "C" void av2_tryon_test_fail_lock();
extern "C" bool av2_tryon_test_disabled();
constexpr uint32_t G=0x922e5e90,M=G+6004,W=G+1956,B=G+2956,L=0x7010fcd0,SP=0x10000;
static uint64_t ticks=1;static bool clock_ok=true,throw_apply=false,reenter=false;
static bool mutate_csr=false;
static unsigned clocks=0;
static unsigned applies=0,timers=0,transitions=0;static PPCContext timer_context{};
extern "C" bool av2_tryon_test_clock(uint64_t* out)noexcept{++clocks;errno=199;*out=ticks;return clock_ok;}
static void alter(PPCContext& c){auto*p=reinterpret_cast<unsigned char*>(&c);for(size_t i=0;i<sizeof(c);++i)p[i]^=0x5a;}
extern "C" void __imp__sub_920BF260(PPCContext& c,uint8_t* base){
 ++applies;assert(!av2_tryon_test_active());
 if(reenter){PPCContext t{};t.r3.u64=M;sub_920ECA30(t,base);}
 if(mutate_csr){const auto before=c.fpscr.getcsr();c.fpscr.setcsr(before^PPCFPSCRRegister::GuestMask);assert(c.fpscr.getcsr()!=before);}
 alter(c);errno=78;if(throw_apply)throw std::runtime_error("original");
}
extern "C" void __imp__sub_92166DB0(PPCContext& c,uint8_t*){++timers;alter(c);if(mutate_csr)c.fpscr.csr=0;std::memcpy(&timer_context,&c,sizeof(c));errno=77;}
extern "C" void __imp__sub_920ECC18(PPCContext& c,uint8_t*){assert(!av2_tryon_test_active());++transitions;alter(c);errno=81;}
extern "C" void __imp__sub_920ECA30(PPCContext& c,uint8_t*){assert(!av2_tryon_test_active());++transitions;alter(c);errno=82;}
static void be(uint8_t*base,uint32_t a,uint32_t v){base[a]=v>>24;base[a+1]=v>>16;base[a+2]=v>>8;base[a+3]=v;}
static void seed(PPCContext& c){std::memset(&c,0x31,sizeof(c));c.r1.u64=SP;c.r3.u64=G;c.r4.u64=W;c.r5.u64=0;c.r31.u64=L;c.lr=0x920DB2E8;}
static void pump(PPCContext& c,uint8_t* base){seed(c);c.lr=0x920ADCCC;sub_92166DB0(c,base);assert(!std::memcmp(&c,&timer_context,sizeof(c)));assert(errno==77);}
static void focus(PPCContext& c,uint8_t* base){seed(c);c.r3.u64=0x40000+1640;c.lr=0x920D8D8C;sub_920FC8A8(c,base);seed(c);}
static void reset(PPCContext& c,uint8_t* base){av2_tryon_test_reset();ticks=1;mutate_csr=false;clock_ok=true;throw_apply=false;reenter=false;applies=timers=transitions=0;std::memset(base+W,0,2000);base[B+0x160]=1;base[B+0x171]=8;std::memcpy(base+W,base+B,1000);base[W+0x160]=2;be(base,M+37092,0x12345678);be(base,L+24,0);be(base,SP+104,0x920DC3C8);be(base,SP,SP+112);be(base,SP+112+1112,0x920D8AC4);be(base,SP+96,0);be(base,SP+100,0x40000+1648);be(base,M+37100,0x40000);pump(c,base);focus(c,base);seed(c);}
static void queue(PPCContext& c,uint8_t* base){seed(c);errno=66;assert(av2_tryon_test_observe(c,base));assert(errno==66 && av2_tryon_test_active());}
static void extra_tests(PPCContext&,uint8_t*);
int main(){
 auto*base=static_cast<uint8_t*>(mmap(nullptr,size_t(1)<<32,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0));assert(base!=MAP_FAILED);PPCContext c{};
 // Exact deadline; complete post-original timer context and errno restoration.
 reset(c,base);queue(c,base);ticks+=124999999;pump(c,base);assert(applies==0);++ticks;pump(c,base);assert(applies==1);pump(c,base);assert(applies==1);
 // Latest metadata restarts dwell; unchanged metadata does not.
 reset(c,base);queue(c,base);ticks+=100000000;focus(c,base);base[W+0x160]=3;queue(c,base);ticks+=124999999;pump(c,base);assert(!applies);++ticks;pump(c,base);assert(applies==1);
 reset(c,base);queue(c,base);ticks+=100000000;queue(c,base);ticks+=25000000;pump(c,base);assert(applies==1);
 // Pending detaches before original call and no host lock spans reentrant guest calls.
 reset(c,base);queue(c,base);ticks+=125000000;reenter=true;pump(c,base);assert(applies==1 && transitions==1);
 // Full actual PPCContext and timer errno survive original apply exception.
 reset(c,base);queue(c,base);ticks+=125000000;throw_apply=true;seed(c);c.lr=0x920ADCCC;bool threw=false;try{sub_92166DB0(c,base);}catch(const std::runtime_error&){threw=true;}assert(threw && applies==1 && !av2_tryon_test_active() && errno==77 && !std::memcmp(&c,&timer_context,sizeof(c)));
 // Stop, working drift, baseline drift, scene change and execution identity cancel.
 for(int mode=0;mode<5;++mode){reset(c,base);queue(c,base);ticks+=125000000;if(mode==0)be(base,L+24,1);if(mode==1)base[W+0x160]=3;if(mode==2)base[B+0x160]=4;if(mode==3)be(base,M+37092,9);if(mode==4){PPCContext other{};pump(other,base);}else pump(c,base);assert(!applies && !av2_tryon_test_active());}
 // Transition invalidation precedes original guest delegate, even same scene token.
 for(int mode=0;mode<2;++mode){reset(c,base);queue(c,base);seed(c);c.r3.u64=M;PPCContext expected;std::memcpy(&expected,&c,sizeof(c));alter(expected);if(mode)sub_920ECC18(c,base);else sub_920ECA30(c,base);assert(!std::memcmp(&c,&expected,sizeof(c)) && errno==(mode?81:82));ticks+=125000000;pump(c,base);assert(!applies && transitions==1);}
 // Clock error cancels; backward time restarts wait, cannot fire immediately.
 reset(c,base);queue(c,base);clock_ok=false;pump(c,base);assert(!applies && !av2_tryon_test_active());
 reset(c,base);ticks=500000000;focus(c,base);queue(c,base);ticks=2;pump(c,base);assert(!applies && !av2_tryon_test_active());focus(c,base);queue(c,base);ticks+=125000000;pump(c,base);assert(applies==1);
 // Excluded entries cancel and original delegates exactly once.
 for(int mode=0;mode<5;++mode){reset(c,base);queue(c,base);seed(c);if(mode==0)c.r5.u64=1;if(mode==1)c.lr=5;if(mode==2)c.r3.u64=3;if(mode==3)base[W]=1;if(mode==4)be(base,SP+104,4);PPCContext expected;std::memcpy(&expected,&c,sizeof(c));alter(expected);sub_920BF260(c,base);assert(applies==1 && !av2_tryon_test_active() && errno==78 && !std::memcmp(&c,&expected,sizeof(c)));}
 // Eligibility classifier rejects unknown fields, nonwearable categories, mismatched input.
 for(int mode=0;mode<4;++mode){reset(c,base);seed(c);if(mode==0)base[W+0x172]=1;if(mode==1)base[W+0x171]=4;if(mode==2)c.r4.u64=B;if(mode==3)be(base,SP+104,4);assert(!av2_tryon_test_observe(c,base));}
 // Actual public entry: both proven stack chains preserve every context byte.
 for(int chain=0;chain<2;++chain){reset(c,base);if(chain){be(base,SP+112+1112,0x920DC5D4);be(base,SP+112,SP+112+1120);be(base,SP+112+1120+120,0x920D8AC4);}seed(c);PPCContext original;std::memcpy(&original,&c,sizeof(c));errno=91;sub_920BF260(c,base);assert(!applies && av2_tryon_test_active() && errno==91 && !std::memcmp(&c,&original,sizeof(c)));ticks+=100000000;sub_920BF260(c,base);assert(!applies);ticks+=25000000;pump(c,base);assert(applies==1);}
 // Public latest focus restarts deadline; a pending Save always applies immediately.
 reset(c,base);seed(c);sub_920BF260(c,base);ticks+=100000000;focus(c,base);base[W+0x160]=3;seed(c);sub_920BF260(c,base);ticks+=124999999;pump(c,base);assert(!applies);++ticks;pump(c,base);assert(applies==1);
 reset(c,base);seed(c);sub_920BF260(c,base);assert(!applies);seed(c);c.lr=0x920BC000;sub_920BF260(c,base);assert(applies==1 && !av2_tryon_test_active());
 // Proven activation/transition consumers must never defer, even pending.
 for(uint32_t pc:{0x920DC0Fu,0x920DC0F0u,0x920D8EBCu}){reset(c,base);seed(c);sub_920BF260(c,base);assert(!applies && av2_tryon_test_active());be(base,SP+112+1112,pc);seed(c);sub_920BF260(c,base);assert(applies==1 && !av2_tryon_test_active());}
 // Unknown/unaligned/inconsistent/overflow stack chains fail open.
 for(int mode=0;mode<6;++mode){reset(c,base);seed(c);if(mode==0)be(base,SP+112+1112,0xdeadbeef);if(mode==1)c.r1.u64=SP+1;if(mode==2)be(base,SP,SP+128);if(mode==3)c.r1.u64=0xfffffff0;if(mode==4){be(base,SP+112+1112,0x920DC5D4);be(base,SP+112,SP+112+1136);}if(mode==5){be(base,SP+112+1112,0x920DC5D4);be(base,SP+112,SP+112+1120);be(base,SP+112+1120+120,0xdeadbeef);}sub_920BF260(c,base);assert(applies==1 && !av2_tryon_test_active());}
 // Idle pumps must not read guest memory or query clock.
 reset(c,base);av2_tryon_test_reset();const auto clocks_before=clocks;pump(c,nullptr);assert(clocks==clocks_before);
 reset(c,base);queue(c,base);be(base,L+24,1);const auto stopped_clocks=clocks;pump(c,base);assert(clocks==stopped_clocks && !av2_tryon_test_active());
 // Injected bookkeeping failure disables pump; original delegates still run.
 reset(c,base);queue(c,base);ticks+=125000000;av2_tryon_test_fail_lock();pump(c,base);assert(!applies && av2_tryon_test_disabled());pump(c,nullptr);assert(!applies);
 seed(c);sub_920BF260(c,base);assert(applies==1 && errno==78);
 extra_tests(c,base);
 munmap(base,size_t(1)<<32);
}








static void extra_tests(PPCContext& c,uint8_t* base){
 for(int throwing=0;throwing<2;++throwing){
  reset(c,base);queue(c,base);ticks+=125000000;mutate_csr=true;throw_apply=throwing;
  const uint32_t before=c.fpscr.getcsr();seed(c);c.lr=0x920ADCCC;bool threw=false;
  try{sub_92166DB0(c,base);}catch(const std::runtime_error&){threw=true;}
  assert(threw==bool(throwing) && applies==1 && c.fpscr.getcsr()==before && c.fpscr.csr==0 && errno==77 && !std::memcmp(&c,&timer_context,sizeof(c)));
 }
 reset(c,base);queue(c,base);ticks+=124000000;focus(c,base);assert(!av2_tryon_test_active());ticks+=1000000;pump(c,base);assert(!applies);base[W+0x160]=3;queue(c,base);ticks+=123999999;pump(c,base);assert(!applies);++ticks;pump(c,base);assert(applies==1);
 reset(c,base);ticks+=300000000;seed(c);sub_920BF260(c,base);assert(applies==1 && !av2_tryon_test_active());
 for(int mode=0;mode<4;++mode){reset(c,base);seed(c);if(mode==0)be(base,SP+100,0x40000+1656);if(mode==1)be(base,M+37100,0x40010);if(mode==2)ticks=0;if(mode==3){PPCContext other{};seed(other);sub_920BF260(other,base);assert(applies==1);continue;}sub_920BF260(c,base);assert(applies==1 && !av2_tryon_test_active());}
 reset(c,base);queue(c,base);const auto n=arms;seed(c);c.lr=0x12345678;sub_920FC8A8(c,base);assert(arms==n+1 && av2_tryon_test_active() && errno==83);ticks+=125000000;pump(c,base);assert(applies==1);
 reset(c,base);be(base,L+24,1);const auto nclock=clocks;pump(c,base);assert(clocks==nclock);be(base,L+24,0);seed(c);sub_920BF260(c,base);assert(applies==1);
 reset(c,base);be(base,L+24,1);focus(c,base);be(base,L+24,0);seed(c);sub_920BF260(c,base);assert(applies==1);
}
