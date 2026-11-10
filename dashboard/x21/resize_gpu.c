// SPDX-License-Identifier: GPL-3.0-or-later
#include "resize_gpu.h"
#include "filtered_limits.h"
#include "resize_spv.h"
#include <stdlib.h>
#include <string.h>
#define FN(X) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdPipelineBarrier) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) X(GetFenceStatus)
#define MAX_BYTES (64u*1024u*1024u)
#define SCRATCH_BYTES (4u*1024u*1024u)
#define TABLE_BYTES (4u*1024u*1024u)
typedef struct {VkBuffer b;VkDeviceMemory m;void*map;VkDeviceSize bytes;ExboxGpuReservation reservation;} Buffer;
typedef struct {uint32_t sw,sh,dw,dh,alpha,phase,xbase,tw,base;} Push;
struct ExboxResize {
 VkDevice device;VkPhysicalDeviceMemoryProperties memory;VkPhysicalDeviceLimits limits;ExboxGpuBudget*budget;
 VkDescriptorSetLayout desc;VkPipelineLayout layout;VkPipeline pipe;unsigned jobs,scratch_owners;uint64_t allocated;
#define FIELD(n) PFN_vk##n n;
 FN(FIELD)
#undef FIELD
};
struct ExboxResizeScratch {ExboxResize*g;Buffer buffer;unsigned refs,recorded;VkCommandBuffer command;};
struct ExboxResizeJob {ExboxResizeScratch*shared;ExboxResize*g;Buffer table,scratch,output;VkDescriptorPool pool;VkDescriptorSet set;Push push;uint32_t tile;int state;};
static void drop(ExboxResize*g,Buffer*b){
 if(b->map)g->UnmapMemory(g->device,b->m);
 if(b->b)g->DestroyBuffer(g->device,b->b,0);
 if(b->m)g->FreeMemory(g->device,b->m,0);
 if(b->reservation.owner){g->allocated-=b->reservation.bytes;exbox_gpu_budget_release(g->budget,&b->reservation);}
 memset(b,0,sizeof *b);
}
static int make(ExboxResize*g,Buffer*b,VkDeviceSize bytes,int mapped){
 if(!bytes||bytes>g->limits.maxStorageBufferRange)return 0;
 VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 if(g->CreateBuffer(g->device,&ci,0,&b->b)!=VK_SUCCESS)return 0;
 VkMemoryRequirements req;g->GetBufferMemoryRequirements(g->device,b->b,&req);
 if(req.size<bytes||!exbox_gpu_budget_reserve(g->budget,&b->reservation,req.size))return 0;
 g->allocated+=req.size;uint32_t type=UINT32_MAX;
 VkMemoryPropertyFlags flags=mapped?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
 for(uint32_t i=0;i<g->memory.memoryTypeCount;i++)if((req.memoryTypeBits&(1u<<i))&&(g->memory.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
 if(type==UINT32_MAX)return 0;
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
 if(g->AllocateMemory(g->device,&ai,0,&b->m)!=VK_SUCCESS||g->BindBufferMemory(g->device,b->b,b->m,0)!=VK_SUCCESS)return 0;
 if(mapped&&g->MapMemory(g->device,b->m,0,bytes,0,&b->map)!=VK_SUCCESS)return 0;
 b->bytes=bytes;return 1;
}
ExboxResizeScratch* exbox_resize_scratch_create(ExboxResize*g){if(!g)return 0;ExboxResizeScratch*s=calloc(1,sizeof *s);if(s){s->g=g;g->scratch_owners++;}return s;}
int exbox_resize_scratch_destroy(ExboxResizeScratch*s){if(!s)return 1;if(s->refs||s->recorded)return 0;drop(s->g,&s->buffer);s->g->scratch_owners--;free(s);return 1;}
static void detach_scratch(ExboxResizeJob*j){if(!j->shared)return;ExboxResizeScratch*s=j->shared;if(j->state==1){s->recorded--;if(!s->recorded)s->command=VK_NULL_HANDLE;}s->refs--;if(!s->refs)drop(s->g,&s->buffer);j->shared=0;}
int exbox_resize_destroy(ExboxResize*g){if(!g)return 1;if(g->jobs||g->scratch_owners)return 0;
 if(g->pipe)g->DestroyPipeline(g->device,g->pipe,0);if(g->layout)g->DestroyPipelineLayout(g->device,g->layout,0);if(g->desc)g->DestroyDescriptorSetLayout(g->device,g->desc,0);free(g);return 1;
}
ExboxResize* exbox_resize_create(VkDevice d,const VkPhysicalDeviceMemoryProperties*m,const VkPhysicalDeviceProperties*p,VkQueueFlags flags,PFN_vkGetDeviceProcAddr get,ExboxGpuBudget*budget){
 if(!d||!m||!p||!get||!budget||m->memoryTypeCount>32||!(flags&VK_QUEUE_COMPUTE_BIT)||p->limits.maxPushConstantsSize<sizeof(Push)||p->limits.maxComputeWorkGroupInvocations<64||p->limits.maxComputeWorkGroupSize[0]<64||!p->limits.maxComputeWorkGroupCount[0]||!p->limits.maxComputeWorkGroupCount[1]||!p->limits.maxComputeWorkGroupCount[2]||!p->limits.maxComputeWorkGroupSize[1]||!p->limits.maxComputeWorkGroupSize[2]||!p->limits.maxBoundDescriptorSets||p->limits.maxPerStageResources<4||p->limits.maxPerStageDescriptorStorageBuffers<4||p->limits.maxDescriptorSetStorageBuffers<4||p->limits.maxStorageBufferRange<16)return 0;
 ExboxResize*g=calloc(1,sizeof *g);if(!g)return 0;g->device=d;g->memory=*m;g->limits=p->limits;g->budget=budget;
#define LOAD(n) g->n=(PFN_vk##n)get(d,"vk" #n);if(!g->n){free(g);return 0;}
 FN(LOAD)
#undef LOAD
 VkDescriptorSetLayoutBinding bindings[4]={0};for(uint32_t i=0;i<4;i++)bindings[i]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0};
 VkDescriptorSetLayoutCreateInfo ds={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=4,.pBindings=bindings};if(g->CreateDescriptorSetLayout(d,&ds,0,&g->desc)!=VK_SUCCESS)goto fail;
 VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&g->desc,.pushConstantRangeCount=1,.pPushConstantRanges=&range};if(g->CreatePipelineLayout(d,&li,0,&g->layout)!=VK_SUCCESS)goto fail;
 VkShaderModule shader=VK_NULL_HANDLE;VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof resize_spv,.pCode=resize_spv};if(g->CreateShaderModule(d,&sm,0,&shader)!=VK_SUCCESS)goto fail;
 VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=g->layout};VkResult rc=g->CreateComputePipelines(d,VK_NULL_HANDLE,1,&pi,0,&g->pipe);g->DestroyShaderModule(d,shader,0);if(rc!=VK_SUCCESS)goto fail;return g;
 fail:exbox_resize_destroy(g);return 0;
}
static int valid_table(const uint32_t*t,size_t words,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh){
 uint64_t head=(uint64_t)(dw+dh)*3;if(!t||words<head||words>TABLE_BYTES/4)return 0;
 for(uint32_t i=0;i<dw+dh;i++){uint32_t first=t[i*3],count=t[i*3+1],offset=t[i*3+2],source=i<dw?sw:sh;if(!count||(uint64_t)first+count>source||offset<head||(uint64_t)offset+count>words)return 0;for(uint32_t k=0;k<count;k++){float f;memcpy(&f,t+offset+k,4);if(!__builtin_isfinite(f)||f>2||f< -2)return 0;}}
 return 1;
}
static ExboxResizeJob* prepare(ExboxResize*g,ExboxResizeScratch*shared,VkBuffer source,VkDeviceSize offset,VkDeviceSize available,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,int alpha,const uint32_t*t,size_t words){
 if((shared&&(shared->g!=g||shared->recorded))||!g||!source||g->jobs>=EXBOX_FILTERED_MAX_SLOTS+1||!sw||!sh||!dw||!dh||sw>8192||sh>8192||dw>8192||dh>8192||(alpha!=0&&alpha!=1))return 0;
 uint64_t in=(uint64_t)sw*sh*4,out=(uint64_t)dw*dh*4,align=g->limits.minStorageBufferOffsetAlignment;
 if(in>MAX_BYTES||out>MAX_BYTES||in>available||offset>UINT64_MAX-in||in>g->limits.maxStorageBufferRange||out>g->limits.maxStorageBufferRange||(align&&offset%align))return 0;
 const int identity=sw==dw&&sh==dh;if(!identity&&!valid_table(t,words,sw,sh,dw,dh))return 0;
 ExboxResizeJob*j=calloc(1,sizeof *j);if(!j)return 0;j->g=g;j->push=(Push){sw,sh,dw,dh,(uint32_t)alpha,0,0,0,0};g->jobs++;
 uint64_t cap=g->limits.maxStorageBufferRange<SCRATCH_BYTES?g->limits.maxStorageBufferRange:SCRATCH_BYTES;j->tile=(uint32_t)(cap/(16u*sh));if(j->tile>dw)j->tile=dw;
 if(!j->tile)goto fail;
 if(!make(g,&j->output,out,0))goto fail;
 if(shared&&!identity){
  if(!shared->buffer.b&&!make(g,&shared->buffer,cap,0)){drop(g,&shared->buffer);goto fail;}
  j->shared=shared;shared->refs++;
 }else if(!make(g,&j->scratch,identity?16:(uint64_t)j->tile*sh*16,0))goto fail;
 if(!make(g,&j->table,identity?4:words*4,1))goto fail;
 if(!identity)memcpy(j->table.map,t,words*4);
 VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4};VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&size};if(g->CreateDescriptorPool(g->device,&pool,0,&j->pool)!=VK_SUCCESS)goto fail;
 VkDescriptorSetAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=j->pool,.descriptorSetCount=1,.pSetLayouts=&g->desc};if(g->AllocateDescriptorSets(g->device,&sa,&j->set)!=VK_SUCCESS)goto fail;
 VkDescriptorBufferInfo bi[4]={{source,offset,in},{j->table.b,0,j->table.bytes},{j->shared?j->shared->buffer.b:j->scratch.b,0,j->shared?j->shared->buffer.bytes:j->scratch.bytes},{j->output.b,0,j->output.bytes}};VkWriteDescriptorSet wr[4]={0};for(uint32_t i=0;i<4;i++){wr[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;wr[i].dstSet=j->set;wr[i].dstBinding=i;wr[i].descriptorCount=1;wr[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;wr[i].pBufferInfo=&bi[i];}g->UpdateDescriptorSets(g->device,4,wr,0,0);return j;
 fail:exbox_resize_release(j);return 0;
}
ExboxResizeJob* exbox_resize_prepare(ExboxResize*g,VkBuffer b,VkDeviceSize o,VkDeviceSize n,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,int a,const uint32_t*t,size_t w){return prepare(g,0,b,o,n,sw,sh,dw,dh,a,t,w);}
ExboxResizeJob* exbox_resize_prepare_shared(ExboxResize*g,ExboxResizeScratch*s,VkBuffer b,VkDeviceSize o,VkDeviceSize n,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,int a,const uint32_t*t,size_t w){if(!s)return 0;return prepare(g,s,b,o,n,sw,sh,dw,dh,a,t,w);}
static void dispatch(ExboxResizeJob*j,VkCommandBuffer cb,Push*p,uint32_t n){ExboxResize*g=j->g;uint64_t max=(uint64_t)g->limits.maxComputeWorkGroupCount[0]*64;for(uint32_t base=0;base<n;){uint32_t count=(uint64_t)(n-base)<max?n-base:(uint32_t)max;p->base=base;g->CmdPushConstants(cb,g->layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof *p,p);g->CmdDispatch(cb,(count+63)/64,1,1);base+=count;}}
int exbox_resize_record(ExboxResizeJob*j,VkCommandBuffer cb){if(!j||!cb||j->state)return 0;
 if(j->shared){ExboxResizeScratch*s=j->shared;if(s->command&&s->command!=cb)return 0;s->command=cb;s->recorded++;}
 ExboxResize*g=j->g;Push p=j->push;
 VkMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,0,0,0);
 if(j->shared){VkMemoryBarrier war={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT};g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&war,0,0,0,0);}
 g->CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,g->pipe);g->CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,g->layout,0,1,&j->set,0,0);
 if(p.sw==p.dw&&p.sh==p.dh){p.phase=2;dispatch(j,cb,&p,p.dw*p.dh);}else for(uint32_t x=0;x<p.dw;x+=j->tile){p.xbase=x;p.tw=p.dw-x<j->tile?p.dw-x:j->tile;p.phase=0;dispatch(j,cb,&p,p.tw*p.sh);b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,0,0,0);p.phase=1;dispatch(j,cb,&p,p.tw*p.dh);b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,0,0,0);}
 b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT;g->CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&b,0,0,0,0);j->state=1;return 1;
}
int exbox_resize_complete_after_wait(ExboxResizeJob*j){if(!j)return 0;if(j->state==2)return 1;if(j->state!=1&&j->state!=3)return 0;ExboxResize*g=j->g;if(j->pool)g->DestroyDescriptorPool(g->device,j->pool,0);j->pool=VK_NULL_HANDLE;drop(g,&j->table);detach_scratch(j);drop(g,&j->scratch);j->state=2;return 1;}
int exbox_resize_complete(ExboxResizeJob*j,VkFence fence){if(!j)return 0;if(j->state==2)return 1;if((j->state!=1&&j->state!=3)||!fence||j->g->GetFenceStatus(j->g->device,fence)!=VK_SUCCESS)return 0;return exbox_resize_complete_after_wait(j);}
int exbox_resize_begin_use(ExboxResizeJob*j){if(!j||j->state!=2)return 0;j->state=3;return 1;}
VkBuffer exbox_resize_output(const ExboxResizeJob*j,VkDeviceSize*bytes){if(!j)return VK_NULL_HANDLE;if(bytes)*bytes=j->output.bytes;return j->output.b;}
int exbox_resize_release(ExboxResizeJob*j){if(!j)return 1;if(j->state==1||j->state==3)return 0;ExboxResize*g=j->g;if(j->pool)g->DestroyDescriptorPool(g->device,j->pool,0);drop(g,&j->table);detach_scratch(j);drop(g,&j->scratch);drop(g,&j->output);g->jobs--;free(j);return 1;}
uint64_t exbox_resize_allocated(const ExboxResize*g){return g?g->allocated:0;}
