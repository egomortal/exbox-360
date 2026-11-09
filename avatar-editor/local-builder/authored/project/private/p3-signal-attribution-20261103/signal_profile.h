#pragma once
#include <cstdint>
#include <cstddef>
constexpr uint32_t av2_signal_target=0x400050d4;
enum Av2SignalOp:uint32_t{Av2Wait=1,Av2Set=2,Av2Pulse=3,Av2NtSet=4,Av2Release=5};
extern "C" uint64_t av2_signal_begin(uint32_t op,uint32_t object,uint32_t handle,uint32_t thread,uint64_t lr,uint32_t pc,uint32_t a,uint32_t b,uint32_t c,uint64_t detail,uint64_t start) noexcept;
extern "C" void av2_signal_end(uint64_t token,uint32_t result,uint32_t auxiliary=0) noexcept;
extern "C" void av2_signal_unresolved_handle() noexcept;
extern "C" void av2_irq_enter(uint32_t source,uint32_t callback,uint32_t data) noexcept;
extern "C" void av2_irq_return(uint32_t source,uint32_t callback,uint32_t data,uint64_t result) noexcept;
extern "C" void av2_signal_heartbeat(char* out,size_t size) noexcept;
extern "C" void av2_signal_dump(const char* window) noexcept;
