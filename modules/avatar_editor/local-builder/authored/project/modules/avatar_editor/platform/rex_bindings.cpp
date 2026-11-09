// Replaces Clock platform methods and the TLS/sleep subset of threading_posix.
// Do not link alongside those same upstream definitions. Full WaitHandle,
// Thread/WaitHandle/APC remain the upstream POSIX implementation in the host runtime.
#include "ps5_platform.h"
#include <rex/chrono/clock.h>
#include <rex/thread.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <sched.h>

namespace rex::chrono {
uint64_t Clock::host_tick_frequency_platform() { return av2::platform::tick_frequency; }
uint64_t Clock::host_tick_count_platform() {
  std::uint64_t ticks = 0;
  if (av2::platform::monotonic_ticks(ticks)) std::abort();
  return ticks;
}
uint64_t Clock::QueryHostSystemTime() {
  std::uint64_t ticks = 0;
  if (av2::platform::system_time_100ns(ticks)) std::abort();
  return ticks;
}
uint64_t Clock::QueryHostUptimeMillis() { return host_tick_count_platform() / 1000000; }
} // namespace rex::chrono
namespace rex::thread {
namespace {
struct Slot { pthread_key_t key{}; std::uint32_t generation = 0; bool used = false; };
std::array<Slot, 128> slots;
std::mutex tls_mutex;
Slot* slot_for(TlsHandle handle) {
  const auto index = handle & 0x7FU;
  auto& slot = slots[index];
  return handle != kInvalidTlsHandle && slot.used && slot.generation == (handle >> 7) ? &slot : nullptr;
}
}
TlsHandle AllocateTlsHandle() {
  std::lock_guard<std::mutex> lock(tls_mutex);
  for (std::uint32_t i = 0; i < slots.size(); ++i) {
    auto& slot = slots[i];
    // Never recycle a wrapped generation, so stale handles cannot alias.
    if (slot.used || slot.generation >= 0x1FFFFFEU) continue;
    if (pthread_key_create(&slot.key, nullptr)) return kInvalidTlsHandle;
    ++slot.generation; slot.used = true;
    return (slot.generation << 7) | i;
  }
  return kInvalidTlsHandle;
}
bool FreeTlsHandle(TlsHandle handle) {
  std::lock_guard<std::mutex> lock(tls_mutex);
  auto* slot = slot_for(handle);
  if (!slot || pthread_key_delete(slot->key)) return false;
  slot->used = false; return true;
}
uintptr_t GetTlsValue(TlsHandle handle) {
  std::lock_guard<std::mutex> lock(tls_mutex);
  auto* slot = slot_for(handle);
  return slot ? reinterpret_cast<uintptr_t>(pthread_getspecific(slot->key)) : 0;
}
bool SetTlsValue(TlsHandle handle, uintptr_t value) {
  std::lock_guard<std::mutex> lock(tls_mutex);
  auto* slot = slot_for(handle);
  return slot && !pthread_setspecific(slot->key, reinterpret_cast<void*>(value));
}
void MaybeYield() { (void)sched_yield(); }
void SyncMemory() { std::atomic_thread_fence(std::memory_order_seq_cst); }
void Sleep(std::chrono::microseconds duration) {
  if (duration.count() <= 0) { MaybeYield(); return; }
  const auto us = static_cast<std::uint64_t>(duration.count());
  if (us > std::numeric_limits<std::uint64_t>::max() / 1000 || av2::platform::sleep_ns(us * 1000))
    std::abort();
}
} // namespace rex::thread
