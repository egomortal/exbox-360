#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <pthread.h>
#include <string_view>
#include <vector>

namespace av2::platform {
// Independent port primitives; not yet wired into rex::memory or guest SEH.
// All operations return an errno value (zero on success), except open_asset.
class Arena {
 public:
  Arena() = default;
  ~Arena();
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  int reserve(std::size_t bytes, std::size_t commit_limit);
  int commit(std::size_t offset, std::size_t bytes);
  int decommit(std::size_t offset, std::size_t bytes);
  int release(); // Caller must stop every reader first.
  void* base() const { return base_; }
  std::size_t page_size() const { return page_size_; }
  std::size_t charged_bytes() const;
 private:
  bool valid_range(std::size_t offset, std::size_t bytes) const;
  mutable std::mutex mutex_;
  void* base_ = nullptr;
  std::size_t size_ = 0, page_size_ = 0, limit_ = 0, charged_ = 0;
  std::vector<std::uint8_t> committed_;
};

class Thread {
 public:
  Thread() = default;
  ~Thread(); // A joinable thread is a lifecycle error, as with std::thread.
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;
  int start(void* (*entry)(void*), void* argument, std::size_t stack_bytes = 0);
  int join();
 private:
  pthread_t thread_{};
  bool joinable_ = false;
};

class TlsKey {
 public:
  TlsKey() = default;
  ~TlsKey(); // All users must be joined before key destruction.
  TlsKey(const TlsKey&) = delete;
  TlsKey& operator=(const TlsKey&) = delete;
  int create(void (*destructor)(void*) = nullptr);
  int set(void* value);
  void* get() const;
 private:
  pthread_key_t key_{};
  bool valid_ = false;
};

constexpr std::uint64_t tick_frequency = 1000000000ULL;
int monotonic_ticks(std::uint64_t& nanoseconds);
int system_time_100ns(std::uint64_t& ticks_since_1601);
int sleep_ns(std::uint64_t nanoseconds);

// Trusted mount root, relative Xbox path; ASCII folding only. Returns owned fd
// or -1 with errno. Rejects traversal, symlinks, ambiguous case and non-files.
// Streams directory entries; never caches the closet or its item payloads.
int open_asset(const char* root, std::string_view guest_relative_path);
// Empty relative path opens the mount root. Same traversal/case rules.
int open_asset_directory(const char* root, std::string_view guest_relative_path);
} // namespace av2::platform
