#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace av2::platform {
// ReXGlue 0.10.0 xmemory.cpp map_info, with 16 KiB host-page reconciliation.
// Native bridge uses set_host after the SDK reconciles host-page permissions.
// Raw guest accesses MUST use check_access when
// a host page contains mixed 4 KiB permissions; mprotect cannot enforce those.
class GuestMemory {
 public:
  enum Access : std::uint8_t { reserved = 0, committed = 1, read = 2, write = 4, guard = 8 };
  static constexpr std::uint64_t span = 0x120000000ULL;
  static constexpr std::size_t host_page = 0x4000, guest_page = 0x1000;
  GuestMemory() = default;
  ~GuestMemory();
  GuestMemory(const GuestMemory&) = delete;
  GuestMemory& operator=(const GuestMemory&) = delete;
  // Budget is backing only. Metadata/stacks/runtime/GPU need separate headroom.
  int reserve(std::size_t backing_budget);
  // Guest offsets (or physical window at +4 GiB), 4 KiB aligned. No native EXEC.
  // Failure can leave earlier pages applied and charged; stop users and release.
  int set(std::uint64_t offset, std::size_t bytes, std::uint8_t access);
  int set_host(std::uint64_t offset, std::size_t bytes, std::uint8_t access);
  std::uint8_t host_access(std::uint64_t offset) const;
  bool check_access(std::uint64_t offset, std::size_t bytes, bool writing) const;
  void* translate(std::uint64_t offset) const;
  std::size_t charged_bytes() const;
  int release(); // All readers must be stopped.
 private:
  // A block owns its exact kernel allocation until its last mapped page goes.
  // Partial decommit may retain charged backing; never release a suballocation.
  struct Page { std::int64_t backing = -1; std::uint64_t block = 0; };
  struct Block { std::int64_t backing = -1; std::size_t bytes = 0, pages = 0; };
  int protect_run(std::uint64_t backing, std::size_t bytes);
  int allocate_run(std::uint64_t backing, std::size_t bytes);
  int drop_page(std::map<std::uint64_t, Page>::iterator page);
  std::size_t charged_locked() const;
  std::map<std::uint64_t, Block> blocks_;
  std::uint64_t next_block_ = 1;
  static bool corrected(std::uint64_t offset, std::size_t bytes, std::uint64_t& host);
  int reconcile(std::uint64_t backing);
  mutable std::mutex mutex_;
  void* base_ = nullptr;
  std::size_t limit_ = 0;
  bool poisoned_ = false;
  std::vector<std::uint8_t> access_;
  std::map<std::uint64_t, Page> pages_;
};
} // namespace av2::platform
