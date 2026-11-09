#pragma once
#include <cstdint>
extern "C" void av2_wait_record(uint32_t thread,uint32_t object,bool timed,uint64_t timeout,uint32_t reason,uint32_t mode,uint32_t alertable,uint32_t result,uint64_t start,uint64_t end) noexcept;
extern "C" void av2_wait_dump(const char* window) noexcept;
