// Native link wrapper observes successful thread creation without changing
// attributes, start routines, return codes or runtime scheduling semantics.
// Delegate to the SDK stack/guard/MXCSR wrapper, never directly to libc.
#include "boot_log.h"
#include <pthread.h>
extern "C" int av2_platform_pthread_create(pthread_t*,const pthread_attr_t*,void*(*)(void*),void*);
extern "C" int __wrap_pthread_create(pthread_t* thread,const pthread_attr_t* attr,void*(*start)(void*),void* arg){
 const int result=av2_platform_pthread_create(thread,attr,start,arg);
 av2_boot_thread_created(result);
 return result;
}
