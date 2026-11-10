// SPDX-License-Identifier: MIT
#pragma once

#include "sha256.hpp"
#include "theme_catalog.hpp"
#include "../dashboard_services/theme_scene_capabilities.hpp"
#include "../../image_resample.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace exbox::themes {

struct PreparedTheme {
    std::string stable_id, display_name, error;
    std::vector<std::uint32_t> pixels;
    int width{}, height{};
    int requested_slot{}, source_slot{};
    explicit operator bool() const { return error.empty() && width > 0 && height > 0 && pixels.size() == std::size_t(width)*height; }
};

enum class RouteMode { Compatibility, Authored };
enum class RouteStatus { Exact, Compatibility, DefaultBackground, Unavailable };
struct RouteResult { RouteStatus status{RouteStatus::Unavailable}; int requested_slot{},source_slot{-1}; const Asset* asset{}; };
inline RouteResult route_wallpaper(const CatalogEntry& entry,int requested_slot,RouteMode mode,
                                   const exbox_services::theme_scene::Evidence& evidence={}) {
    RouteResult result;result.requested_slot=std::clamp(requested_slot,0,3);
    bool any=false;for(const auto&wallpaper:entry.wallpapers)any=any||bool(wallpaper);
    if(!any)return result;
    if(mode==RouteMode::Authored){
        const auto admission=exbox_services::theme_scene::admit(
            exbox_services::theme_scene::RequestMode::AuthoredExact,
            exbox_services::theme_scene::bit(exbox_services::theme_scene::Role::Background),
            evidence,any);
        if(admission.status!=exbox_services::theme_scene::Status::ExactScene){
            result.status=RouteStatus::DefaultBackground;return result;
        }
        const auto&exact=entry.wallpapers[std::size_t(result.requested_slot)];
        if(exact){result.status=RouteStatus::Exact;result.source_slot=result.requested_slot;result.asset=&*exact;}
        else result.status=RouteStatus::DefaultBackground;
        return result;
    }
    const auto admission=exbox_services::theme_scene::admit(
        exbox_services::theme_scene::RequestMode::Compatibility,
        exbox_services::theme_scene::bit(exbox_services::theme_scene::Role::Background),
        evidence,any);
    if(admission.status!=exbox_services::theme_scene::Status::CompatibilityWallpaper)return result;
    for(int i=0;i<4;++i)if(entry.wallpapers[std::size_t(i)]){
        result.status=RouteStatus::Compatibility;result.source_slot=i;result.asset=&*entry.wallpapers[std::size_t(i)];return result;
    }
    return result;
}

inline PreparedTheme prepare_compatibility_background(const CatalogEntry& entry,
                                                       const std::string& root,
                                                       int target_width,
                                                       int target_height,
                                                       int requested_slot=0) {
    PreparedTheme result; result.stable_id=entry.stable_id; result.display_name=entry.display_name;
    result.requested_slot=std::clamp(requested_slot,0,3);
    const RouteResult route=route_wallpaper(entry,result.requested_slot,RouteMode::Compatibility);
    result.source_slot=route.source_slot;const Asset* asset=route.asset;
    if(!asset||!safe_relative_path(asset->relative_path)){result.error="No safe wallpaper in this theme";return result;}
    if(target_width<1||target_height<1||target_width>3840||target_height>2160){result.error="Unsupported output size";return result;}
    const std::string path=root+(root.empty()||root.back()=='/'?"":"/")+asset->relative_path;
    std::ifstream file(path,std::ios::binary);
    if(!file){result.error="Theme wallpaper is missing";return result;}
    std::vector<std::uint8_t> encoded((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    if(encoded.size()!=asset->bytes){result.error="Theme wallpaper size changed";return result;}
    Sha256 digest;digest.update(encoded.data(),encoded.size());
    if(digest.finish()!=asset->content_sha256){result.error="Theme wallpaper hash changed";return result;}
    if(sniff(encoded)!=asset->encoding){result.error="Theme wallpaper encoding changed";return result;}
    SDL_RWops* stream=SDL_RWFromConstMem(encoded.data(),int(encoded.size()));
    SDL_Surface* decoded=stream?IMG_Load_RW(stream,1):nullptr;
    SDL_Surface* source=decoded?SDL_ConvertSurfaceFormat(decoded,SDL_PIXELFORMAT_ARGB8888,0):nullptr;
    if(decoded)SDL_FreeSurface(decoded);
    if(!source){result.error="Theme wallpaper decode failed";return result;}
    const int sw=source->w,sh=source->h;
    int cw=sw,ch=sh,cx=0,cy=0;
    if(std::int64_t(sw)*target_height>std::int64_t(target_width)*sh){cw=int(std::int64_t(sh)*target_width/target_height);cx=(sw-cw)/2;}
    else if(std::int64_t(sw)*target_height<std::int64_t(target_width)*sh){ch=int(std::int64_t(sw)*target_height/target_width);cy=(sh-ch)/2;}
    if(cw<1||ch<1){SDL_FreeSurface(source);result.error="Theme wallpaper has invalid dimensions";return result;}
    std::vector<std::uint32_t> crop(std::size_t(cw)*ch);
    for(int y=0;y<ch;++y){const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(source->pixels)+(cy+y)*source->pitch)+cx;std::copy(row,row+cw,crop.begin()+std::size_t(y)*cw);}
    SDL_FreeSurface(source);
    result.pixels.resize(std::size_t(target_width)*target_height);
    image_quality::resize_argb(crop.data(),cw,ch,cw*4,result.pixels.data(),target_width,target_height,target_width*4);
    result.width=target_width;result.height=target_height;return result;
}

inline PreparedTheme prepare_preview(const CatalogEntry& entry,const std::string& root,int width,int height) {
    CatalogEntry preview=entry;for(auto&wallpaper:preview.wallpapers)wallpaper.reset();
    if(entry.thumbnail)preview.wallpapers[0]=entry.thumbnail;
    else for(const auto&wallpaper:entry.wallpapers)if(wallpaper){preview.wallpapers[0]=wallpaper;break;}
    return prepare_compatibility_background(preview,root,width,height,0);
}

} // namespace exbox::themes
