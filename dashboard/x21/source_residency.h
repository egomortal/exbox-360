/* Presenter-thread source-byte residency. GPU allocation remains the existing
 * mapped source buffer. No caller pointer survives successful preparation. */
#pragma once
#include <string.h>
#include <stdint.h>
#define EXBOX_SOURCE_ENTRIES 8192u
typedef struct {const void*data;uint32_t bytes,offset;uint64_t hash,lease;} SourceSpan;
typedef struct {uint32_t offset,bytes;uint64_t hash,age,lease;ExboxGpuSourceKey key;const void*pending;unsigned pinned;} SourceEntry;
typedef struct {
 SourceEntry live[EXBOX_SOURCE_ENTRIES],plan[EXBOX_SOURCE_ENTRIES];
 SourceSpan spans[EXBOX_GPU2D_MAX_COMMANDS];size_t count,planned;uint64_t clock;
 ExboxGpuSourceStats stats;
} SourceCache;
static inline uint64_t source_rot(uint64_t x,unsigned n){return (x<<n)|(x>>(64-n));}
static uint64_t source_hash(const void*p,size_t n){
#ifdef EXBOX_SOURCE_TEST_COLLISION
 (void)p;(void)n;return 1;
#else
 const unsigned char*b=p;uint64_t a=0x9e3779b185ebca87ull,c=0xc2b2ae3d27d4eb4full,d=0x165667b19e3779f9ull,e=0x85ebca77c2b2ae63ull;size_t left=n;
 while(left>=32){uint64_t w[4];memcpy(w,b,32);a=source_rot(a+w[0]*0xc2b2ae3d27d4eb4full,31)*0x9e3779b185ebca87ull;c=source_rot(c+w[1]*0xc2b2ae3d27d4eb4full,31)*0x9e3779b185ebca87ull;d=source_rot(d+w[2]*0xc2b2ae3d27d4eb4full,31)*0x9e3779b185ebca87ull;e=source_rot(e+w[3]*0xc2b2ae3d27d4eb4full,31)*0x9e3779b185ebca87ull;b+=32;left-=32;}
 uint64_t h=source_rot(a,1)+source_rot(c,7)+source_rot(d,12)+source_rot(e,18)+(uint64_t)n;
 while(left>=8){uint64_t w;memcpy(&w,b,8);h=source_rot(h^(w*0xc2b2ae3d27d4eb4full),27)*0x9e3779b185ebca87ull;b+=8;left-=8;}
 while(left--){h=source_rot(h^(*b++*0x165667b19e3779f9ull),11)*0x9e3779b185ebca87ull;}h^=h>>33;h*=0xc2b2ae3d27d4eb4full;h^=h>>29;return h;
#endif
}
static void source_remove(SourceCache*c,size_t i){memmove(c->plan+i,c->plan+i+1,(c->planned-i-1)*sizeof(SourceEntry));--c->planned;}
static int source_gap(SourceCache*c,uint32_t bytes,uint32_t capacity,uint32_t*out,size_t*where){uint32_t cursor=0;for(size_t i=0;i<c->planned;i++){if(c->plan[i].offset-cursor>=bytes){*out=cursor;*where=i;return 1;}cursor=c->plan[i].offset+c->plan[i].bytes;}if(capacity-cursor>=bytes){*out=cursor;*where=c->planned;return 1;}return 0;}
static int source_plan(SourceCache*c,const unsigned char*mapped,uint32_t capacity,size_t n,uint64_t tick,ExboxGpuSourceStats*delta){
 for(size_t i=0;i<n;i++){SourceSpan*s=c->spans+i;s->offset=0;if(!s->bytes)continue;size_t match=c->planned;
  for(size_t j=0;j<c->planned;j++){SourceEntry*e=c->plan+j;if(s->lease){if(e->lease==s->lease&&e->bytes==s->bytes){match=j;break;}continue;}if(e->lease||e->hash!=s->hash||e->bytes!=s->bytes)continue;delta->verified_bytes+=s->bytes;const void*prior=e->pending?e->pending:mapped+e->offset;if(!memcmp(prior,s->data,s->bytes)){match=j;break;}}
  if(match<c->planned){SourceEntry*e=c->plan+match;if(s->lease)delta->lease_hits++;else if(e->pending)delta->in_packet_hits++;else delta->hits++;e->pinned=1;e->age=tick;s->offset=e->offset;continue;}
  uint32_t offset=0;size_t where=0;
  while(!source_gap(c,s->bytes,capacity,&offset,&where)||c->planned==EXBOX_SOURCE_ENTRIES){size_t victim=c->planned;for(size_t j=0;j<c->planned;j++)if(!c->plan[j].pinned&&!c->plan[j].lease&&(victim==c->planned||c->plan[j].age<c->plan[victim].age))victim=j;if(victim==c->planned)return 0;source_remove(c,victim);delta->evictions++;}
  memmove(c->plan+where+1,c->plan+where,(c->planned-where)*sizeof(SourceEntry));c->plan[where]=(SourceEntry){.offset=offset,.bytes=s->bytes,.hash=s->hash,.age=tick,.pending=s->data,.pinned=1};c->planned++;s->offset=offset;delta->misses++;delta->uploaded_bytes+=s->bytes;
 }return 1;
}
static int source_apply(SourceCache*c,void*mapped,size_t capacity,size_t n){
 if(!c||!mapped||!capacity||capacity>UINT32_MAX||n>EXBOX_GPU2D_MAX_COMMANDS)return 0;
 size_t total=0;uintptr_t map=(uintptr_t)mapped;
 for(size_t i=0;i<n;i++){SourceSpan*s=c->spans+i;
 if(s->lease){size_t j=0;for(;j<c->count;j++)if(c->live[j].lease==s->lease)break;if(j==c->count||c->live[j].bytes!=s->bytes||s->data!=(const unsigned char*)mapped+c->live[j].offset)return 0;continue;}
 if(s->bytes){uintptr_t p=(uintptr_t)s->data;if(!p||s->bytes%4||s->bytes>capacity-total||p>UINTPTR_MAX-s->bytes||map>UINTPTR_MAX-capacity||(p<map+capacity&&p+s->bytes>map))return 0;total+=s->bytes;s->hash=source_hash(s->data,s->bytes);}}
 memcpy(c->plan,c->live,c->count*sizeof(SourceEntry));c->planned=c->count;uint64_t tick=c->clock;if(tick==UINT64_MAX){tick>>=1;for(size_t j=0;j<c->planned;j++)c->plan[j].age>>=1;}++tick;
 ExboxGpuSourceStats delta={0};delta.hashed_bytes=total;
 if(!source_plan(c,mapped,(uint32_t)capacity,n,tick,&delta)){c->planned=0;for(size_t j=0;j<c->count;j++)if(c->live[j].lease)c->plan[c->planned++]=c->live[j];memset(&delta,0,sizeof delta);delta.hashed_bytes=total;delta.evictions=c->count-c->planned;delta.compactions=1;if(!source_plan(c,mapped,(uint32_t)capacity,n,tick,&delta))return 0;}
 // All failure decisions precede these writes. Previous submission must be complete.
 size_t resident=0;for(size_t j=0;j<c->planned;j++){SourceEntry*e=c->plan+j;if(e->pending)memcpy((unsigned char*)mapped+e->offset,e->pending,e->bytes);e->pending=NULL;e->pinned=0;resident+=e->bytes;}
 memcpy(c->live,c->plan,c->planned*sizeof(SourceEntry));c->count=c->planned;c->clock=tick;
#define ADD(field) c->stats.field+=delta.field
 ADD(lease_hits);ADD(hits);ADD(in_packet_hits);ADD(misses);ADD(uploaded_bytes);ADD(hashed_bytes);ADD(verified_bytes);ADD(evictions);ADD(compactions);
#undef ADD
 c->stats.last_uploaded_bytes=delta.uploaded_bytes;c->stats.last_hits=delta.hits;c->stats.last_misses=delta.misses;c->stats.resident_bytes=resident;c->stats.entries=c->count;c->stats.cpu_metadata_bytes=sizeof *c;
 return 1;
}
static void source_epoch(SourceCache*c,uint64_t epoch){if(c->stats.epoch==epoch)return;c->stats.epoch=epoch;c->stats.invalidations++;c->count=c->planned=0;c->clock=0;c->stats.resident_bytes=c->stats.entries=0;}

static SourceEntry* source_find(SourceCache*c,uint64_t lease){if(!lease)return NULL;for(size_t i=0;i<c->count;i++)if(c->live[i].lease==lease)return c->live+i;return NULL;}
static void source_update_size(SourceCache*c){size_t bytes=0;for(size_t i=0;i<c->count;i++)bytes+=c->live[i].bytes;c->stats.resident_bytes=bytes;c->stats.entries=c->count;c->stats.cpu_metadata_bytes=sizeof*c;}
