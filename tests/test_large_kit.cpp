#include "../avatar_kit.hpp"
#include <cstdio>
#ifdef __PROSPERO__
#include <sys/mman.h>
static std::size_t direct_live = 0, direct_peak = 0, direct_allocations = 0;
extern "C" std::int64_t sceKernelGetDirectMemorySize(void) { return 12ll << 30; }
extern "C" int sceKernelAllocateDirectMemory(std::int64_t, std::int64_t,
    std::size_t size, std::size_t, int kind, std::int64_t *physical) {
    if (kind != 11) return -1;
    *physical = static_cast<std::int64_t>(++direct_allocations);
    direct_live += size;
    direct_peak = std::max(direct_peak, direct_live);
    return 0;
}
extern "C" int sceKernelMapDirectMemory(void **addr, std::size_t size, int,
    int, std::int64_t, std::size_t) {
    *addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return *addr == MAP_FAILED ? -1 : 0;
}
extern "C" int sceKernelMunmap(void *addr, std::size_t size) { return munmap(addr, size); }
extern "C" int sceKernelReleaseDirectMemory(std::int64_t, std::size_t size) {
    direct_live -= size;
    return 0;
}
#endif

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    {
        avkit::Kit kit;
        if (!kit.load_file(argv[1])) return 1;
        std::printf("parts=%zu textures=%zu bodies=%zu\n",
                    kit.parts.size(), kit.textures.size(), kit.bodies.size());
        if (kit.bodies.size() != 4 || kit.textures.size() != 957) return 3;
    }
#ifdef __PROSPERO__
    std::printf("direct_peak=%zu direct_allocations=%zu direct_live_after_close=%zu\n",
                direct_peak, direct_allocations, direct_live);
    if (direct_peak < (600u << 20)) return 4;
    if (direct_live != 0) return 5;
#endif
    return 0;
}
