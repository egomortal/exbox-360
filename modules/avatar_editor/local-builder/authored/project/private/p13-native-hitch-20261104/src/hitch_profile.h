#pragma once
#include <cstdint>
extern "C" void av2_hitch_configure(bool enabled,uint64_t now) noexcept;
extern "C" uint64_t av2_hitch_begin() noexcept;
extern "C" void av2_hitch_record(const char*,uint64_t start,uint32_t thread=0,uint32_t object=0) noexcept;
extern "C" void av2_hitch_interval(const char*,uint64_t start,uint64_t end,uint32_t thread=0,uint32_t object=0) noexcept;
extern "C" void av2_hitch_frame(uint64_t frame,uint64_t now) noexcept;
extern "C" void av2_hitch_finish(const char* reason) noexcept;
#include "wait_metadata.h"
extern "C" void av2_hitch_wait(uint64_t start,uint32_t thread,uint32_t object,const Av2HitchWait* metadata) noexcept;
