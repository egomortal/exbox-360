// SPDX-License-Identifier: GPL-3.0-or-later
// Borrowed views into immutable, editor-resolved resources. The owner of the
// input bytes must outlive these views and any queued GPU preparation.
#pragma once
#include "appearance.hpp"
#include <string>

namespace editor_dashboard::appearance {
struct ResolvedScene {
    Manifest manifest;
    ByteView cpu,gpu;
    std::uint32_t skeleton_version=0,coordinates=0;
    std::uint32_t joints=0,components=0;
    std::uint64_t triangles=0;
    bool has_prop=false;
};
inline std::optional<ResolvedScene> resolved_scene(ByteView bytes,
        ByteView expected_manifest,std::size_t resource_limit,std::string& error) {
    const auto fail=[&](const char* why)->std::optional<ResolvedScene>{error=why;return {};};
    const auto range=[](ByteView b,std::uint64_t o,std::uint64_t n){return o<=b.size()&&n<=b.size()-o;};
    const auto le=[](ByteView b,std::size_t o){return std::uint32_t(b[o])|std::uint32_t(b[o+1])<<8|std::uint32_t(b[o+2])<<16|std::uint32_t(b[o+3])<<24;};
    const auto be=[](ByteView b,std::size_t o){return std::uint32_t(b[o])<<24|std::uint32_t(b[o+1])<<16|std::uint32_t(b[o+2])<<8|b[o+3];};
    if(!bytes.data()||bytes.size()<1032||std::memcmp(bytes.data(),"EXAVR1\0\0",8))return fail("resolved appearance header");
    if(le(bytes,8)!=1||le(bytes,20)!=1000)return fail("resolved appearance version");
    const std::uint64_t nc=le(bytes,12),ng=le(bytes,16);
    if(nc+ng>resource_limit||1032+nc+ng!=bytes.size())return fail("resolved appearance resource extent");
    auto parsed=parse(bytes.subspan(32,1000));
    if(!parsed||!coherent(*parsed,expected_manifest))return fail("resolved appearance commit mismatch");
    ResolvedScene scene;scene.manifest=*parsed;scene.cpu=bytes.subspan(1032,nc);scene.gpu=bytes.subspan(1032+nc,ng);
    scene.skeleton_version=le(bytes,24);scene.coordinates=le(bytes,28);
    if(scene.skeleton_version!=2||scene.coordinates!=0)return fail("resolved appearance rig convention");
    if(!range(scene.cpu,0,20))return fail("resolved appearance root");
    const auto skeleton=[&](std::uint32_t offset)->std::uint32_t{
        if(!range(scene.cpu,offset,8))return 0;
        auto count=be(scene.cpu,offset),j=be(scene.cpu,offset+4);
        if(!count||count>72||!range(scene.cpu,j,std::uint64_t(count)*96))return 0;
        for(std::uint32_t i=0;i<count;++i){
            const auto p=std::uint64_t(j)+i*96;auto parent=be(scene.cpu,p);
            if(parent!=255&&parent>=i)return 0;
            for(std::size_t k=16;k<96;k+=4){auto bits=be(scene.cpu,p+k);float f;std::memcpy(&f,&bits,4);if(!std::isfinite(f))return 0;}
        }
        return count;
    };
    scene.joints=skeleton(be(scene.cpu,0));if(!scene.joints)return fail("resolved appearance skeleton");
    const auto model=[&](std::uint32_t offset,std::uint32_t joint_count)->bool {
        if(!range(scene.cpu,offset,52))return false;
        const auto nb=be(scene.cpu,offset+20),nt=be(scene.cpu,offset+24),batch=be(scene.cpu,offset+44),textures=be(scene.cpu,offset+48);
        if(!range(scene.cpu,batch,std::uint64_t(nb)*512)||!range(scene.cpu,textures,std::uint64_t(nt)*44))return false;
        for(std::uint32_t i=0;i<nb;++i){
            const std::uint64_t b=std::uint64_t(batch)+512*i;
            const auto triangles=be(scene.cpu,b+484),vertices=be(scene.cpu,b+488),uv=be(scene.cpu,b+492),stride=be(scene.cpu,b+496),isize=be(scene.cpu,b+500),vp=be(scene.cpu,b+504),ip=be(scene.cpu,b+508);
            if(uv>6||stride<28+uv*4||isize!=2||!range(scene.gpu,vp,std::uint64_t(vertices)*stride)||!range(scene.gpu,ip,std::uint64_t(triangles)*3*isize))return false;
            for(std::uint32_t v=0;v<vertices;++v){const auto p=std::uint64_t(vp)+std::uint64_t(v)*stride;
                for(std::size_t k=0;k<12;k+=4){auto bits=be(scene.gpu,p+k);float f;std::memcpy(&f,&bits,4);if(!std::isfinite(f))return false;}
                const auto weights=be(scene.gpu,p+16),indices=be(scene.gpu,p+20);if(!weights)return false;
                for(unsigned k=0;k<4;++k)if(((weights>>(8*k))&255)&&((indices>>(8*k))&255)>=joint_count)return false;
                for(std::size_t k=0;k<uv*2;++k){const auto q=p+28+2*k;auto half=std::uint16_t(scene.gpu[q])<<8|scene.gpu[q+1];if((half&0x7c00)==0x7c00)return false;}
            }
            for(std::uint64_t t=0;t<std::uint64_t(triangles)*3;++t){const auto p=std::uint64_t(ip)+2*t;auto index=std::uint32_t(scene.gpu[p])<<8|scene.gpu[p+1];if(index>=vertices)return false;}
            for(std::size_t p=0;p<20;++p){const auto o=b+4+p*24;auto type=be(scene.cpu,o);
                if(type>3)return false;
                // The original prop materials use 0xffff for generated
                // coordinates (e.g. usage 4), not a vertex UV-array index.
                if(type==1){const auto index=std::uint32_t(scene.cpu[o+8])<<8|scene.cpu[o+9],layer=std::uint32_t(scene.cpu[o+10])<<8|scene.cpu[o+11];if(index>=nt||(layer!=0xffff&&layer>=uv))return false;}
                if(type==2||type==3)for(std::size_t k=8;k<24;k+=4){auto bits=be(scene.cpu,o+k);float f;std::memcpy(&f,&bits,4);if(!std::isfinite(f))return false;}
            }
            scene.triangles+=triangles;
        }
        for(std::uint32_t i=0;i<nt;++i){const std::uint64_t t=std::uint64_t(textures)+44*i;
            if(!range(scene.gpu,be(scene.cpu,t+36),be(scene.cpu,t+12))||!range(scene.gpu,be(scene.cpu,t+40),be(scene.cpu,t+16)))return false;
        }
        return true;
    };
    scene.components=be(scene.cpu,8);const auto infos=be(scene.cpu,12),models=be(scene.cpu,16);
    if(scene.components>15||!range(scene.cpu,infos,std::uint64_t(scene.components)*32)||!range(scene.cpu,models,std::uint64_t(scene.components)*52))return fail("resolved appearance components");
    for(std::uint32_t i=0;i<scene.components;++i)if(!model(models+52*i,scene.joints))return fail("resolved appearance model/material");
    const auto prop=be(scene.cpu,4);scene.has_prop=prop!=0;
    if(prop&&(!range(scene.cpu,prop,92)||!skeleton(be(scene.cpu,prop))||!model(prop+36,skeleton(be(scene.cpu,prop)))))return fail("resolved appearance prop");
    error.clear();return scene;
}
} // namespace editor_dashboard::appearance
