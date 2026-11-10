// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "resolved_scene.hpp"
#include "decoded_clip.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace editor_dashboard::appearance {
class ActorBundle {
    std::vector<std::uint8_t> model_bytes_,clip_bytes_;
    ResolvedScene scene_;
    DecodedClip clip_;
    ActorBundle()=default;
public:
    ActorBundle(const ActorBundle&)=delete;
    ActorBundle& operator=(const ActorBundle&)=delete;
    const ResolvedScene& scene()const{return scene_;}
    const DecodedClip& clip()const{return clip_;}
    static std::shared_ptr<const ActorBundle> prepare(std::vector<std::uint8_t> model,
            std::vector<std::uint8_t> clip,ByteView manifest,std::size_t limit,std::string& error){
        if(model.size()>limit||clip.size()>limit-model.size()){error="actor resource admission limit";return {};}
        auto owner=std::shared_ptr<ActorBundle>(new ActorBundle);
        owner->model_bytes_=std::move(model);owner->clip_bytes_=std::move(clip);
        auto scene=resolved_scene(owner->model_bytes_,manifest,limit,error);if(!scene)return {};
        auto motion=DecodedClip::parse(owner->clip_bytes_,limit);
        if(!motion||motion->joints()!=scene->joints){error="actor clip/rig mismatch";return {};}
        owner->scene_=*scene;owner->clip_=*motion;error.clear();return owner;
    }
    static std::shared_ptr<const ActorBundle> load(const std::filesystem::path& folder,
            std::size_t limit,std::string& error){
        const auto read=[&](const std::filesystem::path& path,std::size_t cap,std::vector<std::uint8_t>& out){
            std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return false;
            const auto size=file.tellg();if(size<0||std::uint64_t(size)>cap)return false;
            out.resize(std::size_t(size));file.seekg(0);
            if(!out.empty())file.read(reinterpret_cast<char*>(out.data()),out.size());
            return bool(file)&&file.peek()==std::char_traits<char>::eof();
        };
        std::vector<std::uint8_t> before,after,model,clip;
        if(!read(folder/"avatar_manifest.bin",1000,before)||before.size()!=1000){error="actor manifest unavailable";return {};}
        if(!read(folder/"resolved.exav",limit,model)||!read(folder/"idle.exclip",limit-model.size(),clip)){error="actor prepared resources unavailable";return {};}
        auto owner=prepare(std::move(model),std::move(clip),before,limit,error);if(!owner)return {};
        if(!read(folder/"avatar_manifest.bin",1000,after)||before!=after){error="actor changed during preparation";return {};}
        return owner;
    }
};
} // namespace editor_dashboard::appearance
