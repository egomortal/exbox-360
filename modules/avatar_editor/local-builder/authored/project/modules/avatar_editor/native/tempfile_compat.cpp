// Native SDK exposes mkstemp through a module unavailable to native titles.
// Its source-built platform already supplies mkstemps, bound by radv-link.sh.
// A zero suffix has exactly mkstemp's template/permission/exclusive-create ABI.
#include <cstdlib>
#if defined(__PROSPERO__)
extern "C" int mkstemps(char*,int); // native platform API; omitted by SDK libc headers
#endif
extern "C" int __wrap_mkstemp(char* pattern) {return ::mkstemps(pattern,0);}
