#include "native_2d_client.h"
#include "native_gpu.h"
#include "native_session.h"
#include "avatar_gpu.h"
#include "source_client.h"
#include "filtered_c.h"
#include <stdlib.h>
#include <stdint.h>
enum {READY=1,PREPARED=2,BUSY=3,RETAINED=4};
struct ExboxNative2D {ExboxSourceClient sources;ExboxFilteredOwner* filtered;ExboxGpu2D* helper;ExboxAvatarGpu* avatar;ExboxGpuBudget* budget;uint32_t avatar_w,avatar_h;int avatar_active;VkDevice device;PFN_vkGetDeviceProcAddr get;uint32_t width,height;size_t bytes;int state,records;size_t actor_count,actor_indices[4],command_count;};
static ExboxNative2D* owner;
static int bound(ExboxNative2D* c){
 ExboxNativeGpuContext ctx;
 if(!exbox_native_gpu_context(&ctx)||ctx.device!=c->device||ctx.get!=c->get||ctx.width!=c->width||ctx.height!=c->height){c->state=RETAINED;return 0;}
 return 1;
}
ExboxNative2D* exbox_native2d_create_with_budget(ExboxGpuBudget* budget){
 ExboxNativeGpuContext ctx;if(owner||!exbox_native_gpu_context(&ctx))return NULL;
 if(!ctx.budget||(budget&&budget!=ctx.budget))return NULL;budget=ctx.budget;
 if(!exbox_native_retire_upload())return NULL;
 if(!ctx.device||!ctx.get||!(ctx.queue_flags&VK_QUEUE_COMPUTE_BIT)||!ctx.width||!ctx.height||ctx.width>EXBOX_GPU2D_WIDTH||ctx.height>EXBOX_GPU2D_HEIGHT)return NULL;
 ExboxNative2D*c=calloc(1,sizeof *c);if(!c)return NULL;
 c->helper=exbox_gpu2d_create_with_budget(ctx.width,ctx.height,ctx.device,&ctx.memory,&ctx.properties,ctx.queue_flags,ctx.get,budget);
 if(!c->helper){free(c);return NULL;}
 if(!exbox_gpu2d_output(c->helper)){exbox_gpu2d_destroy(c->helper);free(c);return NULL;}
 c->budget=budget;c->device=ctx.device;c->get=ctx.get;c->width=ctx.width;c->height=ctx.height;c->bytes=(size_t)c->width*c->height*4;c->state=READY;owner=c;return c;
}
static int prepare_slots(ExboxNative2D*c,const ExboxGpuCommand* commands,size_t count){
 for(size_t i=0;i<count;i++)if(commands[i].kind==EXBOX_GPU_GUIDE_SAVE_BLACK&&!exbox_gpu2d_enable_guide(c->helper))return 0;
 for(size_t i=0;i<count;i++)if(commands[i].kind==EXBOX_GPU_BEGIN_SLOT)return exbox_gpu2d_enable_custom_slot(c->helper);
 return 1;
}
int exbox_native2d_prepare_sources(ExboxNative2D*c,const ExboxGpuCommand* commands,size_t count,const uint64_t*ids){
 if(!c||c!=owner||(c->state!=READY&&c->state!=PREPARED)||!bound(c))return 0;
 if(!exbox_native_retire_upload())return 0;
 c->state=READY;
 if(!prepare_slots(c,commands,count)||!exbox_source_client_prepare(&c->sources,c->helper,commands,count,ids)||!exbox_gpu2d_prepare_leased(c->helper,commands,count,c->sources.frame,0))return 0;
 c->avatar_active=0;c->actor_count=0;
 c->state=PREPARED;return 1;
}
int exbox_native2d_prepare_avatar_sources(ExboxNative2D*c,const ExboxGpuCommand*commands,size_t count,const uint32_t*words,size_t word_count,uint32_t w,uint32_t h,uint64_t generation,const uint64_t*ids){
 if(!c||c!=owner||(c->state!=READY&&c->state!=PREPARED)||!bound(c)||!words||!word_count||!generation||!w||!h||w>1200||h>1880)return 0;
 if(!exbox_native_retire_upload())return 0;
 c->state=READY;c->avatar_active=0;c->actor_count=0;if(!prepare_slots(c,commands,count)||!exbox_source_client_prepare(&c->sources,c->helper,commands,count,ids))return 0;ExboxNativeGpuContext ctx;if(!exbox_native_gpu_context(&ctx))return 0;
 if(c->avatar&&(c->avatar_w!=w||c->avatar_h!=h)){exbox_avatar_destroy(c->avatar);c->avatar=NULL;}
 if(!c->avatar){c->avatar=exbox_avatar_create_with_budget(w,h,ctx.device,&ctx.memory,&ctx.properties,ctx.queue_flags,ctx.get,c->budget);if(!c->avatar)return 0;c->avatar_w=w;c->avatar_h=h;}
 if(!exbox_avatar_prepare(c->avatar,words,word_count))return 0;
 if(!exbox_gpu2d_bind_external(c->helper,exbox_avatar_output(c->avatar),(size_t)w*h*4,w,h,w*4,generation)||!exbox_gpu2d_prepare_leased(c->helper,commands,count,c->sources.frame,generation))return 0;
 c->avatar_active=1;c->state=PREPARED;return 1;
}
static void client_record(void*user,VkCommandBuffer command){
 ExboxNative2D*c=user;if(c!=owner||c->state!=BUSY)return;
 if(++c->records!=1)return;
 if(c->filtered&&exbox_filtered_phase(c->filtered)==EXBOX_FILTERED_PREPARED&&!exbox_filtered_record(c->filtered,command)){c->records=-1;return;}
 if(c->actor_count){
  if(!exbox_gpu2d_record_range(c->helper,command,0,c->actor_indices[0],1)){c->records=-1;return;}
  for(size_t i=0;i<c->actor_count;++i){
   size_t end=i+1<c->actor_count?c->actor_indices[i+1]:c->command_count;
   if(!exbox_avatar_record_actor(c->avatar,command,i)||!exbox_gpu2d_record_range(c->helper,command,c->actor_indices[i],end-c->actor_indices[i],0)){c->records=-1;return;}
  }
 }else{
  if(c->avatar_active)exbox_avatar_record(c->avatar,command);
  exbox_gpu2d_record(c->helper,command);
 }
}
int exbox_native2d_present(ExboxNative2D*c){
 if(!c||c!=owner||c->state!=PREPARED||!bound(c))return 0;
 VkBuffer output=exbox_gpu2d_output(c->helper);if(!output)return 0;
 c->state=BUSY;c->records=0;
 const int ok=exbox_native_present_gpu(output,c->bytes,client_record,c);
 if(!ok||c->records!=1){if(c->filtered&&exbox_filtered_phase(c->filtered)==EXBOX_FILTERED_RECORDED)exbox_filtered_complete_after_wait(c->filtered,0);c->state=RETAINED;return 0;}
 exbox_gpu2d_complete_after_wait(c->helper);
 if(c->filtered&&exbox_filtered_phase(c->filtered)==EXBOX_FILTERED_RECORDED&&!exbox_filtered_complete_after_wait(c->filtered,1)){c->state=RETAINED;return 0;}
 c->state=READY;return 1;
}
int exbox_native2d_retained(const ExboxNative2D*c){return !c||c!=owner||c->state==RETAINED;}
int exbox_native2d_destroy(ExboxNative2D**p){
 if(!p)return 0;
 if(!*p)return 1;
 ExboxNative2D*c=*p;if(c!=owner||c->state==BUSY||c->state==RETAINED||!bound(c))return 0;
 if(c->filtered&&!exbox_filtered_owner_destroy(c->filtered)){c->state=RETAINED;return 0;}
 if(c->avatar)exbox_avatar_destroy(c->avatar);
 exbox_gpu2d_destroy(c->helper);owner=NULL;*p=NULL;free(c);return 1;
}

ExboxNative2D* exbox_native2d_create(void){return exbox_native2d_create_with_budget(NULL);}

int exbox_native2d_prepare(ExboxNative2D*c,const ExboxGpuCommand*commands,size_t count){return exbox_native2d_prepare_sources(c,commands,count,NULL);}
int exbox_native2d_prepare_avatar(ExboxNative2D*c,const ExboxGpuCommand*commands,size_t count,const uint32_t*words,size_t word_count,uint32_t w,uint32_t h,uint64_t generation){return exbox_native2d_prepare_avatar_sources(c,commands,count,words,word_count,w,h,generation,NULL);}

static int filtered_unbind(void*context,unsigned slot){ExboxNative2D*c=context;return exbox_gpu2d_unbind_filtered(c->helper,slot);}
static int prepare_actors(ExboxNative2D*c,const ExboxGpuCommand*commands,size_t count,const uint64_t*ids,const ExboxNativeAvatar*actors,size_t actor_count,uint32_t w,uint32_t h,uint64_t generation){
 if(!actors||!actor_count||actor_count>4||!w||!h||w>1200||h>1880||!generation)return 0;
 size_t external=0;for(size_t i=0;i<count;++i)if(commands[i].kind==EXBOX_GPU_EXTERNAL)++external;if(external!=actor_count)return 0;
 ExboxAvatarInput inputs[4];for(size_t i=0;i<actor_count;++i){if(actors[i].command_index>=count||commands[actors[i].command_index].kind!=EXBOX_GPU_EXTERNAL||(i&&actors[i].command_index<=actors[i-1].command_index))return 0;inputs[i]=actors[i].input;}
 c->state=READY;c->avatar_active=0;c->actor_count=0;
 if(!prepare_slots(c,commands,count)||!exbox_source_client_prepare(&c->sources,c->helper,commands,count,ids))return 0;
 ExboxNativeGpuContext ctx;if(!exbox_native_gpu_context(&ctx))return 0;
 if(c->avatar&&(c->avatar_w!=w||c->avatar_h!=h)){exbox_avatar_destroy(c->avatar);c->avatar=NULL;}
 if(!c->avatar){c->avatar=exbox_avatar_create_with_budget(w,h,ctx.device,&ctx.memory,&ctx.properties,ctx.queue_flags,ctx.get,c->budget);if(!c->avatar)return 0;c->avatar_w=w;c->avatar_h=h;}
 if(!exbox_avatar_prepare_group(c->avatar,inputs,actor_count)||!exbox_gpu2d_bind_external(c->helper,exbox_avatar_output(c->avatar),(size_t)w*h*4,w,h,w*4,generation)||!exbox_gpu2d_prepare_leased(c->helper,commands,count,c->sources.frame,generation))return 0;
 for(size_t i=0;i<actor_count;++i)c->actor_indices[i]=actors[i].command_index;
 c->actor_count=actor_count;c->command_count=count;c->avatar_active=1;c->state=PREPARED;return 1;
}
static int prepare_frame(ExboxNative2D*c,const ExboxGpuCommand*commands,size_t count,const uint64_t*ids,const uint32_t*words,size_t word_count,uint32_t w,uint32_t h,uint64_t generation,const ExboxFilteredRequest*requests,size_t request_count,const ExboxNativeAvatar*actors,size_t actor_count){
 if(!c||c!=owner||(c->state!=READY&&c->state!=PREPARED)||!bound(c)||count>EXBOX_GPU2D_MAX_COMMANDS||request_count>EXBOX_FILTERED_MAX_SLOTS||(count&&!commands)||(request_count&&!requests))return 0;
 if(exbox_filtered_capacity()!=EXBOX_FILTERED_MAX_SLOTS||exbox_gpu2d_filtered_capacity()!=EXBOX_FILTERED_MAX_SLOTS)return 0;
 if(!exbox_native_retire_upload())return 0;
 size_t required=0;if(!exbox_gpu2d_validate(commands,count,&required))return 0;
 c->state=READY;ExboxGpuCommand* adjusted=NULL;const ExboxGpuCommand*use=commands;
 if(request_count||c->filtered){
  ExboxNativeGpuContext ctx;if(!exbox_native_gpu_context(&ctx))return 0;
  if(!c->filtered)c->filtered=exbox_filtered_owner_create(ctx.device,&ctx.memory,&ctx.properties,ctx.queue_flags,ctx.get,c->budget,filtered_unbind,c);
  if(!c->filtered)return 0;
  if(exbox_filtered_phase(c->filtered)==EXBOX_FILTERED_PREPARED&&!exbox_filtered_cancel_unsubmitted(c->filtered)){c->state=RETAINED;return 0;}
  ExboxFilteredBinding bindings[EXBOX_FILTERED_MAX_SLOTS];size_t produced=0;
  if(!exbox_filtered_prepare(c->filtered,requests,request_count,bindings,EXBOX_FILTERED_MAX_SLOTS,&produced)||produced!=request_count)return 0;
  for(size_t i=0;i<produced;i++)if(!exbox_gpu2d_bind_filtered(c->helper,(unsigned)i,bindings[i].buffer,(size_t)bindings[i].bytes,bindings[i].width,bindings[i].height,bindings[i].pitch,bindings[i].generation)){exbox_filtered_cancel_unsubmitted(c->filtered);return 0;}
  if(count){adjusted=malloc(count*sizeof(*adjusted));if(!adjusted){exbox_filtered_cancel_unsubmitted(c->filtered);return 0;}memcpy(adjusted,commands,count*sizeof(*adjusted));use=adjusted;}
  for(size_t i=0;i<count;i++)if(use[i].kind==EXBOX_GPU_FILTERED){unsigned slot=(unsigned)use[i].transform[9];if(slot>=produced){free(adjusted);exbox_filtered_cancel_unsubmitted(c->filtered);return 0;}adjusted[i].bytes=(size_t)bindings[slot].generation;}
 }
 int ok=actor_count?prepare_actors(c,use,count,ids,actors,actor_count,w,h,generation):word_count?exbox_native2d_prepare_avatar_sources(c,use,count,words,word_count,w,h,generation,ids):exbox_native2d_prepare_sources(c,use,count,ids);free(adjusted);
 if(!ok&&c->filtered&&exbox_filtered_phase(c->filtered)==EXBOX_FILTERED_PREPARED)exbox_filtered_cancel_unsubmitted(c->filtered);
 return ok;
}
int exbox_native2d_prepare_frame(ExboxNative2D*c,const ExboxGpuCommand*commands,size_t count,const uint64_t*ids,const uint32_t*words,size_t word_count,uint32_t w,uint32_t h,uint64_t generation,const ExboxFilteredRequest*requests,size_t request_count){return prepare_frame(c,commands,count,ids,words,word_count,w,h,generation,requests,request_count,NULL,0);}
int exbox_native2d_prepare_actors_frame(ExboxNative2D*c,const ExboxGpuCommand*commands,size_t count,const uint64_t*ids,const ExboxNativeAvatar*actors,size_t actor_count,uint32_t w,uint32_t h,uint64_t generation,const ExboxFilteredRequest*requests,size_t request_count){if(!actors||!actor_count||actor_count>4)return 0;return prepare_frame(c,commands,count,ids,NULL,0,w,h,generation,requests,request_count,actors,actor_count);}
