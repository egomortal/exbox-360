#include "filtered_c.h"
// X21 isolated host compositor proof. Readback exists only in this test executable.
#include "gpu2d.h"
#include "avatar_gpu.h"
#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
static void ok(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Vulkan result "+std::to_string(r));}
struct Session {
 ExboxFilteredOwner* filtered=nullptr;
 ExboxGpuBudget budget=EXBOX_GPU_BUDGET_INIT;ExboxAvatarGpu* avatar=nullptr;unsigned avatar_w=0,avatar_h=0;bool avatar_active=false;std::vector<std::size_t> actor_indices;std::size_t actor_commands=0;
 VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};uint32_t family{};
 VkPhysicalDeviceMemoryProperties memory{};VkPhysicalDeviceProperties properties{};VkQueueFlags flags{};
 VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};ExboxGpu2D* compositor{};
 VkBuffer readback{};VkDeviceMemory readback_memory{};size_t readback_bytes{};
 ~Session(){if(device){vkDeviceWaitIdle(device);if(avatar)exbox_avatar_destroy(avatar);exbox_gpu2d_destroy(compositor);if(readback)vkDestroyBuffer(device,readback,nullptr);if(readback_memory)vkFreeMemory(device,readback_memory,nullptr);if(fence)vkDestroyFence(device,fence,nullptr);if(pool)vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);}if(instance)vkDestroyInstance(instance,nullptr);}
 void open(){
  VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.pApplicationName="ExBox X21 host correctness";app.apiVersion=VK_API_VERSION_1_1;
  VkInstanceCreateInfo ii{};ii.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;ii.pApplicationInfo=&app;ok(vkCreateInstance(&ii,nullptr,&instance));
  uint32_t n=0;ok(vkEnumeratePhysicalDevices(instance,&n,nullptr));if(!n)throw std::runtime_error("no Vulkan device; CPU fallback required");std::vector<VkPhysicalDevice> ps(n);ok(vkEnumeratePhysicalDevices(instance,&n,ps.data()));physical=ps[0];vkGetPhysicalDeviceProperties(physical,&properties);vkGetPhysicalDeviceMemoryProperties(physical,&memory);
  vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> qs(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,qs.data());for(;family<n;++family)if(qs[family].queueFlags&VK_QUEUE_COMPUTE_BIT)break;if(family==n)throw std::runtime_error("compute queue unavailable");flags=qs[family].queueFlags;
  float priority=1;VkDeviceQueueCreateInfo qi{};qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;VkDeviceCreateInfo di{};di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;ok(vkCreateDevice(physical,&di,nullptr,&device));vkGetDeviceQueue(device,family,0,&queue);
  VkCommandPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;ok(vkCreateCommandPool(device,&pi,nullptr,&pool));VkCommandBufferAllocateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ci.commandPool=pool;ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ci.commandBufferCount=1;ok(vkAllocateCommandBuffers(device,&ci,&command));VkFenceCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;ok(vkCreateFence(device,&fi,nullptr,&fence));
  std::cout<<"device="<<properties.deviceName<<" one_session=1 swapchains=0 offscreen_test=1\n";
 }
 void extent(unsigned w,unsigned h){
  exbox_gpu2d_destroy(compositor);compositor=nullptr;if(readback)vkDestroyBuffer(device,readback,nullptr);readback={};if(readback_memory)vkFreeMemory(device,readback_memory,nullptr);readback_memory={};
  compositor=exbox_gpu2d_create_with_budget(w,h,device,&memory,&properties,flags,vkGetDeviceProcAddr,&budget);if(!compositor)throw std::runtime_error("GPU capability/budget failure: use CPU fallback");readback_bytes=size_t(w)*h*4;
  VkBufferCreateInfo bi{};bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bi.size=readback_bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;ok(vkCreateBuffer(device,&bi,nullptr,&readback));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,readback,&req);unsigned type=0;for(;type<memory.memoryTypeCount;++type)if((req.memoryTypeBits&(1u<<type))&&(memory.memoryTypes[type].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))break;if(type==memory.memoryTypeCount)throw std::runtime_error("test readback memory unavailable");VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=req.size;ai.memoryTypeIndex=type;ok(vkAllocateMemory(device,&ai,nullptr,&readback_memory));ok(vkBindBufferMemory(device,readback,readback_memory,0));
 }
 std::vector<uint32_t> verify_only_readback(){
  ok(vkResetCommandBuffer(command,0));ok(vkResetFences(device,1,&fence));VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;ok(vkBeginCommandBuffer(command,&begin));if(filtered&&exbox_filtered_phase(filtered)==EXBOX_FILTERED_PREPARED&&!exbox_filtered_record(filtered,command))throw std::runtime_error("filter record refused");if(!actor_indices.empty()){if(!exbox_gpu2d_record_range(compositor,command,0,actor_indices[0],1))throw std::runtime_error("actor prefix record");for(std::size_t i=0;i<actor_indices.size();++i){auto end=i+1<actor_indices.size()?actor_indices[i+1]:actor_commands;if(!exbox_avatar_record_actor(avatar,command,i)||!exbox_gpu2d_record_range(compositor,command,actor_indices[i],end-actor_indices[i],0))throw std::runtime_error("actor interleaving record");}}else{if(avatar_active)exbox_avatar_record(avatar,command);exbox_gpu2d_record(compositor,command);}VkBufferCopy copy{0,0,readback_bytes};vkCmdCopyBuffer(command,exbox_gpu2d_output(compositor),readback,1,&copy);
  VkBufferMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=readback;b.size=VK_WHOLE_SIZE;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);ok(vkEndCommandBuffer(command));VkSubmitInfo si{};si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;si.commandBufferCount=1;si.pCommandBuffers=&command;ok(vkQueueSubmit(queue,1,&si,fence));ok(vkWaitForFences(device,1,&fence,VK_TRUE,30000000000ull));
  void* p=nullptr;ok(vkMapMemory(device,readback_memory,0,readback_bytes,0,&p));std::vector<uint32_t> out(readback_bytes/4);std::memcpy(out.data(),p,readback_bytes);vkUnmapMemory(device,readback_memory);return out;
 }
 std::vector<uint32_t> verify_avatar_only_readback(){
  if(!avatar)return {};size_t bytes=size_t(avatar_w)*avatar_h*4;
  ok(vkResetCommandBuffer(command,0));ok(vkResetFences(device,1,&fence));VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;ok(vkBeginCommandBuffer(command,&begin));
  VkBufferCopy copy{0,0,bytes};vkCmdCopyBuffer(command,exbox_avatar_output(avatar),readback,1,&copy);
  VkBufferMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=readback;b.size=VK_WHOLE_SIZE;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);ok(vkEndCommandBuffer(command));VkSubmitInfo si{};si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;si.commandBufferCount=1;si.pCommandBuffers=&command;ok(vkQueueSubmit(queue,1,&si,fence));ok(vkWaitForFences(device,1,&fence,VK_TRUE,30000000000ull));
  void* p=nullptr;ok(vkMapMemory(device,readback_memory,0,bytes,0,&p));std::vector<uint32_t> out(bytes/4);std::memcpy(out.data(),p,bytes);vkUnmapMemory(device,readback_memory);return out;
 }

};
