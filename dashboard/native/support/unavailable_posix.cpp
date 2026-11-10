// Native titles do not provide these calls. Fail explicitly; never fake success.
extern "C" void exbox_native_note(const char*,...);
#include <atomic>
#include <cerrno>
#include <sys/types.h>
#include <cstddef>
namespace { void once(std::atomic_flag& flag,const char* name) {
 if(!flag.test_and_set(std::memory_order_relaxed))exbox_native_note("import called %s (unsupported: ENOSYS)\n",name);
 errno=ENOSYS;
} }
extern "C" pid_t __wrap_fork(){static std::atomic_flag f=ATOMIC_FLAG_INIT;once(f,"fork");return -1;}
extern "C" int __wrap_link(const char*,const char*){static std::atomic_flag f=ATOMIC_FLAG_INIT;once(f,"link");return -1;}
extern "C" int __wrap_symlink(const char*,const char*){static std::atomic_flag f=ATOMIC_FLAG_INIT;once(f,"symlink");return -1;}
extern "C" ssize_t __wrap_readlink(const char*,char*,size_t){static std::atomic_flag f=ATOMIC_FLAG_INIT;once(f,"readlink");return -1;}
extern "C" long __wrap_pathconf(const char*,int){static std::atomic_flag f=ATOMIC_FLAG_INIT;once(f,"pathconf");return -1;}
