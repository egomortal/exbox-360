#pragma once
#include <algorithm>
#include <atomic>
#include "av2_boot_log.h"
#include <cstdint>
#include <vector>
// Native display route already exercised by Q4's clear title. This helper
// selects its first advertised display/mode and a compatible plane, using
// the renderer's EXISTING instance/device rather than another GPU context.
namespace plume {
// One live native display surface per editor process. Resize reuses its
// surface; failed startup releases this ownership token automatically.
inline std::atomic<bool> av2DisplayOwner{false};
struct Av2DisplayClaim {
    bool keep=false;
    ~Av2DisplayClaim(){if(!keep)av2DisplayOwner.store(false);}
};
inline void av2ReleaseDisplayOwner(){av2DisplayOwner.store(false);av2_boot_log("display","surface ownership released");}
inline VkResult av2CreateDisplaySurface(VkInstance instance,VkPhysicalDevice physical,
                                      VkSurfaceKHR* surface,VkExtent2D* extent) {
    bool expected=false;
    if(!av2DisplayOwner.compare_exchange_strong(expected,true)) {
        av2_boot_log("display","duplicate native surface refused");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    Av2DisplayClaim ownership;
    av2_boot_log("display","surface creation begin; plume owns sole display path");
    if(!vkGetPhysicalDeviceDisplayPropertiesKHR || !vkGetDisplayModePropertiesKHR ||
       !vkGetPhysicalDeviceDisplayPlanePropertiesKHR || !vkGetDisplayPlaneSupportedDisplaysKHR ||
       !vkGetDisplayPlaneCapabilitiesKHR || !vkCreateDisplayPlaneSurfaceKHR)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    uint32_t count=0;
    VkResult result=vkGetPhysicalDeviceDisplayPropertiesKHR(physical,&count,nullptr);
    if(result!=VK_SUCCESS) return result;
    if(!count || count>128) return VK_ERROR_INITIALIZATION_FAILED;
    std::vector<VkDisplayPropertiesKHR> displays(count);
    result=vkGetPhysicalDeviceDisplayPropertiesKHR(physical,&count,displays.data());
    if(result!=VK_SUCCESS || !count) return result==VK_SUCCESS?VK_ERROR_INITIALIZATION_FAILED:result;
    if(!(displays[0].supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    count=0;
    result=vkGetDisplayModePropertiesKHR(physical,displays[0].display,&count,nullptr);
    if(result!=VK_SUCCESS) return result;
    if(!count || count>1024) return VK_ERROR_INITIALIZATION_FAILED;
    std::vector<VkDisplayModePropertiesKHR> modes(count);
    result=vkGetDisplayModePropertiesKHR(physical,displays[0].display,&count,modes.data());
    if(result!=VK_SUCCESS || !count) return result==VK_SUCCESS?VK_ERROR_INITIALIZATION_FAILED:result;
    count=0;
    result=vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical,&count,nullptr);
    if(result!=VK_SUCCESS) return result;
    if(!count || count>128) return VK_ERROR_INITIALIZATION_FAILED;
    std::vector<VkDisplayPlanePropertiesKHR> planes(count);
    result=vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical,&count,planes.data());
    if(result!=VK_SUCCESS) return result;
    for(uint32_t plane=0;plane<count;++plane) {
        uint32_t supported_count=0;
        result=vkGetDisplayPlaneSupportedDisplaysKHR(physical,plane,&supported_count,nullptr);
        if(result!=VK_SUCCESS) return result;
        if(!supported_count) continue;
        if(supported_count>128) return VK_ERROR_INITIALIZATION_FAILED;
        std::vector<VkDisplayKHR> supported(supported_count);
        result=vkGetDisplayPlaneSupportedDisplaysKHR(physical,plane,&supported_count,supported.data());
        if(result!=VK_SUCCESS) return result;
        if(std::find(supported.begin(),supported.begin()+supported_count,displays[0].display)==supported.begin()+supported_count) continue;
        VkDisplayPlaneCapabilitiesKHR caps{};
        result=vkGetDisplayPlaneCapabilitiesKHR(physical,modes[0].displayMode,plane,&caps);
        if(result!=VK_SUCCESS) return result;
        if(!(caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)) continue;
        VkDisplaySurfaceCreateInfoKHR info{};
        info.sType=VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
        info.displayMode=modes[0].displayMode;
        info.planeIndex=plane;info.planeStackIndex=planes[plane].currentStackIndex;
        info.transform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        info.globalAlpha=1.0f;info.alphaMode=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
        info.imageExtent=modes[0].parameters.visibleRegion;
        if(!info.imageExtent.width || !info.imageExtent.height) return VK_ERROR_INITIALIZATION_FAILED;
        result=vkCreateDisplayPlaneSurfaceKHR(instance,&info,nullptr,surface);
        av2_boot_log("display","surface result=%d extent=%ux%u plane=%u",int(result),info.imageExtent.width,info.imageExtent.height,plane);
        if(result==VK_SUCCESS){*extent=info.imageExtent;ownership.keep=true;}
        return result;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}
}
