// Avatar randomization only; never use this fallback for security.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include "boot_log.h"
namespace {
using Provider=int(*)(void*,size_t);
Provider provider() {
 static Provider resolved=[]()->Provider {
  // Keep the module resident for the lifetime of the function pointer.
  void* module=dlopen("/system/common/lib/libSceRandom.sprx",RTLD_NOW|RTLD_LOCAL);
  auto result=reinterpret_cast<Provider>(dlsym(module?module:RTLD_DEFAULT,"sceRandomGetRandomNumber"));
  av2_boot_log("random","optional module=%d provider=%d; fallback avatar-only PRNG",module!=nullptr,result!=nullptr);
  return result;
 }();
 return resolved;
}
uint64_t seed() {
 timespec now{}; (void)clock_gettime(CLOCK_MONOTONIC,&now);
 return uint64_t(now.tv_sec)^(uint64_t(now.tv_nsec)<<32)^uint64_t(getpid());
}
uint64_t word() {
 // Atomic state prevents concurrent callers from racing. SplitMix64 is not cryptographic.
 static std::atomic<uint64_t> state{seed()};
 uint64_t z=state.fetch_add(UINT64_C(0x9e3779b97f4a7c15),std::memory_order_relaxed);
 z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
 return z^(z>>31);
}
}
extern "C" int __wrap_sceRandomGetRandomNumber(void* destination,size_t bytes) {
 if(!bytes)return 0;
 if(!destination)return -1;
 auto* next=static_cast<unsigned char*>(destination); auto native=provider();
 while(bytes) {
  const size_t count=bytes<64?bytes:64;
  if(!native||native(next,count)!=0) {
   // Replace the full failed chunk, including any partially written provider bytes.
   for(size_t i=0;i<count;) {
    uint64_t value=word();
    for(unsigned j=0;j<8&&i<count;++j,++i){next[i]=static_cast<unsigned char>(value);value>>=8;}
   }
  }
  next+=count;bytes-=count;
 }
 return 0;
}
