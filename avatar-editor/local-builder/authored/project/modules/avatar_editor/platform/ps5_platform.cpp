#include "ps5_platform.h"
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __PROSPERO__
// ABI measured by Omni's vsstest.c; reserve does not charge flexible memory.
extern "C" int sceKernelReserveVirtualRange(void**, std::size_t, int, std::size_t);
#endif

namespace av2::platform {
Arena::~Arena() { (void)release(); }

int Arena::reserve(std::size_t bytes, std::size_t commit_limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (base_) return EBUSY;
  const long host_page = sysconf(_SC_PAGESIZE);
  if (host_page <= 0) return ENOTSUP;
  const auto page = static_cast<std::size_t>(host_page);
#ifdef __PROSPERO__
  if (page != 0x4000) return ENOTSUP;
#endif
  // MEMORY_DIRECT.md: no implicit entitlement to the whole flexible pool.
  // Caller supplies headroom after stacks, metadata, SDL and runtime overhead.
  if (!bytes || bytes % page || !commit_limit || commit_limit >= (1ULL << 30))
    return EINVAL;
  std::vector<std::uint8_t> pages;
  try { pages.resize(bytes / page, 0); }
  catch (const std::bad_alloc&) { return ENOMEM; }
  catch (const std::length_error&) { return EOVERFLOW; }
  void* mapping = nullptr;
#ifdef __PROSPERO__
  const int result = sceKernelReserveVirtualRange(&mapping, bytes, 0, page);
  if (result) return static_cast<int>(static_cast<unsigned>(result) & 0xFFFFU);
  if (!mapping) return EFAULT;
#else
  mapping = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapping == MAP_FAILED) return errno;
#endif
  base_ = mapping;
  size_ = bytes;
  page_size_ = page;
  limit_ = commit_limit;
  committed_.swap(pages);
  return 0;
}

bool Arena::valid_range(std::size_t offset, std::size_t bytes) const {
  return base_ && bytes && offset <= size_ && bytes <= size_ - offset &&
         offset % page_size_ == 0 && bytes % page_size_ == 0;
}

int Arena::commit(std::size_t offset, std::size_t bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!valid_range(offset, bytes)) return EINVAL;
  const auto first = offset / page_size_, count = bytes / page_size_;
  std::size_t additional = 0;
  for (std::size_t i = first; i < first + count; ++i)
    if (!committed_[i]) additional += page_size_;
  if (additional > limit_ - charged_) return ENOMEM;
#ifdef __PROSPERO__
  // Only map fresh runs: MAP_FIXED over a committed page would erase contents.
  // A kernel failure may leave earlier runs committed, but fully accounted for.
  for (std::size_t i = first; i < first + count;) {
    if (committed_[i]) { ++i; continue; }
    std::size_t end = i + 1;
    while (end < first + count && !committed_[end]) ++end;
    auto* address = static_cast<unsigned char*>(base_) + i * page_size_;
    const auto length = (end - i) * page_size_;
    if (mmap(address, length, PROT_READ | PROT_WRITE,
             MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) return errno;
    for (; i < end; ++i) committed_[i] = 1;
    charged_ += length;
  }
#else
  auto* address = static_cast<unsigned char*>(base_) + offset;
  if (mprotect(address, bytes, PROT_READ | PROT_WRITE) != 0) return errno;
  for (std::size_t i = first; i < first + count; ++i) committed_[i] = 1;
  charged_ += additional; // Charge worst-case resident bytes before first touch.
#endif
  return 0;
}

int Arena::decommit(std::size_t offset, std::size_t bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!valid_range(offset, bytes)) return EINVAL;
  auto* address = static_cast<unsigned char*>(base_) + offset;
#ifdef __PROSPERO__
  void* reservation = address;
  const int result = sceKernelReserveVirtualRange(&reservation, bytes, 0x10, 0);
  if (result) return static_cast<int>(static_cast<unsigned>(result) & 0xFFFFU);
  if (reservation != address) return EFAULT;
#else
  // Replace only our owned anonymous range. Fresh pages are zero on recommit;
  // madvise(DONTNEED) alone has different discard semantics across kernels.
  if (mmap(address, bytes, PROT_NONE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) return errno;
#endif
  const auto first = offset / page_size_, count = bytes / page_size_;
  for (std::size_t i = first; i < first + count; ++i) {
    if (committed_[i]) charged_ -= page_size_;
    committed_[i] = 0;
  }
  return 0;
}

int Arena::release() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (base_ && munmap(base_, size_) != 0) return errno;
  base_ = nullptr;
  size_ = page_size_ = limit_ = charged_ = 0;
  std::vector<std::uint8_t>().swap(committed_);
  return 0;
}

std::size_t Arena::charged_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return charged_;
}

Thread::~Thread() { if (joinable_) std::terminate(); }
int Thread::start(void* (*entry)(void*), void* argument, std::size_t stack_bytes) {
  if (joinable_) return EBUSY;
  if (!entry) return EINVAL;
  pthread_attr_t attributes;
  int result = pthread_attr_init(&attributes);
  if (result) return result;
  if (stack_bytes) result = pthread_attr_setstacksize(&attributes, stack_bytes);
  if (!result) result = pthread_create(&thread_, &attributes, entry, argument);
  (void)pthread_attr_destroy(&attributes);
  if (!result) joinable_ = true;
  return result;
}
int Thread::join() {
  if (!joinable_) return EINVAL;
  const int result = pthread_join(thread_, nullptr);
  if (!result) joinable_ = false;
  return result;
}

TlsKey::~TlsKey() { if (valid_) (void)pthread_key_delete(key_); }
int TlsKey::create(void (*destructor)(void*)) {
  if (valid_) return EBUSY;
  const int result = pthread_key_create(&key_, destructor);
  if (!result) valid_ = true;
  return result;
}
int TlsKey::set(void* value) { return valid_ ? pthread_setspecific(key_, value) : EINVAL; }
void* TlsKey::get() const { return valid_ ? pthread_getspecific(key_) : nullptr; }

int monotonic_ticks(std::uint64_t& nanoseconds) {
  timespec value{};
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return errno;
  if (value.tv_sec < 0 || static_cast<std::uint64_t>(value.tv_sec) >
      (std::numeric_limits<std::uint64_t>::max() - 999999999ULL) / tick_frequency)
    return EOVERFLOW;
  nanoseconds = static_cast<std::uint64_t>(value.tv_sec) * tick_frequency +
                static_cast<std::uint64_t>(value.tv_nsec);
  return 0;
}
int system_time_100ns(std::uint64_t& ticks_since_1601) {
  timespec value{};
  if (clock_gettime(CLOCK_REALTIME, &value) != 0) return errno;
  constexpr std::uint64_t epoch = 11644473600ULL;
  if (value.tv_sec < 0 || static_cast<std::uint64_t>(value.tv_sec) >
      (std::numeric_limits<std::uint64_t>::max() - 9999999ULL) / 10000000ULL - epoch)
    return EOVERFLOW;
  ticks_since_1601 = (static_cast<std::uint64_t>(value.tv_sec) + epoch) * 10000000ULL +
                    static_cast<std::uint64_t>(value.tv_nsec) / 100;
  return 0;
}
int sleep_ns(std::uint64_t nanoseconds) {
  const auto seconds = nanoseconds / tick_frequency;
  if (seconds > static_cast<std::uint64_t>(std::numeric_limits<time_t>::max()))
    return EOVERFLOW;
  timespec remaining{static_cast<time_t>(seconds),
                     static_cast<long>(nanoseconds % tick_frequency)};
  while (nanosleep(&remaining, &remaining) != 0) {
    if (errno != EINTR) return errno;
  }
  return 0;
}

namespace {
struct Fd {
  int value;
  explicit Fd(int fd) : value(fd) {}
  ~Fd() {
    const int saved = errno;
    if (value >= 0) (void)close(value);
    errno = saved;
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
unsigned char fold(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}
bool equal_fold(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (fold(static_cast<unsigned char>(a[i])) != fold(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}
int lookup(int parent, std::string_view name, std::string& found) {
  const int scan = openat(parent, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (scan < 0) return errno;
  DIR* directory = fdopendir(scan);
  if (!directory) { const int error = errno; (void)close(scan); return error; }
  int error = 0;
  bool matched = false;
  for (;;) {
    errno = 0;
    const dirent* entry = readdir(directory);
    if (!entry) { error = errno; break; }
    if (!equal_fold(entry->d_name, name)) continue;
    if (matched) { error = EEXIST; break; }
    found = entry->d_name;
    matched = true;
  }
  (void)closedir(directory);
  return error ? error : (matched ? 0 : ENOENT);
}
} // namespace

static int open_relative(const char* root, std::string_view path, bool directory) {
  if (directory && root && path.empty())
    return open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (!root || path.empty() || path.size() > 4096 || path.front() == '/' || path.front() == '\\') {
    errno = EINVAL; return -1;
  }
  for (unsigned char c : path) {
    if (c >= 128) { errno = ENOTSUP; return -1; }
    if (c < 32 || c == ':') { errno = EINVAL; return -1; }
  }
  Fd current(open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (current.value < 0) return -1;
  std::size_t start = 0;
  while (start < path.size()) {
    const auto end = path.find_first_of("/\\", start);
    const bool last = end == std::string_view::npos;
    const auto name = path.substr(start, last ? path.size() - start : end - start);
    if (name.empty() || name == "." || name == ".." || name.size() > 255) {
      errno = EINVAL; return -1;
    }
    std::string found;
    found.reserve(255);
    const int error = lookup(current.value, name, found);
    if (error) { errno = error; return -1; }
    const int next = openat(current.value, found.c_str(), O_RDONLY | O_CLOEXEC |
                            O_NOFOLLOW | O_NONBLOCK | ((last && !directory) ? 0 : O_DIRECTORY));
    if (next < 0) return -1;
    (void)close(current.value);
    current.value = next;
    if (last) {
      struct stat info{};
      if (fstat(next, &info) != 0) return -1;
      if (!(directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode))) { errno = EINVAL; return -1; }
      current.value = -1;
      return next;
    }
    start = end + 1;
  }
  errno = EINVAL;
  return -1;
}
int open_asset(const char* root, std::string_view path) { return open_relative(root, path, false); }
int open_asset_directory(const char* root, std::string_view path) { return open_relative(root, path, true); }
} // namespace av2::platform
