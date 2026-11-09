#include "guest_memory.h"
#include <array>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __PROSPERO__
#include "../native/boot_log.h"
extern "C" int sceKernelReserveVirtualRange(void**, std::size_t, int, std::size_t);
extern "C" std::int64_t sceKernelGetDirectMemorySize();
extern "C" int sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t,
                                            std::size_t, int, std::int64_t*);
extern "C" int sceKernelMapDirectMemory(void**, std::size_t, int, int, std::int64_t, std::size_t);
extern "C" int sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
#endif

namespace av2::platform {
namespace {
struct View { std::uint64_t start, size, backing; };
constexpr View views[] = {
  {0, 0x40000000, 0}, {0x40000000, 0x3F000000, 0x40000000},
  {0x7F000000, 0x01000000, 0x100000000ULL},
  {0x80000000, 0x10000000, 0x80000000}, {0x90000000, 0x10000000, 0x80000000},
  {0xA0000000, 0x20000000, 0x100000000ULL}, {0xC0000000, 0x20000000, 0x100000000ULL},
  {0xE0000000, 0x20000000, 0x100000000ULL}, {0x100000000ULL, 0x20000000, 0x100000000ULL}
};
std::uint64_t backing_for(std::uint64_t address) {
  for (const auto& v : views)
    if (address >= v.start && address - v.start < v.size) return v.backing + address - v.start;
  return GuestMemory::span;
}
// Q7z: docs/EDITOR-CONSOLE-BRINGUP-2026-11-03.md.
// Protection for one host page of one alias view.
// The 0xA0000000, 0xC0000000, 0xE0000000 and raw physical views all alias the
// same physical backing, and the guest legitimately reads physical memory
// through any of them (Xenia maps every physical view read/write). Console
// run-31 faulted in draw assembly reading 0xDF344040 through the C view of a
// page that was allocated through another view. A physical alias that has no
// allocation of its own therefore takes the union of the protections of the
// physical aliases that do; a view's own allocation/protection always wins.
constexpr std::uint64_t kPhysicalBacking = 0x100000000ULL;
int own_protection(const std::vector<std::uint8_t>& access, std::uint64_t view_address,
                   std::size_t pages, std::uint8_t committed, std::uint8_t read,
                   std::uint8_t write, std::uint8_t guard, std::size_t guest_page,
                   bool& any_committed) {
  int prot = 0;
  const auto slot = view_address / guest_page;
  for (std::size_t i = 0; i < pages; ++i) {
    const auto a = access[slot + i];
    any_committed |= (a & committed) != 0;
    if (!(a & guard)) { if (a & read) prot |= PROT_READ; if (a & write) prot |= PROT_WRITE; }
  }
  return prot;
}
int alias_protection(const std::vector<std::uint8_t>& access, const View& view,
                     std::uint64_t backing, std::size_t pages, std::uint8_t committed,
                     std::uint8_t read, std::uint8_t write, std::uint8_t guard,
                     std::size_t guest_page) {
  bool own_committed = false;
  const int own = own_protection(access, view.start + backing - view.backing, pages, committed,
                                 read, write, guard, guest_page, own_committed);
  if (own_committed || view.backing != kPhysicalBacking) return own;
  int inherited = 0;
  for (const auto& other : views) {
    if (&other == &view || other.backing != kPhysicalBacking) continue;
    if (backing < other.backing || backing - other.backing >= other.size) continue;
    bool other_committed = false;
    const int p = own_protection(access, other.start + backing - other.backing, pages, committed,
                                 read, write, guard, guest_page, other_committed);
    if (other_committed) inherited |= p;
  }
  return inherited;
}
int reserve_at(void*& address, std::size_t bytes, bool fixed) {
#ifdef __PROSPERO__
  const int error = sceKernelReserveVirtualRange(&address, bytes, fixed ? 0x10 : 0,
                                                fixed ? 0 : GuestMemory::host_page);
  if(error || !fixed || av2_boot_trace_pages())
    av2_boot_memory("kernel-reserve",address,bytes,fixed?0x10:0,"virtual-only",error,0);
  return error ? static_cast<int>(static_cast<unsigned>(error) & 0xFFFFU) : 0;
#else
  void* mapped = mmap(address, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS |
                     (fixed ? MAP_FIXED : 0), -1, 0);
  if (mapped == MAP_FAILED) return errno;
  address = mapped;
  return 0;
#endif
}
#ifdef __PROSPERO__
// Preserve the shipped Q7n/Q7z native source lineage; see docs/EDITOR-CONSOLE-BRINGUP-2026-11-03.md.
// The original non-Mac runtime chooses powers-of-two bases starting at 4 GiB.
// Reserve extra virtual address space only, trim owned slack, and retain the
// same 4.5 GiB span. No direct memory is committed by this helper.
constexpr std::uintptr_t guest_base_alignment=UINT64_C(0x100000000);
int reserve_aligned_guest(void*& chosen) {
  constexpr std::size_t requested=GuestMemory::span+guest_base_alignment-GuestMemory::host_page;
  void* raw=nullptr; const int error=reserve_at(raw,requested,false);
  if(error)return error;
  const auto start=reinterpret_cast<std::uintptr_t>(raw);
  if(!start || (start & (GuestMemory::host_page-1)) || start>UINTPTR_MAX-requested) {
    if(raw)(void)munmap(raw,requested);
    return EFAULT;
  }
  const auto aligned=(start+guest_base_alignment-1)&~(guest_base_alignment-1);
  const auto prefix=aligned-start;
  const auto suffix=requested-prefix-GuestMemory::span;
  av2_boot_log("memory","aligned reservation raw=%p bytes=%zu base=%p alignment=%llu prefix=%zu suffix=%zu",raw,requested,reinterpret_cast<void*>(aligned),static_cast<unsigned long long>(guest_base_alignment),size_t(prefix),size_t(suffix));
  // Trim the end first so rollback still has one contiguous owned range.
  if(suffix && munmap(reinterpret_cast<void*>(aligned+GuestMemory::span),suffix)) {
    const int saved=errno;(void)munmap(raw,requested);return saved;
  }
  if(prefix && munmap(raw,prefix)) {
    const int saved=errno;(void)munmap(raw,requested-suffix);return saved;
  }
  chosen=reinterpret_cast<void*>(aligned);
  return 0;
}
#endif
} // namespace
GuestMemory::~GuestMemory() { (void)release(); }
int GuestMemory::reserve(std::size_t budget) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (base_ || !pages_.empty() || !blocks_.empty()) return EBUSY;
  const long page = sysconf(_SC_PAGESIZE);
  if (page <= 0 || host_page % static_cast<std::size_t>(page)) return ENOTSUP;
  if (budget < host_page || budget >= (1ULL << 30) || budget % host_page) return EINVAL;
  try { access_.assign(span / guest_page, 0); }
  catch (const std::bad_alloc&) { return ENOMEM; }
  void* base = nullptr;
#ifdef __PROSPERO__
  const int error = reserve_aligned_guest(base);
#else
  const int error = reserve_at(base, span, false);
#endif
  if (error) { access_.clear(); return error; }
  if (!base) { access_.clear(); return EFAULT; }
#ifdef __PROSPERO__
  if(reinterpret_cast<std::uintptr_t>(base)&(guest_base_alignment-1)) {
    av2_boot_log("memory","unaligned guest reservation refused base=%p",base);
    (void)munmap(base,span);access_.clear();return EFAULT;
  }
#endif
  base_ = base; limit_ = budget; poisoned_ = false;
  return 0;
}
bool GuestMemory::corrected(std::uint64_t offset, std::size_t bytes, std::uint64_t& host) {
  if (!bytes || offset >= span || bytes > span - offset) return false;
  host = offset;
  if (offset >= 0xE0000000ULL && offset < 0x100000000ULL) {
    // SDK E heap size is 0x1FD00000; its 4 KiB skew is in CPU translation too.
    if (offset >= 0xFFD00000ULL || bytes > 0xFFD00000ULL - offset) return false;
    host += guest_page;
  } else if (offset < 0xE0000000ULL && bytes > 0xE0000000ULL - offset) return false;
  return true;
}
void* GuestMemory::translate(std::uint64_t offset) const {
  std::uint64_t address = 0;
  return base_ && corrected(offset, 1, address) ? static_cast<unsigned char*>(base_) + address : nullptr;
}
int GuestMemory::set(std::uint64_t offset, std::size_t bytes, std::uint8_t access) {
  std::uint64_t address=0;
  if(!corrected(offset,bytes,address))return EINVAL;
  return set_host(address,bytes,access);
}
// The SDK already applied the E-view skew and reconciled 4 KiB guest pages
// before calling its low-level host memory API. Do not apply that skew twice.
int GuestMemory::set_host(std::uint64_t address, std::size_t bytes, std::uint8_t access) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(!base_ || poisoned_)return EFAULT;
  if(!bytes || address>=span || bytes>span-address || address%guest_page || bytes%guest_page ||
     (access & ~std::uint8_t(15)) || (access && !(access & committed)) ||
     ((access & write) && !(access & read)))return EINVAL;
  // Publish the entire requested metadata range before reconciling host pages.
  // This removes four repeated mappings/protection passes per 16 KiB page.
  std::fill(access_.begin()+address/guest_page,
            access_.begin()+(address+bytes)/guest_page,access);
  const auto first=address & ~(std::uint64_t(host_page)-1);
  const auto end=(address+bytes+host_page-1)&~(std::uint64_t(host_page)-1);
  for(auto at=first;at<end;) {
#ifdef __PROSPERO__
    if(av2_boot_should_stop()){poisoned_=true;return ECANCELED;}
#endif
    const auto backing=backing_for(at);
    std::size_t run=host_page;
    // Batch only new, fully covered pages. Stop at view boundaries, an existing
    // page or 2 MiB; every alias must span the identical range. Edge pages keep
    // the established mixed-4-KiB reconciliation path.
    if((access&committed) && at>=address && at+host_page<=address+bytes &&
       pages_.find(backing)==pages_.end()) {
      while(run<2*1024*1024 && at+run+host_page<=address+bytes &&
            backing_for(at+run)==backing+run && pages_.find(backing+run)==pages_.end()) {
        bool same=true;
        for(const auto& v:views) {
          const bool a=backing>=v.backing && backing-v.backing<v.size;
          const bool b=backing+run>=v.backing && backing+run-v.backing<v.size;
          if(a!=b){same=false;break;}
        }
        if(!same)break;
        run+=host_page;
      }
      const int error=allocate_run(backing,run);
      if(error){poisoned_=true;return error;}
    } else if((access&committed) && pages_.find(backing)!=pages_.end()) {
      // Already-backed aliases need only protection updates, merged by range.
      while(run<2*1024*1024 && at+run<end && backing_for(at+run)==backing+run &&
            pages_.find(backing+run)!=pages_.end()) {
        bool same=true;
        for(const auto& v:views) {
          const bool a=backing>=v.backing && backing-v.backing<v.size;
          const bool b=backing+run>=v.backing && backing+run-v.backing<v.size;
          if(a!=b){same=false;break;}
        }
        if(!same)break;
        run+=host_page;
      }
      const int error=protect_run(backing,run);
      if(error){poisoned_=true;return error;}
    } else {
      const int error=reconcile(backing);
      if(error){poisoned_=true;return error;}
    }
    at+=run;
  }
  return 0;
}
std::size_t GuestMemory::charged_locked() const {
  std::size_t total=0;for(const auto& item:blocks_)total+=item.second.bytes;return total;
}
// Full allocations are released only when no page still refers to them.
int GuestMemory::drop_page(std::map<std::uint64_t,Page>::iterator page) {
  auto block=blocks_.find(page->second.block);
  if(block==blocks_.end() || !block->second.pages)return EFAULT;
  if(block->second.pages==1) {
#ifdef __PROSPERO__
    if(sceKernelReleaseDirectMemory(block->second.backing,block->second.bytes))return EIO;
#endif
    blocks_.erase(block);
  } else --block->second.pages;
  pages_.erase(page);return 0;
}
int GuestMemory::protect_run(std::uint64_t backing,std::size_t bytes) {
  for(const auto& view:views) {
    if(backing<view.backing || backing-view.backing>=view.size)continue;
    const auto offset=view.start+backing-view.backing;
    (void)offset;
    const auto protection=[&](std::size_t n){
      return alias_protection(access_,view,backing+n,host_page/guest_page,committed,read,write,guard,guest_page);
    };
    for(std::size_t n=0;n<bytes;) {
      const int prot=protection(n);std::size_t length=host_page;
      while(n+length<bytes && protection(n+length)==prot)length+=host_page;
      if(mprotect(static_cast<unsigned char*>(base_)+offset+n,length,prot))return errno;
      n+=length;
    }
  }
  return 0;
}
int GuestMemory::allocate_run(std::uint64_t backing,std::size_t bytes) {
  if(bytes>limit_-charged_locked())return ENOMEM;
  const auto id=next_block_++;
  // Allocate all bookkeeping before asking the kernel for backing.
  try {
    blocks_.emplace(id,Block{-1,bytes,bytes/host_page});
    for(std::size_t n=0;n<bytes;n+=host_page)pages_.emplace(backing+n,Page{-1,id});
  } catch(const std::bad_alloc&) {
    for(std::size_t n=0;n<bytes;n+=host_page)pages_.erase(backing+n);
    blocks_.erase(id);return ENOMEM;
  }
  auto cancel=[&]{for(std::size_t n=0;n<bytes;n+=host_page)pages_.erase(backing+n);blocks_.erase(id);};
#ifdef __PROSPERO__
  auto& block=blocks_.at(id);
  const int error=sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),bytes,host_page,11,&block.backing);
  if(error || av2_boot_trace_pages())av2_boot_log("direct-allocation","offset=%lld bytes=%zu type=11 result=%x",static_cast<long long>(block.backing),bytes,error);
  if(error){cancel();return ENOMEM;}
  for(std::size_t n=0;n<bytes;n+=host_page)pages_.at(backing+n).backing=block.backing+n;
#else
  const int fd=memfd_create("av2-guest-block",MFD_CLOEXEC);
  if(fd<0){const int e=errno;cancel();return e;}
  if(ftruncate(fd,bytes)){const int e=errno;(void)close(fd);cancel();return e;}
#endif
  int result=0;bool first=true;
  for(const auto& view:views) {
    if(backing<view.backing || backing-view.backing>=view.size)continue;
    void* address=static_cast<unsigned char*>(base_)+view.start+backing-view.backing;
#ifdef __PROSPERO__
    void* requested=address;
    const int error=sceKernelMapDirectMemory(&address,bytes,PROT_READ|PROT_WRITE,0x10,block.backing,host_page);
    if(error || address!=requested || av2_boot_trace_pages())av2_boot_log("direct-map","requested=%p actual=%p bytes=%zu flags=10 result=%x",requested,address,bytes,error);
    if(error || address!=requested){result=EIO;break;}
#else
    if(mmap(address,bytes,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,0)==MAP_FAILED){result=errno;break;}
#endif
    if(first){std::memset(address,0,bytes);first=false;}
    // Permissions can differ between aliases and mixed guest pages. Merge only
    // consecutive host pages with exactly the same effective protection.
    for(std::size_t n=0;n<bytes;) {
      const auto protection=[&](std::size_t offset){
        return alias_protection(access_,view,backing+offset,host_page/guest_page,committed,read,write,guard,guest_page);
      };
      const int prot=protection(n);std::size_t length=host_page;
      while(n+length<bytes && protection(n+length)==prot)length+=host_page;
      if(prot!=(PROT_READ|PROT_WRITE) && mprotect(static_cast<unsigned char*>(address)+n,length,prot)){result=errno;break;}
      n+=length;
    }
    if(result)break;
  }
#ifndef __PROSPERO__
  (void)close(fd);
#endif
  // On a mapping failure retain every allocation for whole-arena release.
  if(result)poisoned_=true;
  return result;
}

int GuestMemory::reconcile(std::uint64_t backing) {
  std::array<std::uint64_t, 9> aliases{};
  std::array<int, 9> protections{};
  std::size_t count = 0;
  bool needed = false;
  for (const auto& view : views) {
    if (backing < view.backing || backing - view.backing >= view.size) continue;
    const auto at = view.start + backing - view.backing;
    aliases[count] = at;
    for (std::size_t i = 0; i < host_page / guest_page; ++i)
      needed |= (access_[at / guest_page + i] & committed) != 0;
    protections[count] = alias_protection(access_, view, backing, host_page / guest_page,
                                          committed, read, write, guard, guest_page);
    ++count;
  }
  auto found = pages_.find(backing);
  if (!needed) {
    if (found == pages_.end()) return 0;
    for (std::size_t i = 0; i < count; ++i) {
      void* address = static_cast<unsigned char*>(base_) + aliases[i];
      void* requested = address;
      const int error = reserve_at(address, host_page, true);
      if (error || address != requested) return error ? error : EFAULT;
    }
    return drop_page(found);
  }
  if (found == pages_.end()) return allocate_run(backing,host_page);
  for (std::size_t i = 0; i < count; ++i)
    if (mprotect(static_cast<unsigned char*>(base_) + aliases[i], host_page, protections[i])) {
      poisoned_ = true; return errno;
    }
  return 0;
}
std::uint8_t GuestMemory::host_access(std::uint64_t offset) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return base_ && offset<span ? access_[offset/guest_page] : 0;
}
bool GuestMemory::check_access(std::uint64_t offset, std::size_t bytes, bool writing) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::uint64_t address = 0;
  if (!base_ || poisoned_ || !corrected(offset, bytes, address)) return false;
  for (auto page = address / guest_page; page <= (address + bytes - 1) / guest_page; ++page) {
    const auto access = access_[page];
    if ((access & guard) || !(access & (writing ? write : read))) return false;
  }
  return true;
}
std::size_t GuestMemory::charged_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return charged_locked();
}
int GuestMemory::release() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (base_ && munmap(base_, span)) return errno;
  base_ = nullptr;
  // Retain failed whole allocations for retry and honest accounting.
  for(auto it=blocks_.begin();it!=blocks_.end();) {
#ifdef __PROSPERO__
    if(sceKernelReleaseDirectMemory(it->second.backing,it->second.bytes))return EIO;
#endif
    it=blocks_.erase(it);
  }
  pages_.clear();
  std::vector<std::uint8_t>().swap(access_);
  limit_ = 0; poisoned_ = false;
  return 0;
}
} // namespace av2::platform
