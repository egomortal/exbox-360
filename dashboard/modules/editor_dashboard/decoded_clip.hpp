// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include "bytes.hpp"
#include <vector>

namespace editor_dashboard::appearance {
struct JointPose {
    std::array<float,3> position{},scale{};
    std::array<float,4> rotation{};
};
struct ClipSample {
    std::uint32_t frame=0;
    std::array<std::vector<JointPose>,2> poses;
    std::vector<JointPose> motions;
    std::vector<std::uint32_t> texture_layers;
};
// Views are borrowed from immutable prepared resources, never guest pointers.
class DecodedClip {
    ByteView bytes_;
    std::uint32_t frames_=0,poses_[2]{},motions_=0,textures_=0;
    std::size_t stride_=0;
    float fps_=0;
    static std::uint32_t word(ByteView b,std::size_t p) {
        return std::uint32_t(b[p])|std::uint32_t(b[p+1])<<8|std::uint32_t(b[p+2])<<16|std::uint32_t(b[p+3])<<24;
    }
    static float real(ByteView b,std::size_t p) {auto n=word(b,p);float f;std::memcpy(&f,&n,4);return f;}
    static std::array<float,4> rotation(std::array<float,4> a,std::array<float,4> b,float t) {
        auto normalize=[](std::array<float,4> q){float n=0;for(float v:q)n+=v*v;n=std::sqrt(n);for(auto& v:q)v/=n;return q;};
        a=normalize(a);b=normalize(b);float dot=0;for(int i=0;i<4;++i)dot+=a[i]*b[i];
        if(dot<0){dot=-dot;for(auto& v:b)v=-v;}
        dot=std::clamp(dot,-1.f,1.f);float wa=1-t,wb=t;
        if(dot<.9995f){const float angle=std::acos(dot),den=std::sin(angle);wa=std::sin((1-t)*angle)/den;wb=std::sin(t*angle)/den;}
        for(int i=0;i<4;++i)a[i]=wa*a[i]+wb*b[i];return normalize(a);
    }
public:
    std::uint32_t frames()const{return frames_;}
    float fps()const{return fps_;}
    std::uint32_t joints(unsigned set=0)const{return set<2?poses_[set]:0;}
    float duration()const{return frames_>1?float(frames_-1)/fps_:0;}
    static std::optional<DecodedClip> parse(ByteView b,std::size_t limit) {
        if(!b.data()||b.size()<32||b.size()>limit||std::memcmp(b.data(),"EXCLIP1\0",8))return {};
        DecodedClip c;c.bytes_=b;c.frames_=word(b,8);c.fps_=real(b,12);c.poses_[0]=word(b,16);c.poses_[1]=word(b,20);c.motions_=word(b,24);c.textures_=word(b,28);
        if(!c.frames_||c.frames_>65535||!std::isfinite(c.fps_)||c.fps_<=0||c.fps_>240||c.poses_[0]>72||c.poses_[1]>72||c.motions_>3||c.textures_>5)return {};
        c.stride_=40*(c.poses_[0]+c.poses_[1])+28*c.motions_+4*c.textures_;
        if(!c.stride_||std::uint64_t(c.frames_)*c.stride_+32!=b.size())return {};
        for(std::uint32_t f=0;f<c.frames_;++f){std::size_t p=32+f*c.stride_;
            for(std::uint32_t i=0;i<c.poses_[0]+c.poses_[1]+c.motions_;++i){
                const auto floats=i<c.poses_[0]+c.poses_[1]?10u:7u;float norm=0;
                for(unsigned j=0;j<floats;++j){const auto v=real(b,p+4*j);if(!std::isfinite(v))return {};if(j>=3&&j<7)norm+=v*v;}
                if(norm<1e-8f||!std::isfinite(norm))return {};p+=floats*4;
            }
        }
        return c;
    }
    // Caller explicitly chooses looping only for an admitted loop clip. Never
    // interpolate the last frame into the first or across a clip replacement.
    std::optional<ClipSample> sample(double seconds,bool loop)const {
        if(!std::isfinite(seconds))return {};
        const double duration=double(frames_-1)/fps_,time=std::max(0.,seconds);
        double frame=loop&&frames_>1?std::fmod(time,duration)*fps_:std::min(time,duration)*fps_;
        frame=std::clamp(frame,0.,double(frames_-1));
        const auto first=std::uint32_t(frame),second=std::min(first+1,frames_-1);const float t=float(frame-first);
        const auto a=32+first*stride_,b=32+second*stride_;std::size_t offset=0;ClipSample result;result.frame=first;
        const auto joint=[&](bool motion){JointPose out;std::array<float,4> qa{},qb{};
            for(int i=0;i<3;++i){float x=real(bytes_,a+offset+4*i),y=real(bytes_,b+offset+4*i);out.position[i]=x+(y-x)*t;}
            for(int i=0;i<4;++i){qa[i]=real(bytes_,a+offset+12+4*i);qb[i]=real(bytes_,b+offset+12+4*i);}
            out.rotation=rotation(qa,qb,t);out.scale={1,1,1};
            if(!motion)for(int i=0;i<3;++i){float x=real(bytes_,a+offset+28+4*i),y=real(bytes_,b+offset+28+4*i);out.scale[i]=x+(y-x)*t;}
            offset+=motion?28:40;return out;};
        for(int set=0;set<2;++set){result.poses[set].reserve(poses_[set]);for(std::uint32_t j=0;j<poses_[set];++j)result.poses[set].push_back(joint(false));}
        for(std::uint32_t j=0;j<motions_;++j)result.motions.push_back(joint(true));
        // Face texture selections are discrete, not interpolated numeric IDs.
        for(std::uint32_t j=0;j<textures_;++j)result.texture_layers.push_back(word(bytes_,a+offset+4*j));
        return result;
    }
};
} // namespace editor_dashboard::appearance
