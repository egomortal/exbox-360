#include "render_phases.h"
#include <atomic>
#include <cerrno>
#include <time.h>
namespace av2::render_phase {
namespace{std::atomic<bool> enabled{false};std::atomic<uint64_t> deadline{0};thread_local Collector collector;}
void enable(uint64_t start,uint64_t end)noexcept{deadline.store(end);enabled.store(end>start);}
void disable()noexcept{enabled.store(false);}
bool active()noexcept{return enabled.load(std::memory_order_relaxed);}
uint64_t now()noexcept {
 int saved=errno;timespec t{};const int code=clock_gettime(CLOCK_MONOTONIC,&t);errno=saved;
 const uint64_t value=code?0:uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);
 if(value>=deadline.load(std::memory_order_relaxed))enabled.store(false,std::memory_order_relaxed);
 return value;
}
Collector&local()noexcept{return collector;}
Snapshot snapshot(uint64_t time)noexcept{return active()?collector.snapshot(time):Snapshot{};}
}
