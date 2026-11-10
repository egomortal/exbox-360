// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace exbox_services::theme_scene {

enum class Role : std::uint32_t {
    Background=1u<<0, Floor=1u<<1, Foreground=1u<<2, Midground=1u<<3,
    Banner=1u<<4, ChannelBanner=1u<<5, SphereColor=1u<<6,
    AvatarDirectionalLight=1u<<7, AvatarAmbientLight=1u<<8
};
constexpr std::uint32_t bit(Role role){return static_cast<std::uint32_t>(role);}
inline constexpr std::uint32_t all_roles=(1u<<9)-1;

enum class RequestMode { Compatibility, AuthoredExact };
enum class Status { CompatibilityWallpaper, ExactScene, DefaultBackground, Unavailable };

struct Evidence {
    std::uint32_t present_roles=0;
    bool owner_assets_verified=false;
    bool route_table_proven=false;
    bool z_order_proven=false;
    bool crop_sampling_proven=false;
    bool lighting_format_proven=false;
};

struct Decision {
    Status status=Status::Unavailable;
    std::uint32_t requested_roles=0;
    std::uint32_t applied_roles=0;
    const char* reason="No verified theme scene";
    explicit operator bool() const{return status==Status::ExactScene||status==Status::CompatibilityWallpaper;}
};

inline Decision admit(RequestMode mode,std::uint32_t requested_roles,const Evidence&evidence,bool compatibility_wallpaper_available){
    Decision out;out.requested_roles=requested_roles;
    if(mode==RequestMode::Compatibility){
        if(compatibility_wallpaper_available){out.status=Status::CompatibilityWallpaper;out.reason="Explicit single-wallpaper compatibility";}
        else {out.status=Status::Unavailable;out.reason="No verified compatibility wallpaper";}return out;
    }
    if(!requested_roles){out.status=Status::DefaultBackground;out.reason="No authored scene roles requested";return out;}
    if(requested_roles&~all_roles){out.status=Status::DefaultBackground;out.reason="Unknown authored scene role";return out;}
    if(!evidence.owner_assets_verified){out.status=Status::DefaultBackground;out.reason="Theme scene assets are not verified";return out;}
    if((evidence.present_roles&requested_roles)!=requested_roles){out.status=Status::DefaultBackground;out.reason="Exact authored role set is incomplete";return out;}
    if(!evidence.route_table_proven||!evidence.z_order_proven||!evidence.crop_sampling_proven){out.status=Status::DefaultBackground;out.reason="Authored route or composition is unproven";return out;}
    const std::uint32_t lighting=bit(Role::AvatarDirectionalLight)|bit(Role::AvatarAmbientLight);
    if((requested_roles&lighting)&&!evidence.lighting_format_proven){out.status=Status::DefaultBackground;out.reason="Avatar lighting format is unproven";return out;}
    out.status=Status::ExactScene;out.applied_roles=requested_roles;out.reason="Exact evidence-backed authored scene";return out;
}

} // namespace exbox_services::theme_scene
