#include "guest_memory_bridge.h"
#include "boot_log.h"
#include "../platform/guest_memory.h"
#include <cerrno>
#include <time.h>
namespace {
av2::platform::GuestMemory memory;
uintptr_t base=0;
using M=av2::platform::GuestMemory;
uint64_t microseconds(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000+uint64_t(t.tv_nsec)/1000;}
// Retain the existing GuestMemory primitive's below-1-GiB safety envelope.
// This is direct backing only; no dashboard allocator or flexible budget changes.
constexpr size_t backing_limit=(1ULL<<30)-M::host_page;
uint8_t flags(unsigned permissions) {
 return M::committed | ((permissions&1)?M::read:0) | ((permissions&2)?M::write:0) |
        (permissions?0:M::guard);
}
}
static bool guest_boundary_selftest();
extern "C" void* av2_guest_reserve() {
 if(base)return nullptr;
 const int result=memory.reserve(backing_limit);
 if(result){av2_boot_log("memory","guest reserve failed errno=%d",result);return nullptr;}
 base=reinterpret_cast<uintptr_t>(memory.translate(0));
 if(base & UINT64_C(0xffffffff)) {
   av2_boot_log("memory","startup alignment assertion failed base=%p expected=4294967296",reinterpret_cast<void*>(base));
   (void)memory.release();base=0;return nullptr;
 }
 av2_boot_log("memory","startup alignment assertion PASS base=%p alignment=4294967296 span=%llu",reinterpret_cast<void*>(base),static_cast<unsigned long long>(M::span));
 av2_boot_memory("reserve-guest",reinterpret_cast<void*>(base),M::span,0,"virtual-only",0,0);
 av2_boot_log("memory","guest direct backing cap=%zu; commit on SDK demand; aliases share backing",backing_limit);
 if(!guest_boundary_selftest()) {
   av2_boot_log("memory-selftest","FAIL; clean stop before runtime setup");
   (void)memory.release();base=0;return nullptr;
 }
 return reinterpret_cast<void*>(base);
}
extern "C" bool av2_guest_contains(const void* address,size_t bytes) {
 const auto value=reinterpret_cast<uintptr_t>(address);
 return base && value>=base && value-base<M::span && bytes<=M::span-(value-base);
}
extern "C" bool av2_guest_release() {
 const auto before=memory.charged_bytes();const int result=memory.release();
 av2_boot_memory("release-guest",reinterpret_cast<void*>(base),M::span,0,"direct",result,-int64_t(before));
 if(!result)base=0;
 return result==0;
}
extern "C" bool av2_guest_commit(void* address,size_t bytes,unsigned permissions) {
 if(!av2_guest_contains(address,bytes))return false;
 const auto before=memory.charged_bytes();const auto started=microseconds();
 const int result=memory.set_host(reinterpret_cast<uintptr_t>(address)-base,bytes,flags(permissions));
 const auto after=memory.charged_bytes();
 av2_boot_memory_timed("commit-guest",address,bytes,int(permissions),"direct",result,int64_t(after)-int64_t(before),microseconds()-started);
 return result==0;
}
extern "C" bool av2_guest_protect(void* address,size_t bytes,unsigned permissions) {
 if(!av2_guest_contains(address,bytes))return false;
 const auto offset=reinterpret_cast<uintptr_t>(address)-base;
 if(offset%M::guest_page || bytes%M::guest_page)return false;
 // SDK small-page reconciliation calls Protect (not AllocFixed) after its
 // guest page table commits 4 KiB pages inside a 16 KiB host page. Granting
 // access must materialize backing here; no-access on a never-used page must
 // remain reservation-only. The SDK still owns guest protection metadata.
 const auto before=memory.charged_bytes();const auto started=microseconds();int result=0;
 if(permissions)result=memory.set_host(offset,bytes,flags(permissions));
 else for(size_t at=0;at<bytes;) {
   const bool backed=memory.host_access(offset+at)&M::committed;
   size_t length=M::guest_page;
   while(at+length<bytes && bool(memory.host_access(offset+at+length)&M::committed)==backed)length+=M::guest_page;
   result=memory.set_host(offset+at,length,backed?flags(0):uint8_t(M::reserved));
   if(result)break;
   at+=length;
 }
 av2_boot_memory_timed("protect-guest",address,bytes,int(permissions),"direct",result,
                 int64_t(memory.charged_bytes())-int64_t(before),microseconds()-started);
 return result==0;
}
extern "C" bool av2_guest_decommit(void* address,size_t bytes) {
 if(!av2_guest_contains(address,bytes))return false;
 const auto before=memory.charged_bytes();const auto started=microseconds();
 const int result=memory.set_host(reinterpret_cast<uintptr_t>(address)-base,bytes,M::reserved);
 const auto after=memory.charged_bytes();
 av2_boot_memory_timed("decommit-guest",address,bytes,0,"direct",result,int64_t(after)-int64_t(before),microseconds()-started);
 return result==0;
}
extern "C" unsigned av2_guest_permissions(const void* address) {
 if(!av2_guest_contains(address,1))return 0;
 const auto value=memory.host_access(reinterpret_cast<uintptr_t>(address)-base);
 if(!(value&M::committed) || (value&M::guard))return 0;
 return ((value&M::read)?1u:0u)|((value&M::write)?2u:0u);
}

// Runs immediately after reserve, before any guest heaps/data exist. It checks
// the same E-view and physical alias with actual byte writes, then releases all
// test backing. No persistent state or allocator budget changes.
static bool guest_boundary_selftest() {
 const uint64_t starts[]={0xE0000000ULL,0xE0002000ULL,0xFFCA2000ULL,0xFFCFF000ULL};
 const size_t sizes[]={0x1000,0x2000,0x2000,0x1000};
 const auto initial=memory.charged_bytes();
 for(unsigned i=0;i<4;++i) {
   const uint64_t guest=starts[i], physical=0x100000000ULL+(guest-0xE0000000ULL)+0x1000;
   const auto size=sizes[i];
   if(memory.set(guest,size,M::committed|M::read|M::write) || memory.set_host(physical,size,M::committed|M::read|M::write))return false;
   auto* cpu=reinterpret_cast<volatile unsigned char*>(base+guest+0x1000);
   auto* alias=reinterpret_cast<volatile unsigned char*>(base+physical);
   if(memory.translate(guest)!=reinterpret_cast<void*>(base+guest+0x1000))return false;
   for(size_t n=0;n<size;n+=M::guest_page){cpu[n]=0x5a;if(alias[n]!=0x5a)return false;alias[n]=0xa5;if(cpu[n]!=0xa5)return false;}
   av2_boot_log("memory-selftest","PASS guest=%llx size=%zu shifted=%p physical=%p",static_cast<unsigned long long>(guest),size,reinterpret_cast<void*>(base+guest+0x1000),reinterpret_cast<void*>(base+physical));
   if(memory.set(guest,size,M::reserved)||memory.set_host(physical,size,M::reserved))return false;
 }
 const bool clean=memory.charged_bytes()==initial;
 av2_boot_log("memory-selftest","boundary/alias writes complete backing-restored=%d",int(clean));
 return clean;
}
