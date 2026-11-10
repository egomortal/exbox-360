// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "actor_bundle.hpp"
#include "../../avatar_kit.hpp"
#include "../themes/sha256.hpp"
#include <stdexcept>

namespace editor_dashboard::appearance {
// Only original editor-resolved parts/materials, bound to the exact save.
// The original prop stays stowed for this admitted standing-idle path.
class PreparedActor {
    std::shared_ptr<const ActorBundle> bundle_;
    avkit::Kit kit_;
    std::unique_ptr<avkit::Renderer> renderer_;
    std::vector<std::uint32_t> face_frames_,textures_;
    std::vector<std::uint8_t> wave_bytes_;
    std::optional<DecodedClip> wave_;
    std::vector<std::uint32_t> wave_faces_;
    std::uint32_t variants_=0,draws_=0;
    struct Rig {int parent;std::array<float,3> offset,scale,parent_scale;};
    std::vector<Rig> rig_;
    static std::uint32_t be(ByteView b,std::size_t p){return std::uint32_t(b[p])<<24|std::uint32_t(b[p+1])<<16|std::uint32_t(b[p+2])<<8|b[p+3];}
    static float real(ByteView b,std::size_t p){auto n=be(b,p);float f;std::memcpy(&f,&n,4);return f;}
    static avkit::M34 trs(const std::array<float,3>&p,std::array<float,4>q,const std::array<float,3>&s){
        float n=0;for(float v:q)n+=v*v;n=std::sqrt(n);for(float&v:q)v/=n;
        const float x=q[0],y=q[1],z=q[2],w=q[3];avkit::M34 m;
        const float r[9]={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
        for(int i=0;i<3;++i){for(int j=0;j<3;++j)m.m[4*i+j]=r[3*i+j]*s[j];m.m[4*i+3]=p[i];}return m;
    }
    static avkit::M34 inverse_rigid(const avkit::M34&m){avkit::M34 r;for(int i=0;i<3;++i){for(int j=0;j<3;++j)r.m[4*i+j]=m.m[4*j+i];r.m[4*i+3]=0;for(int j=0;j<3;++j)r.m[4*i+3]-=r.m[4*i+j]*m.m[4*j+3];}return r;}
public:
    static bool read(const std::filesystem::path&p,std::size_t cap,std::vector<std::uint8_t>&v){
        std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)return false;auto n=f.tellg();if(n<0||std::uint64_t(n)>cap)return false;v.resize(std::size_t(n));f.seekg(0);if(n)f.read(reinterpret_cast<char*>(v.data()),n);return bool(f);
    }
    static std::shared_ptr<PreparedActor> load(const std::filesystem::path&folder,ByteView committed,std::string&error){
        auto fail=[&](const char*e)->std::shared_ptr<PreparedActor>{error=e;return {};};
        std::vector<std::uint8_t> meta,model,clip,kit;
        if(!read(folder/"face.bin",1u<<20,meta)||meta.size()<1212||std::memcmp(meta.data(),"EXFACE2\0",8))return fail("prepared material descriptor/basis unavailable");
        if(committed.size()!=1000||std::memcmp(meta.data()+8,committed.data(),1000))return fail("prepared material commit mismatch");
        auto hash=[](const std::vector<std::uint8_t>&b){return exbox::themes::sha256(std::string_view(reinterpret_cast<const char*>(b.data()),b.size()));};
        if(!read(folder/"resolved.exav",16u<<20,model)||!read(folder/"idle.exclip",16u<<20,clip)||!read(folder/"appearance.nxak",16u<<20,kit))return fail("prepared material resource unavailable");
        if(hash(model)!=std::string(reinterpret_cast<const char*>(meta.data()+1008),64)||hash(clip)!=std::string(reinterpret_cast<const char*>(meta.data()+1072),64)||hash(kit)!=std::string(reinterpret_cast<const char*>(meta.data()+1136),64))return fail("prepared material resource digest mismatch");
        auto a=std::shared_ptr<PreparedActor>(new PreparedActor);
        a->bundle_=ActorBundle::prepare(std::move(model),std::move(clip),committed,32u<<20,error);if(!a->bundle_)return {};
        auto le=[&](std::size_t p){return std::uint32_t(meta[p])|std::uint32_t(meta[p+1])<<8|std::uint32_t(meta[p+2])<<16|std::uint32_t(meta[p+3])<<24;};
        auto frames=le(1200);a->draws_=le(1204);a->variants_=le(1208);
        if(frames!=a->bundle_->clip().frames()||!a->draws_||a->draws_>64||!a->variants_||a->variants_>32||meta.size()!=1212+4ull*(frames+std::uint64_t(a->draws_)*a->variants_))return fail("prepared face table extent");
        if(!a->kit_.load(kit.data(),kit.size())||a->kit_.joints.size()!=a->bundle_->scene().joints)return fail("prepared material kit/rig rejected");
        a->renderer_=std::make_unique<avkit::Renderer>(a->kit_);if(!a->renderer_->set_prepared_appearance())return fail("prepared draw list rejected");
        for(std::uint32_t i=0;i<frames;++i){auto v=le(1212+4*i);if(v>=a->variants_)return fail("face variant out of range");a->face_frames_.push_back(v);}
        for(std::uint32_t i=0;i<a->draws_*a->variants_;++i){auto v=le(1212+4*(frames+i));if(v>=a->kit_.textures.size())return fail("face texture out of range");a->textures_.push_back(v);}
        std::vector<std::uint8_t> wm;
        if(std::filesystem::exists(folder/"wave.face.bin")){
            if(!read(folder/"wave.face.bin",1u<<20,wm)||wm.size()<1212||std::memcmp(wm.data(),"EXFACE2\0",8)||std::memcmp(wm.data()+8,committed.data(),1000)||std::memcmp(wm.data()+1008,meta.data()+1008,64)||std::memcmp(wm.data()+1136,meta.data()+1136,64))return fail("wave material identity mismatch");
            if(!read(folder/"wave.exclip",16u<<20,a->wave_bytes_)||hash(a->wave_bytes_)!=std::string(reinterpret_cast<const char*>(wm.data()+1072),64))return fail("wave clip digest mismatch");
            a->wave_=DecodedClip::parse(a->wave_bytes_,16u<<20);if(!a->wave_||a->wave_->joints()!=a->bundle_->scene().joints||a->wave_->joints(1))return fail("wave rig mismatch");
            auto wl=[&](std::size_t p){return std::uint32_t(wm[p])|std::uint32_t(wm[p+1])<<8|std::uint32_t(wm[p+2])<<16|std::uint32_t(wm[p+3])<<24;};auto wf=wl(1200);
            if(wf!=a->wave_->frames()||wl(1204)!=a->draws_||wl(1208)!=a->variants_||wm.size()!=1212+4ull*(wf+std::uint64_t(a->draws_)*a->variants_))return fail("wave face extent");
            for(std::uint32_t i=0;i<wf;++i){auto v=wl(1212+4*i);if(v>=a->variants_)return fail("wave face variant");a->wave_faces_.push_back(v);}
            for(std::size_t i=0;i<a->textures_.size();++i)if(wl(1212+4*(wf+i))!=a->textures_[i])return fail("wave material table mismatch");
        }
        const auto cpu=a->bundle_->scene().cpu;auto skeleton=be(cpu,0),jp=be(cpu,skeleton+4);std::vector<avkit::M34> bind,rest;
        for(std::uint32_t i=0;i<a->bundle_->scene().joints;++i){auto p=jp+96*i;auto parent=be(cpu,p);std::array<float,3>pos{},scale{},pws{1,1,1};std::array<float,4>rot{};
            for(int k=0;k<3;++k){pos[k]=real(cpu,p+16+4*k);scale[k]=real(cpu,p+80+4*k);}for(int k=0;k<4;++k)rot[k]=real(cpu,p+32+4*k);
            bind.push_back(trs(pos,rot,{1,1,1}));auto local=parent==255?bind.back():avkit::mul(inverse_rigid(bind[parent]),bind.back());
            auto scaled=local;for(int r=0;r<3;++r)for(int c=0;c<3;++c)scaled.m[4*r+c]*=scale[c];rest.push_back(parent==255?scaled:avkit::mul(rest[parent],scaled));
            if(parent!=255)for(int c=0;c<3;++c){float v=0;for(int r=0;r<3;++r)v+=rest[parent].m[4*r+c]*rest[parent].m[4*r+c];pws[c]=std::sqrt(v);}
            a->rig_.push_back({parent==255?-1:int(parent),{local.m[3],local.m[7],local.m[11]},scale,pws});
        }
        error.clear();return a;
    }
    avkit::Renderer& renderer(){return *renderer_;}
    const Manifest& manifest()const{return bundle_->scene().manifest;}
    bool has_emote()const{return bool(wave_);}
    const DecodedClip& clip(bool emote=false)const{return emote&&wave_?*wave_:bundle_->clip();}
    avkit::Camera camera()const{auto c=avkit::Camera::full();c.yaw=0;c.span=renderer_->body()->top_y*1.08f;c.center_y=c.span/2;return c;}
    std::vector<avkit::M34> pose(double seconds,bool emote=false){
        if(emote&&!wave_)throw std::runtime_error("original wave unavailable");
        auto sample=clip(emote).sample(seconds,!emote);if(!sample)throw std::runtime_error("invalid prepared animation time");
        const auto&faces=emote?wave_faces_:face_frames_;const auto frame=std::min<std::size_t>(faces.size()-1,sample->frame);
        std::vector<int> textures;for(std::uint32_t d=0;d<draws_;++d)textures.push_back(int(textures_[d*variants_+faces[frame]]));
        if(!renderer_->set_prepared_textures(textures))throw std::runtime_error("prepared face binding refused");
        std::vector<avkit::M34> world;world.reserve(rig_.size());
        for(std::size_t i=0;i<rig_.size();++i){const auto&r=rig_[i];auto p=sample->poses[0][i];for(int k=0;k<3;++k){p.position[k]=(r.offset[k]+p.position[k])*r.parent_scale[k];p.scale[k]*=r.scale[k];}auto m=trs(p.position,p.rotation,p.scale);world.push_back(r.parent<0?m:avkit::mul(world[std::size_t(r.parent)],m));}
        // Kit positions/inverse binds use the existing 1.2-metre conversion.
        for(auto&m:world){m.m[2]*=-1;m.m[6]*=-1;m.m[8]*=-1;m.m[9]*=-1;m.m[11]*=-1;for(int k=0;k<3;++k)m.m[4*k+3]*=1.2f;}return world;
    }
};
}
