/* Forced into every jxrlib file in the PS5 build (-include jxr_compat.h).
 * jxrlib calls two functions without declaring them, which C99 and newer
 * clang refuse: wcslen (from <wchar.h>) and _byteswap_ulong (defined in its
 * own strcodec.c). Declaring them here leaves jxrlib's sources untouched. */
#ifndef NXE_JXR_COMPAT_H
#define NXE_JXR_COMPAT_H
#include <wchar.h>
unsigned int _byteswap_ulong(unsigned int bits);
#endif
