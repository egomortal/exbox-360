#pragma once
#include <cstdint>
extern "C" void av2_normal_exit_route(uint32_t route) noexcept;
extern "C" bool av2_normal_exit_intercept(void* thread, uint32_t route, int status, bool main_only) noexcept;
extern "C" bool av2_normal_exit_parked(const void* thread) noexcept;
extern "C" bool av2_normal_exit_requested() noexcept;
extern "C" bool av2_normal_exit_refused() noexcept;
extern "C" uint32_t av2_normal_exit_route_value() noexcept;
extern "C" int av2_normal_exit_status() noexcept;
extern "C" const char* av2_normal_exit_route_name() noexcept;

extern "C" [[noreturn]] void av2_normal_exit_park_unstarted(const void* thread) noexcept;

extern "C" void av2_normal_exit_enable() noexcept;
extern "C" bool av2_normal_exit_active() noexcept;
extern "C" void av2_normal_exit_request(uint32_t route) noexcept;
