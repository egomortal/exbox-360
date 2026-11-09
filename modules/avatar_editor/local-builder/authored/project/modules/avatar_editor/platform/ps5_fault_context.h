#pragma once
#include <rex/exception_handler.h>
#include <cstring>
#ifdef __PROSPERO__
#include <machine/fpu.h>
#include <cstddef>
static_assert(offsetof(struct savefpu, sv_xmm)==160);
#endif
namespace av2::platform {
// FreeBSD/PS5 mcontext: SDK x86/ucontext.h and x86/fpu.h savefpu.
// Templates also permit a synthetic ABI fixture on the host. No allocation,
// guest dispatch or logging occurs while translating a captured signal context.
template<class Native> void load_ps5_context(const Native& native, rex::arch::HostThreadContext& host) {
  host.rip=native.mc_rip; host.eflags=static_cast<uint32_t>(native.mc_rflags);
  const uint64_t values[]={uint64_t(native.mc_rax),uint64_t(native.mc_rcx),uint64_t(native.mc_rdx),uint64_t(native.mc_rbx),
    uint64_t(native.mc_rsp),uint64_t(native.mc_rbp),uint64_t(native.mc_rsi),uint64_t(native.mc_rdi),
    uint64_t(native.mc_r8),uint64_t(native.mc_r9),uint64_t(native.mc_r10),uint64_t(native.mc_r11),
    uint64_t(native.mc_r12),uint64_t(native.mc_r13),uint64_t(native.mc_r14),uint64_t(native.mc_r15)};
  std::memcpy(host.int_registers,values,sizeof(values));
  static_assert(sizeof(native.mc_fpstate)>=160+256);
  if (native.mc_fpformat==0x10002)
    std::memcpy(host.xmm_registers,reinterpret_cast<const unsigned char*>(native.mc_fpstate)+160,256);
  else std::memset(host.xmm_registers,0,256);
}
template<class Native> void store_ps5_context(Native& native, const rex::arch::HostThreadContext& host,
                                             uint16_t integer_mask,uint16_t xmm_mask) {
  native.mc_rip=host.rip;
  native.mc_rflags=(uint64_t(native.mc_rflags)&0xffffffff00000000ULL)|host.eflags;
  decltype(&native.mc_rax) registers[]={&native.mc_rax,&native.mc_rcx,&native.mc_rdx,&native.mc_rbx,
    &native.mc_rsp,&native.mc_rbp,&native.mc_rsi,&native.mc_rdi,&native.mc_r8,&native.mc_r9,
    &native.mc_r10,&native.mc_r11,&native.mc_r12,&native.mc_r13,&native.mc_r14,&native.mc_r15};
  for (unsigned i=0;i<16;++i) {
    if (integer_mask&(1U<<i)) *registers[i]=host.int_registers[i];
    if (native.mc_fpformat==0x10002 && (xmm_mask&(1U<<i)))
      std::memcpy(reinterpret_cast<unsigned char*>(native.mc_fpstate)+160+i*16,&host.xmm_registers[i],16);
  }
}
} // namespace av2::platform
