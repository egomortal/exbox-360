// Installed FreeBSD headers/libiconv expect an accessor; native libc exports
// the backing OBJECT __mb_cur_max instead. Read that actual locale value,
// never hard-code ASCII/UTF-8 width. SDK stdlib.h declares both names.
#include <cstdlib>
extern "C" {
extern int __mb_cur_max;
int ___mb_cur_max(void) { return __mb_cur_max; }
}
