#pragma once
#include <cstdint>
extern "C" bool av2_raw_open(const char* current,const char* previous) noexcept;
extern "C" void av2_raw_import(uint32_t id,const char* name,uint32_t r3,uint32_t r4) noexcept;
extern "C" void av2_raw_reason(const char* kind,const char* function,uint32_t line,uintptr_t pointer) noexcept;
extern "C" void av2_raw_abort() noexcept;
