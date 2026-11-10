#pragma once
#include "gpu2d.h"
#include <stdint.h>
#include <string.h>
/* Serial presenter owner only. Called after previous full-frame completion.
 * Producer IDs are immutable owned-publication IDs, never surface addresses.
 * Registrations used by the current frame remain live through its submission. */
typedef struct {uint64_t id;size_t bytes;ExboxGpuSourceLease lease;} ExboxSourceRegistration;
typedef struct {
 ExboxSourceRegistration entries[EXBOX_GPU2D_MAX_COMMANDS];size_t count;
 ExboxGpuSourceLease frame[EXBOX_GPU2D_MAX_COMMANDS];
 uint64_t published_bytes,publications,reuses,retirements;
} ExboxSourceClient;
static inline int exbox_source_client_prepare(ExboxSourceClient*s,ExboxGpu2D*g,const ExboxGpuCommand*c,size_t n,const uint64_t*ids){
 if(!s||!g||n>EXBOX_GPU2D_MAX_COMMANDS||(n&&!c))return 0;
 size_t bytes;if(!exbox_gpu2d_validate(c,n,&bytes))return 0;
 /* A removed registration cannot be referenced by the completed old frame or
  * this incoming frame. UI queued packets retain their own immutable bytes. */
 for(size_t i=s->count;i-->0;){int keep=0;if(ids)for(size_t j=0;j<n;j++)if(ids[j]==s->entries[i].id){keep=1;break;}
  if(!keep){if(!exbox_gpu2d_source_retire(g,s->entries[i].lease))return 0;s->entries[i]=s->entries[--s->count];s->retirements++;}
 }
 memset(s->frame,0,n*sizeof(s->frame[0]));
 if(!ids)return 1;
 for(size_t i=0;i<n;i++)if(ids[i]){
  if(!c[i].pixels||!c[i].bytes||!((c[i].kind>=EXBOX_GPU_TEXTURE&&c[i].kind<=EXBOX_GPU_NINE)||c[i].kind==EXBOX_GPU_DIRECT_LANCZOS||c[i].kind==EXBOX_GPU_VIDEO_Q8||c[i].kind==EXBOX_GPU_VERTICAL_STRIP||c[i].kind==EXBOX_GPU_NXE_REFLECTION))return 0;
  size_t j=0;for(;j<s->count;j++)if(s->entries[j].id==ids[i])break;
  if(j<s->count){if(s->entries[j].bytes!=c[i].bytes)return 0;s->frame[i]=s->entries[j].lease;s->reuses++;continue;}
  if(s->count==EXBOX_GPU2D_MAX_COMMANDS)return 0;
  ExboxGpuSourceLease lease=0;ExboxGpuSourceKey key={1,1,ids[i]};
  if(!exbox_gpu2d_source_publish(g,key,c[i].pixels,c[i].bytes,&lease))return 0;
  s->entries[s->count++]=(ExboxSourceRegistration){ids[i],c[i].bytes,lease};s->frame[i]=lease;s->publications++;s->published_bytes+=c[i].bytes;
 }
 return 1;
}
