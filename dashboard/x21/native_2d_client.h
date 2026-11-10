#pragma once
#include "gpu2d.h"
#include "filtered_c.h"
#include "avatar_gpu.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ExboxNative2D ExboxNative2D;
/* One serial client on the session owner thread. Session already open and
 * MUST outlive client. Never owns device, queue or display. NULL: not created. */
ExboxNative2D* exbox_native2d_create(void);
ExboxNative2D* exbox_native2d_create_with_budget(ExboxGpuBudget*);
/* Copies sources. Failure preserves prepared packet unless ownership lost. */
int exbox_native2d_prepare(ExboxNative2D*,const ExboxGpuCommand*,size_t);
/* Record/submit once via native session. Any present failure quarantines the
 * client/output: no retry/destroy/SDL handoff; owner requests safe title exit. */
int exbox_native2d_present(ExboxNative2D*);
int exbox_native2d_prepare_avatar(ExboxNative2D*,const ExboxGpuCommand*,size_t,const uint32_t*,size_t,uint32_t,uint32_t,uint64_t);
int exbox_native2d_prepare_sources(ExboxNative2D*,const ExboxGpuCommand*,size_t,const uint64_t*);
int exbox_native2d_prepare_avatar_sources(ExboxNative2D*,const ExboxGpuCommand*,size_t,const uint32_t*,size_t,uint32_t,uint32_t,uint64_t,const uint64_t*);
int exbox_native2d_prepare_frame(ExboxNative2D*,const ExboxGpuCommand*,size_t,const uint64_t*,const uint32_t*,size_t,uint32_t,uint32_t,uint64_t,const ExboxFilteredRequest*,size_t);
typedef struct {ExboxAvatarInput input;size_t command_index;} ExboxNativeAvatar;
int exbox_native2d_prepare_actors_frame(ExboxNative2D*,const ExboxGpuCommand*,size_t,const uint64_t*,const ExboxNativeAvatar*,size_t,uint32_t,uint32_t,uint64_t,const ExboxFilteredRequest*,size_t);
/* NULL/unknown pointer refuses resource access too. */
int exbox_native2d_retained(const ExboxNative2D*);
/* BEFORE native_close. Success destroys helper only and sets *p=NULL.
 * Failure retains *p. No borrowed handle is destroyed. */
int exbox_native2d_destroy(ExboxNative2D**);
#ifdef __cplusplus
}
#endif
