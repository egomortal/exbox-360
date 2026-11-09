#pragma once
#include <cstdint>
extern "C" uint64_t av2_launch_begin() noexcept;
extern "C" void av2_launch_record(const char*,uint64_t) noexcept;
extern "C" void av2_launch_elapsed(const char*,uint64_t) noexcept;
extern "C" void av2_launch_phase(const char*) noexcept;
extern "C" void av2_launch_poll() noexcept;
extern "C" void av2_launch_dump(const char*) noexcept;
struct Av2LaunchScope { const char* name; uint64_t start; explicit Av2LaunchScope(const char* n):name(n),start(av2_launch_begin()){} ~Av2LaunchScope(){av2_launch_record(name,start);} };

#include "hitch_profile.h"
struct Av2HitchLaunchScope { const char* name; uint64_t start,hitch; uint32_t thread; explicit Av2HitchLaunchScope(const char* n,uint32_t tid=0):name(n),start(av2_launch_begin()),hitch(av2_hitch_begin()),thread(tid){} ~Av2HitchLaunchScope(){av2_launch_record(name,start);av2_hitch_record(name,hitch,thread);} };
