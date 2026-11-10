/* SPDX-License-Identifier: GPL-3.0-or-later */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "scanout_policy.h"
#include "display_mode.h"

#include <time.h>


/* RADV is linked statically: load entry points from its ICD, not libvulkan. */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance, const char*);
#include "native_session.h"
#include "native_gpu.h"
extern void exbox_presenter_log(const char* text);
static void log_message(const char* format,...){char line[768];va_list args;va_start(args,format);vsnprintf(line,sizeof line,format,args);va_end(args);exbox_presenter_log(line);}
static PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
static PFN_vkCreateInstance vkCreateInstance;
static PFN_vkDestroyInstance vkDestroyInstance;
static PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
static PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
static PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
static PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
static PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhysicalDeviceDisplayPropertiesKHR;
static PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModePropertiesKHR;
static PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetPhysicalDeviceDisplayPlanePropertiesKHR;
static PFN_vkGetDisplayPlaneSupportedDisplaysKHR vkGetDisplayPlaneSupportedDisplaysKHR;
static PFN_vkGetDisplayPlaneCapabilitiesKHR vkGetDisplayPlaneCapabilitiesKHR;
static PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurfaceKHR;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
static PFN_vkCreateDevice vkCreateDevice;
static PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
static PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
static PFN_vkDestroyDevice vkDestroyDevice;
static PFN_vkGetDeviceQueue vkGetDeviceQueue;
static PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
static PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
static PFN_vkCreateSemaphore vkCreateSemaphore;
static PFN_vkCreateFence vkCreateFence;
static PFN_vkResetFences vkResetFences;
static PFN_vkResetCommandPool vkResetCommandPool;
static PFN_vkCreateCommandPool vkCreateCommandPool;
static PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
static PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
static PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
static PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
static PFN_vkCmdClearColorImage vkCmdClearColorImage;
static PFN_vkEndCommandBuffer vkEndCommandBuffer;
static PFN_vkQueueSubmit vkQueueSubmit;
static PFN_vkQueuePresentKHR vkQueuePresentKHR;
static PFN_vkWaitForFences vkWaitForFences;
static PFN_vkQueueWaitIdle vkQueueWaitIdle;
static PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
static PFN_vkDestroyCommandPool vkDestroyCommandPool;
static PFN_vkDestroyFence vkDestroyFence;
static PFN_vkDestroySemaphore vkDestroySemaphore;
static PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
static PFN_vkCreateRenderPass vkCreateRenderPass;
static PFN_vkDestroyRenderPass vkDestroyRenderPass;
static PFN_vkCreateImageView vkCreateImageView;
static PFN_vkDestroyImageView vkDestroyImageView;
static PFN_vkCreateFramebuffer vkCreateFramebuffer;
static PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
static PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
static PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
static PFN_vkCmdClearAttachments vkCmdClearAttachments;
static PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
static PFN_vkCreateBuffer vkCreateBuffer;
static PFN_vkDestroyBuffer vkDestroyBuffer;
static PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
static PFN_vkAllocateMemory vkAllocateMemory;
static PFN_vkFreeMemory vkFreeMemory;
static PFN_vkBindBufferMemory vkBindBufferMemory;
static PFN_vkMapMemory vkMapMemory;
static PFN_vkUnmapMemory vkUnmapMemory;
static PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
static int load_global(void) {
    int ok = 1;
    vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (!vkEnumerateInstanceExtensionProperties) { log_message("missing vkEnumerateInstanceExtensionProperties\n"); ok = 0; }
    vkCreateInstance = (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    if (!vkCreateInstance) { log_message("missing vkCreateInstance\n"); ok = 0; }
    return ok;
}
static int load_instance(VkInstance instance) {
    int ok = 1;
    vkGetPhysicalDeviceMemoryProperties=(PFN_vkGetPhysicalDeviceMemoryProperties)vk_icdGetInstanceProcAddr(instance,"vkGetPhysicalDeviceMemoryProperties");if(!vkGetPhysicalDeviceMemoryProperties)ok=0;
    vkDestroyInstance = (PFN_vkDestroyInstance)vk_icdGetInstanceProcAddr(instance, "vkDestroyInstance");
    if (!vkDestroyInstance) { log_message("missing vkDestroyInstance\n"); ok = 0; }
    vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vk_icdGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    if (!vkEnumeratePhysicalDevices) { log_message("missing vkEnumeratePhysicalDevices\n"); ok = 0; }
    vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
    if (!vkGetPhysicalDeviceProperties) { log_message("missing vkGetPhysicalDeviceProperties\n"); ok = 0; }
    vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures");
    if (!vkGetPhysicalDeviceFeatures) { log_message("missing vkGetPhysicalDeviceFeatures\n"); ok = 0; }
    vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)vk_icdGetInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties");
    if (!vkEnumerateDeviceExtensionProperties) { log_message("missing vkEnumerateDeviceExtensionProperties\n"); ok = 0; }
    vkGetPhysicalDeviceDisplayPropertiesKHR = (PFN_vkGetPhysicalDeviceDisplayPropertiesKHR)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceDisplayPropertiesKHR");
    if (!vkGetPhysicalDeviceDisplayPropertiesKHR) { log_message("missing vkGetPhysicalDeviceDisplayPropertiesKHR\n"); ok = 0; }
    vkGetDisplayModePropertiesKHR = (PFN_vkGetDisplayModePropertiesKHR)vk_icdGetInstanceProcAddr(instance, "vkGetDisplayModePropertiesKHR");
    if (!vkGetDisplayModePropertiesKHR) { log_message("missing vkGetDisplayModePropertiesKHR\n"); ok = 0; }
    vkGetPhysicalDeviceDisplayPlanePropertiesKHR = (PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
    if (!vkGetPhysicalDeviceDisplayPlanePropertiesKHR) { log_message("missing vkGetPhysicalDeviceDisplayPlanePropertiesKHR\n"); ok = 0; }
    vkGetDisplayPlaneSupportedDisplaysKHR = (PFN_vkGetDisplayPlaneSupportedDisplaysKHR)vk_icdGetInstanceProcAddr(instance, "vkGetDisplayPlaneSupportedDisplaysKHR");
    if (!vkGetDisplayPlaneSupportedDisplaysKHR) { log_message("missing vkGetDisplayPlaneSupportedDisplaysKHR\n"); ok = 0; }
    vkGetDisplayPlaneCapabilitiesKHR = (PFN_vkGetDisplayPlaneCapabilitiesKHR)vk_icdGetInstanceProcAddr(instance, "vkGetDisplayPlaneCapabilitiesKHR");
    if (!vkGetDisplayPlaneCapabilitiesKHR) { log_message("missing vkGetDisplayPlaneCapabilitiesKHR\n"); ok = 0; }
    vkCreateDisplayPlaneSurfaceKHR = (PFN_vkCreateDisplayPlaneSurfaceKHR)vk_icdGetInstanceProcAddr(instance, "vkCreateDisplayPlaneSurfaceKHR");
    if (!vkCreateDisplayPlaneSurfaceKHR) { log_message("missing vkCreateDisplayPlaneSurfaceKHR\n"); ok = 0; }
    vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    if (!vkGetPhysicalDeviceQueueFamilyProperties) { log_message("missing vkGetPhysicalDeviceQueueFamilyProperties\n"); ok = 0; }
    vkGetPhysicalDeviceSurfaceSupportKHR = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (!vkGetPhysicalDeviceSurfaceSupportKHR) { log_message("missing vkGetPhysicalDeviceSurfaceSupportKHR\n"); ok = 0; }
    vkCreateDevice = (PFN_vkCreateDevice)vk_icdGetInstanceProcAddr(instance, "vkCreateDevice");
    if (!vkCreateDevice) { log_message("missing vkCreateDevice\n"); ok = 0; }
    vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vk_icdGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
    if (!vkGetDeviceProcAddr) { log_message("missing vkGetDeviceProcAddr\n"); ok = 0; }
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (!vkGetPhysicalDeviceSurfaceCapabilitiesKHR) { log_message("missing vkGetPhysicalDeviceSurfaceCapabilitiesKHR\n"); ok = 0; }
    vkGetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (!vkGetPhysicalDeviceSurfaceFormatsKHR) { log_message("missing vkGetPhysicalDeviceSurfaceFormatsKHR\n"); ok = 0; }
    vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vk_icdGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
    if (!vkDestroySurfaceKHR) { log_message("missing vkDestroySurfaceKHR\n"); ok = 0; }
    return ok;
}
static int load_device(VkDevice device) {
    int ok = 1;
    vkResetFences=(PFN_vkResetFences)vkGetDeviceProcAddr(device,"vkResetFences");if(!vkResetFences)ok=0;
    vkResetCommandPool=(PFN_vkResetCommandPool)vkGetDeviceProcAddr(device,"vkResetCommandPool");if(!vkResetCommandPool)ok=0;
    vkCreateBuffer=(PFN_vkCreateBuffer)vkGetDeviceProcAddr(device,"vkCreateBuffer");if(!vkCreateBuffer)ok=0;
    vkDestroyBuffer=(PFN_vkDestroyBuffer)vkGetDeviceProcAddr(device,"vkDestroyBuffer");if(!vkDestroyBuffer)ok=0;
    vkGetBufferMemoryRequirements=(PFN_vkGetBufferMemoryRequirements)vkGetDeviceProcAddr(device,"vkGetBufferMemoryRequirements");if(!vkGetBufferMemoryRequirements)ok=0;
    vkAllocateMemory=(PFN_vkAllocateMemory)vkGetDeviceProcAddr(device,"vkAllocateMemory");if(!vkAllocateMemory)ok=0;
    vkFreeMemory=(PFN_vkFreeMemory)vkGetDeviceProcAddr(device,"vkFreeMemory");if(!vkFreeMemory)ok=0;
    vkBindBufferMemory=(PFN_vkBindBufferMemory)vkGetDeviceProcAddr(device,"vkBindBufferMemory");if(!vkBindBufferMemory)ok=0;
    vkMapMemory=(PFN_vkMapMemory)vkGetDeviceProcAddr(device,"vkMapMemory");if(!vkMapMemory)ok=0;
    vkUnmapMemory=(PFN_vkUnmapMemory)vkGetDeviceProcAddr(device,"vkUnmapMemory");if(!vkUnmapMemory)ok=0;
    vkCmdCopyBufferToImage=(PFN_vkCmdCopyBufferToImage)vkGetDeviceProcAddr(device,"vkCmdCopyBufferToImage");if(!vkCmdCopyBufferToImage)ok=0;
    vkCreateRenderPass = (PFN_vkCreateRenderPass)vkGetDeviceProcAddr(device,"vkCreateRenderPass"); if(!vkCreateRenderPass)ok=0;
    vkDestroyRenderPass = (PFN_vkDestroyRenderPass)vkGetDeviceProcAddr(device,"vkDestroyRenderPass"); if(!vkDestroyRenderPass)ok=0;
    vkCreateImageView = (PFN_vkCreateImageView)vkGetDeviceProcAddr(device,"vkCreateImageView"); if(!vkCreateImageView)ok=0;
    vkDestroyImageView = (PFN_vkDestroyImageView)vkGetDeviceProcAddr(device,"vkDestroyImageView"); if(!vkDestroyImageView)ok=0;
    vkCreateFramebuffer = (PFN_vkCreateFramebuffer)vkGetDeviceProcAddr(device,"vkCreateFramebuffer"); if(!vkCreateFramebuffer)ok=0;
    vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)vkGetDeviceProcAddr(device,"vkDestroyFramebuffer"); if(!vkDestroyFramebuffer)ok=0;
    vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)vkGetDeviceProcAddr(device,"vkCmdBeginRenderPass"); if(!vkCmdBeginRenderPass)ok=0;
    vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)vkGetDeviceProcAddr(device,"vkCmdEndRenderPass"); if(!vkCmdEndRenderPass)ok=0;
    vkCmdClearAttachments = (PFN_vkCmdClearAttachments)vkGetDeviceProcAddr(device,"vkCmdClearAttachments"); if(!vkCmdClearAttachments)ok=0;
    vkDestroyDevice = (PFN_vkDestroyDevice)vkGetDeviceProcAddr(device, "vkDestroyDevice");
    if (!vkDestroyDevice) { log_message("missing vkDestroyDevice\n"); ok = 0; }
    vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetDeviceProcAddr(device, "vkGetDeviceQueue");
    if (!vkGetDeviceQueue) { log_message("missing vkGetDeviceQueue\n"); ok = 0; }
    vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
    if (!vkCreateSwapchainKHR) { log_message("missing vkCreateSwapchainKHR\n"); ok = 0; }
    vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR");
    if (!vkGetSwapchainImagesKHR) { log_message("missing vkGetSwapchainImagesKHR\n"); ok = 0; }
    vkCreateSemaphore = (PFN_vkCreateSemaphore)vkGetDeviceProcAddr(device, "vkCreateSemaphore");
    if (!vkCreateSemaphore) { log_message("missing vkCreateSemaphore\n"); ok = 0; }
    vkCreateFence = (PFN_vkCreateFence)vkGetDeviceProcAddr(device, "vkCreateFence");
    if (!vkCreateFence) { log_message("missing vkCreateFence\n"); ok = 0; }
    vkCreateCommandPool = (PFN_vkCreateCommandPool)vkGetDeviceProcAddr(device, "vkCreateCommandPool");
    if (!vkCreateCommandPool) { log_message("missing vkCreateCommandPool\n"); ok = 0; }
    vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)vkGetDeviceProcAddr(device, "vkAllocateCommandBuffers");
    if (!vkAllocateCommandBuffers) { log_message("missing vkAllocateCommandBuffers\n"); ok = 0; }
    vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR");
    if (!vkAcquireNextImageKHR) { log_message("missing vkAcquireNextImageKHR\n"); ok = 0; }
    vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vkGetDeviceProcAddr(device, "vkBeginCommandBuffer");
    if (!vkBeginCommandBuffer) { log_message("missing vkBeginCommandBuffer\n"); ok = 0; }
    vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier");
    if (!vkCmdPipelineBarrier) { log_message("missing vkCmdPipelineBarrier\n"); ok = 0; }
    vkCmdClearColorImage = (PFN_vkCmdClearColorImage)vkGetDeviceProcAddr(device, "vkCmdClearColorImage");
    if (!vkCmdClearColorImage) { log_message("missing vkCmdClearColorImage\n"); ok = 0; }
    vkEndCommandBuffer = (PFN_vkEndCommandBuffer)vkGetDeviceProcAddr(device, "vkEndCommandBuffer");
    if (!vkEndCommandBuffer) { log_message("missing vkEndCommandBuffer\n"); ok = 0; }
    vkQueueSubmit = (PFN_vkQueueSubmit)vkGetDeviceProcAddr(device, "vkQueueSubmit");
    if (!vkQueueSubmit) { log_message("missing vkQueueSubmit\n"); ok = 0; }
    vkQueuePresentKHR = (PFN_vkQueuePresentKHR)vkGetDeviceProcAddr(device, "vkQueuePresentKHR");
    if (!vkQueuePresentKHR) { log_message("missing vkQueuePresentKHR\n"); ok = 0; }
    vkWaitForFences = (PFN_vkWaitForFences)vkGetDeviceProcAddr(device, "vkWaitForFences");
    if (!vkWaitForFences) { log_message("missing vkWaitForFences\n"); ok = 0; }
    vkQueueWaitIdle = (PFN_vkQueueWaitIdle)vkGetDeviceProcAddr(device, "vkQueueWaitIdle");
    if (!vkQueueWaitIdle) { log_message("missing vkQueueWaitIdle\n"); ok = 0; }
    vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vkGetDeviceProcAddr(device, "vkDeviceWaitIdle");
    if (!vkDeviceWaitIdle) { log_message("missing vkDeviceWaitIdle\n"); ok = 0; }
    vkDestroyCommandPool = (PFN_vkDestroyCommandPool)vkGetDeviceProcAddr(device, "vkDestroyCommandPool");
    if (!vkDestroyCommandPool) { log_message("missing vkDestroyCommandPool\n"); ok = 0; }
    vkDestroyFence = (PFN_vkDestroyFence)vkGetDeviceProcAddr(device, "vkDestroyFence");
    if (!vkDestroyFence) { log_message("missing vkDestroyFence\n"); ok = 0; }
    vkDestroySemaphore = (PFN_vkDestroySemaphore)vkGetDeviceProcAddr(device, "vkDestroySemaphore");
    if (!vkDestroySemaphore) { log_message("missing vkDestroySemaphore\n"); ok = 0; }
    vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
    if (!vkDestroySwapchainKHR) { log_message("missing vkDestroySwapchainKHR\n"); ok = 0; }
    return ok;
}

__attribute__((noreturn)) void catchReturnFromMain(int);


typedef struct NativeSession {
 VkInstance instance; VkDevice device; VkSurfaceKHR surface; VkSwapchainKHR chain;
 VkQueue queue; uint32_t family,count,baseline; VkExtent2D extent; VkImage images[16];
 VkBuffer staging; VkDeviceMemory memory; void* mapped; size_t bytes;
 VkCommandPool frame_pool; VkCommandBuffer frame_command; VkFence frame_fence;
 VkPhysicalDeviceMemoryProperties gpu_memory; VkPhysicalDeviceProperties gpu_properties; VkQueueFlags queue_flags;
 int unsafe,ready,upload_borrowed; ExboxGpuBudget local_budget,*budget; ExboxGpuReservation staging_res;
} NativeSession;
static NativeSession session;
int exbox_presenter_probe(uint32_t*,int64_t*);
/* Never interpret failed idle or a missing destroy entry point as release. */
int exbox_native_close(void){
 NativeSession* s=&session;
 if(s->unsafe)return 0;
 if(s->budget && (s->budget->used!=s->staging_res.bytes || s->budget->live!=(s->staging_res.owner?1u:0u)))return 0;
 if(s->device && (!vkDeviceWaitIdle || vkDeviceWaitIdle(s->device)!=VK_SUCCESS))return 0;
 if((s->device&&!vkDestroyDevice)||(s->surface&&!vkDestroySurfaceKHR)||(s->instance&&!vkDestroyInstance))return 0;
 if((s->frame_pool&&!vkDestroyCommandPool)||(s->frame_fence&&!vkDestroyFence))return 0;
 if(s->frame_pool)vkDestroyCommandPool(s->device,s->frame_pool,NULL);
 if(s->frame_fence)vkDestroyFence(s->device,s->frame_fence,NULL);
 if(s->mapped)vkUnmapMemory(s->device,s->memory);
 if(s->staging)vkDestroyBuffer(s->device,s->staging,NULL);
 if(s->memory){vkFreeMemory(s->device,s->memory,NULL);exbox_gpu_budget_release(s->budget,&s->staging_res);}
 if(s->chain)vkDestroySwapchainKHR(s->device,s->chain,NULL);
 if(s->device)vkDestroyDevice(s->device,NULL);
 if(s->surface)vkDestroySurfaceKHR(s->instance,s->surface,NULL);
 if(s->instance)vkDestroyInstance(s->instance,NULL);
 memset(s,0,sizeof *s);return 1;
}
#define LIMIT 512u
#define TRY(call) do{VkResult rc=(call);if(rc!=VK_SUCCESS){log_message("%s failed: %d\n",#call,(int)rc);goto cleanup;}}while(0)
static int ensure_upload(void);
static unsigned requested_scale = 1;
int exbox_native_set_render_scale(unsigned scale){
 if(session.instance||session.device|| (scale!=1 && scale!=2))return 0;
 requested_scale=scale;return 1;
}
int exbox_native_open_with_budget(int* width,int* height,ExboxGpuBudget* budget,int cpu_upload){
 if(!width||!height||session.instance||session.device)return -1;
 if(budget&&(budget->used||budget->live||budget->limit>EXBOX_GPU_BUDGET_DEFAULT))return -1;
 session.local_budget=(ExboxGpuBudget)EXBOX_GPU_BUDGET_INIT;session.budget=budget?budget:&session.local_budget;
 VkInstance instance=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;VkSurfaceKHR surface=VK_NULL_HANDLE;
    VkExtensionProperties extensions[LIMIT];
    uint32_t count = LIMIT;
    if (!load_global()) goto cleanup;
    TRY(vkEnumerateInstanceExtensionProperties(NULL, &count, extensions));
    int has_surface = 0, has_display = 0, has_colorspace=0;
    for (uint32_t i = 0; i < count; ++i) {
        has_surface |= !strcmp(extensions[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME);
        has_display |= !strcmp(extensions[i].extensionName, VK_KHR_DISPLAY_EXTENSION_NAME);
        has_colorspace |= !strcmp(extensions[i].extensionName,VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
    }
    log_message("presenter instance extensions=%u required surface/display/colorspace=%d/%d/%d\n",count,has_surface,has_display,has_colorspace);
    if (!has_surface || !has_display || !has_colorspace) { log_message("VK_KHR_display/surface required\n"); goto cleanup; }
    const char *instance_ext[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME,VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Ex-Box dashboard SDR presenter", .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app, .enabledExtensionCount = 3, .ppEnabledExtensionNames = instance_ext};
    TRY(vkCreateInstance(&ici, NULL, &instance));session.instance=instance;
    if (!load_instance(instance)) goto cleanup;
    VkPhysicalDevice physicals[8]; count = 8;
    TRY(vkEnumeratePhysicalDevices(instance, &count, physicals));
    if (!count) goto cleanup;
    VkPhysicalDevice physical = physicals[0];
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(physical, &properties);
    vkGetPhysicalDeviceFeatures(physical, &features);
    log_message("device %s api=%u vendor=%x id=%x\n", properties.deviceName,
        properties.apiVersion, properties.vendorID, properties.deviceID);
    log_message("features robust=%u geometry=%u tessellation=%u anisotropy=%u int64=%u float64=%u\n",
        features.robustBufferAccess, features.geometryShader, features.tessellationShader,
        features.samplerAnisotropy, features.shaderInt64, features.shaderFloat64);
    count = LIMIT; TRY(vkEnumerateDeviceExtensionProperties(physical, NULL, &count, extensions));
    int has_swapchain = 0;
    for (uint32_t i = 0; i < count; ++i) {
        has_swapchain |= !strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    log_message("presenter device extensions=%u swapchain=%d\n",count,has_swapchain);
    if (!has_swapchain) goto cleanup;
    VkDisplayPropertiesKHR displays[8]; count = 8;
    TRY(vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &count, displays));
    if (!count) goto cleanup;
    VkDisplayModePropertiesKHR modes[32]; count = 32;
    TRY(vkGetDisplayModePropertiesKHR(physical, displays[0].display, &count, modes));
    if (!count) goto cleanup;
    ExboxDisplayMode candidates[32];
    if(count>32)goto cleanup;
    for(uint32_t m=0;m<count;m++)candidates[m]=(ExboxDisplayMode){modes[m].parameters.visibleRegion.width,modes[m].parameters.visibleRegion.height,modes[m].parameters.refreshRate};
    const size_t selected_mode=exbox_select_display_mode(candidates,count,requested_scale);
    if(selected_mode==SIZE_MAX){log_message("requested SDR display mode unavailable\n");goto cleanup;}
    log_message("SDR mode requested_scale=%u selected=%ux%u refresh_millihz=%u\n",requested_scale,candidates[selected_mode].width,candidates[selected_mode].height,candidates[selected_mode].refresh_millihz);
    VkDisplayPlanePropertiesKHR planes[8]; uint32_t plane_count = 8;
    TRY(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical, &plane_count, planes));
    uint32_t plane = UINT32_MAX;
    for (uint32_t i = 0; i < plane_count && plane == UINT32_MAX; ++i) {
        VkDisplayKHR supported[8]; count = 8;
        TRY(vkGetDisplayPlaneSupportedDisplaysKHR(physical, i, &count, supported));
        for (uint32_t j = 0; j < count; ++j) if (supported[j] == displays[0].display) plane = i;
    }
    if (plane == UINT32_MAX) goto cleanup;
    VkDisplayPlaneCapabilitiesKHR plane_caps;
    TRY(vkGetDisplayPlaneCapabilitiesKHR(physical, modes[selected_mode].displayMode, plane, &plane_caps));
    if (!(plane_caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)) goto cleanup;
    VkDisplaySurfaceCreateInfoKHR sci = {.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
        .displayMode = modes[selected_mode].displayMode, .planeIndex = plane,
        .planeStackIndex = planes[plane].currentStackIndex, .transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .globalAlpha = 1.0f, .alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
        .imageExtent = modes[selected_mode].parameters.visibleRegion};
    TRY(vkCreateDisplayPlaneSurfaceKHR(instance, &sci, NULL, &surface));session.surface=surface;
    VkQueueFamilyProperties queues[32]; count = 32;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 present = VK_FALSE;
        TRY(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present));
        if (present && queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    }
    if (family == UINT32_MAX) goto cleanup;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    const char *device_ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_ext};
    TRY(vkCreateDevice(physical, &dci, NULL, &device));session.device=device;
    if (!load_device(device)) goto cleanup;
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkSurfaceCapabilitiesKHR caps;
    TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) goto cleanup;
    VkSurfaceFormatKHR formats[32]; count = 32;
    TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats));
    if (!count) goto cleanup;
    if (formats[0].format == VK_FORMAT_UNDEFINED) formats[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    uint32_t wanted = caps.minImageCount + 1;
    if (caps.maxImageCount && wanted > caps.maxImageCount) wanted = caps.maxImageCount;
    VkExtent2D extent = caps.currentExtent.width == UINT32_MAX ? sci.imageExtent : caps.currentExtent;
    if(extent.width!=sci.imageExtent.width || extent.height!=sci.imageExtent.height){log_message("surface extent differs from selected mode\n");goto cleanup;}

 int found=0;for(uint32_t i=0;i<count;++i)if(formats[i].format==VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)found=1;
 if(!found)goto cleanup;
 session.queue=queue;session.family=family;session.extent=extent;session.gpu_properties=properties;session.queue_flags=queues[family].queueFlags;
 VkSwapchainCreateInfoKHR ci={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=surface,.minImageCount=wanted,.imageFormat=VK_FORMAT_B8G8R8A8_UNORM,.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,.imageExtent=extent,.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT,.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,.preTransform=caps.currentTransform,.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,.presentMode=VK_PRESENT_MODE_FIFO_KHR,.clipped=VK_TRUE};
 TRY(vkCreateSwapchainKHR(device,&ci,NULL,&session.chain));session.count=16;TRY(vkGetSwapchainImagesKHR(device,session.chain,&session.count,session.images));
 session.bytes=(size_t)extent.width*extent.height*4;
 vkGetPhysicalDeviceMemoryProperties(physical,&session.gpu_memory);session.ready=1;
 if(cpu_upload&&!ensure_upload())goto cleanup;
 *width=(int)extent.width;*height=(int)extent.height;return 1;
cleanup:
 return exbox_native_close()?0:-1;
}
int exbox_native_open(int*width,int*height){return exbox_native_open_with_budget(width,height,NULL,1);}
int exbox_native_open_gpu(int*width,int*height){return exbox_native_open_with_budget(width,height,NULL,0);}
static int ensure_upload(void){
 NativeSession*s=&session;if(!s->ready||s->unsafe||!s->device||!s->budget)return 0;if(s->mapped)return 1;
 VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void*mapped=NULL;int created=0,allocated=0;ExboxGpuReservation reservation=EXBOX_GPU_RESERVATION_INIT;
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=s->bytes,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
 if(vkCreateBuffer(s->device,&bi,NULL,&buffer)!=VK_SUCCESS)goto fail;created=1;
 VkMemoryRequirements req;vkGetBufferMemoryRequirements(s->device,buffer,&req);uint32_t type=UINT32_MAX;
 for(uint32_t i=0;i<s->gpu_memory.memoryTypeCount;i++)if((req.memoryTypeBits&(1u<<i))&&(s->gpu_memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
 if(type==UINT32_MAX||!exbox_gpu_budget_reserve(s->budget,&reservation,req.size))goto fail;
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
 if(vkAllocateMemory(s->device,&ai,NULL,&memory)!=VK_SUCCESS){exbox_gpu_budget_release(s->budget,&reservation);goto fail;}
 allocated=1;if(vkBindBufferMemory(s->device,buffer,memory,0)!=VK_SUCCESS||vkMapMemory(s->device,memory,0,s->bytes,0,&mapped)!=VK_SUCCESS)goto fail;
 s->staging=buffer;s->memory=memory;s->mapped=mapped;s->staging_res=reservation;
 log_message("native CPU upload admitted bytes=%zu requirement=%llu shared_used=%llu\n",s->bytes,(unsigned long long)req.size,(unsigned long long)s->budget->used);return 1;
 fail:
 /* No successful map reaches this failure path. Do not trust failed-call output pointers. */
 if(created)vkDestroyBuffer(s->device,buffer,NULL);
 if(allocated){vkFreeMemory(s->device,memory,NULL);exbox_gpu_budget_release(s->budget,&reservation);}
 return 0;
}
void* exbox_native_upload(size_t*bytes,int*pitch){if(!bytes||!pitch||!ensure_upload())return NULL;*bytes=session.bytes;*pitch=(int)session.extent.width*4;session.upload_borrowed=1;return session.mapped;}

static int present_source(VkBuffer output,ExboxNativeGpuRecord record,void* user){
 NativeSession* s=&session;if(!s->ready||!output||s->unsafe)return 0;
 VkCommandPool pool=s->frame_pool;VkCommandBuffer command=s->frame_command;VkSemaphore acquired=VK_NULL_HANDLE,rendered=VK_NULL_HANDLE;VkFence fence=s->frame_fence;int ok=0;
 const int reused=fence!=VK_NULL_HANDLE;
 VkSemaphoreCreateInfo si={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};TRY(vkCreateSemaphore(s->device,&si,NULL,&acquired));TRY(vkCreateSemaphore(s->device,&si,NULL,&rendered));
 if(!fence){VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};TRY(vkCreateFence(s->device,&fi,NULL,&fence));}
 uint32_t index;const VkResult acquire=vkAcquireNextImageKHR(s->device,s->chain,5000000000ULL,acquired,VK_NULL_HANDLE,&index);
 // SUBOPTIMAL still acquires an image and signals its semaphore. Retain those
 // resources if refusing the changed surface; it is not an unowned failure.
 if(acquire==VK_SUCCESS || acquire==VK_SUBOPTIMAL_KHR)s->unsafe=1;
 if(acquire!=VK_SUCCESS || index>=s->count)goto cleanup;
 if(!pool){
 VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=s->family};TRY(vkCreateCommandPool(s->device,&pc,NULL,&pool));
 VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};TRY(vkAllocateCommandBuffers(s->device,&ca,&command));
 }else{if(!vkResetCommandPool)goto cleanup;TRY(vkResetCommandPool(s->device,pool,0));}
 if(reused){if(!vkResetFences)goto cleanup;TRY(vkResetFences(s->device,1,&fence));}
 VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};TRY(vkBeginCommandBuffer(command,&begin));
 if(record)record(user,command);
 VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=s->images[index],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
 VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={s->extent.width,s->extent.height,1}};
 vkCmdCopyBufferToImage(command,output,s->images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
 barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=0;barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&barrier);TRY(vkEndCommandBuffer(command));
 uint32_t range=0;int64_t before=0,shown=0;int status=exbox_presenter_probe(&range,&before);if(!exbox_record_baseline(status,range,&s->baseline)||before<0)goto cleanup;
 VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&acquired,.pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&command,.signalSemaphoreCount=1,.pSignalSemaphores=&rendered};TRY(vkQueueSubmit(s->queue,1,&submit,fence));
 VkPresentInfoKHR pi={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=1,.pWaitSemaphores=&rendered,.swapchainCount=1,.pSwapchains=&s->chain,.pImageIndices=&index};TRY(vkQueuePresentKHR(s->queue,&pi));TRY(vkWaitForFences(s->device,1,&fence,VK_TRUE,5000000000ULL));
 /* Keep the same 2s requested-sleep budget without a 20ms polling quantum. */
 for(int i=0;i<2000;++i){status=exbox_presenter_probe(&range,&shown);if(exbox_display_complete(false,s->baseline,status,range,before,shown)){s->unsafe=0;ok=1;break;}usleep(1000);}
cleanup:
 /* On unknown completion, even acquired semaphore/resource ownership is kept.
  * Caller must request safe process exit and must not start SDL VideoOut. */
 if(s->unsafe){s->frame_pool=pool;s->frame_command=command;s->frame_fence=fence;return 0;}
 if(ok){s->frame_pool=pool;s->frame_command=command;s->frame_fence=fence;}
 else{if(pool)vkDestroyCommandPool(s->device,pool,NULL);if(fence)vkDestroyFence(s->device,fence,NULL);s->frame_pool=VK_NULL_HANDLE;s->frame_command=VK_NULL_HANDLE;s->frame_fence=VK_NULL_HANDLE;}
 if(acquired)vkDestroySemaphore(s->device,acquired,NULL);if(rendered)vkDestroySemaphore(s->device,rendered,NULL);return ok;
}

int exbox_native_gpu_context(ExboxNativeGpuContext* out){
 if(!out)return 0;
 memset(out,0,sizeof *out);
 NativeSession* s=&session;
 if(!s->ready||s->unsafe||!s->budget||!s->device||!vkGetDeviceProcAddr||!(s->queue_flags&VK_QUEUE_COMPUTE_BIT))return 0;
 out->device=s->device;out->memory=s->gpu_memory;out->properties=s->gpu_properties;
 out->queue_flags=s->queue_flags;out->get=vkGetDeviceProcAddr;
 out->width=s->extent.width;out->height=s->extent.height;out->budget=s->budget;return 1;
}
int exbox_native_present(void){if(!session.mapped)return 0;int ok=present_source(session.staging,NULL,NULL);if(ok)session.upload_borrowed=0;return ok;}
int exbox_native_present_gpu(VkBuffer output,size_t bytes,ExboxNativeGpuRecord record,void* user){
 if(!output||!record||bytes<session.bytes||!session.ready||session.unsafe||!(session.queue_flags&VK_QUEUE_COMPUTE_BIT))return 0;
 return present_source(output,record,user);
}

/* Serial presenter between completed frames. present_source clears unsafe
 * only after fence and display completion. Pending CPU pointers and unknown
 * completion retain both allocation and charge. No device-idle stall. */
int exbox_native_retire_upload(void){
 NativeSession*s=&session;
 if(!s->ready||!s->device||s->unsafe||s->upload_borrowed||!s->budget)return 0;
 if(!s->staging&&!s->memory&&!s->mapped)return !s->staging_res.owner&&!s->staging_res.bytes;
 if(!s->staging||!s->memory||!s->mapped||!vkUnmapMemory||!vkDestroyBuffer||!vkFreeMemory||s->staging_res.owner!=s->budget||!s->staging_res.bytes||!s->budget->live||s->staging_res.bytes>s->budget->used)return 0;
 const uint64_t bytes=s->staging_res.bytes;
 vkUnmapMemory(s->device,s->memory);vkDestroyBuffer(s->device,s->staging,NULL);vkFreeMemory(s->device,s->memory,NULL);
 exbox_gpu_budget_release(s->budget,&s->staging_res);
 s->mapped=NULL;s->staging=VK_NULL_HANDLE;s->memory=VK_NULL_HANDLE;
 log_message("native CPU upload retired requirement=%llu shared_used=%llu\n",(unsigned long long)bytes,(unsigned long long)s->budget->used);return 1;
}
