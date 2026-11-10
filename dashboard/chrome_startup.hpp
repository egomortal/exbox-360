#pragma once
#include "chrome_rle.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>
#include <new>
#include <utility>
namespace chrome_startup {
struct Palette { unsigned dark, mid, light, high, ink; };
inline constexpr Palette palettes[] = {
    {0x873c24,0xbe6442,0xe3a67b,0xf2c8a2,0x472817},
    {0x936218,0xd69c30,0xe9c65c,0xf5dea1,0x554116},
    {0x2b6b2a,0x61b640,0x9bce70,0xb8df8b,0x23431c},
    {0x163f8b,0x2b72d0,0x77a5e0,0xa6c9ed,0x193f75},
    {0x604775,0x9173b0,0xb8a6ce,0xd7c9e5,0x40334e}
};
inline unsigned mix(unsigned a, unsigned b, float t) {
    t=std::clamp(t,0.0f,1.0f);
    unsigned out=0;
    for (int shift:{0,8,16}) out|=unsigned(((a>>shift)&255)*(1-t)+((b>>shift)&255)*t)<<shift;
    return out;
}
inline float curve(float y) {
    constexpr float points[][2]={{0,0},{112,11},{154,-18},{330,-11},{650,-5},{748,0},{842,34},{920,44},{1080,14}};
    for (int i=1;i<9;i++) if (y<=points[i][0]) {
        float t=(y-points[i-1][0])/(points[i][0]-points[i-1][0]);
        t=t*t*(3-2*t);
        return points[i-1][1]*(1-t)+points[i][1]*t;
    }
    return 14;
}
inline unsigned metal(float d, float y) {
    float shine=0.5f+0.29f*std::cos((d-23)*0.077f)+0.04f*std::sin(y*0.006f);
    unsigned c=mix(0x6f7475,0xeeeeea,shine);
    if (d<5) c=mix(0x323b3a,0xf9fbf4,std::clamp(d/4,0.0f,1.0f));
    if (d>56) c=mix(c,0x444e48,std::min(0.9f,(d-56)/9));
    return c;
}
inline bool raster(std::uint32_t* pixels,size_t count,int d,int active,int first) {
    if(!pixels||(d!=1&&d!=2)||first<0||first>1||active<(first?1:0)||active>=5||count!=size_t(1920*1080)*d*d)return false;
    const int width=1920*d,height=1080*d;
    const auto &p=palettes[active];
    const float density=float(d);
    for (int py=0;py<height;py++) {
        const float y=(py+0.5f)/density-0.5f;
        const float bend=curve(float(y));
        const float left=112+(active-first)*64+first*32+bend, right=1480+active*64-first*48-bend;
        const float row_shade=0.12f+0.13f*std::sin(float(y)/1080*3.14159f);
        const auto green_y=std::pow((y-450)/380.0f,2);
        const float glass=std::exp(-std::pow((y-180)/95.0f,2))*0.18f;
        const unsigned cap=y<132?mix(p.dark,p.mid,float(y)/132*0.65f):mix(p.dark,p.mid,float(y-895)/185);
        for (int px=0;px<width;px++) {
            const float x=(px+0.5f)/density-0.5f;
            unsigned c=0;
            if (!(x>=left && x<=right)) {
            const float outside=std::min(float(x),float(1919-x));
            c=mix(0x666d6a,0xd6d9d3,std::clamp(outside/130,0.0f,1.0f));
            for (int i=first;i<active;i++) {
                const float d=x-(112+(i-first)*64+first*32+bend);
                if (d>=-1 && d<66) c=mix(c,metal(d,float(y)),std::min(d+1,1.0f));
            }
            for (int i=4;i>active;i--) {
                const float edge=right+(i-active-1)*64;
                const float d=x-(edge-bend*0.12f);
                if (d>=-1 && d<68) c=mix(c,metal(64-d,float(y)),std::min(d+1,1.0f));
            }
            }
            if (x>=left-1 && x<=right+1) {
                const float edge=std::min(x-left,right-x);
                unsigned face;
                if (x<left+64) {
                    float d=x-left;
                    face=mix(p.dark,p.light,0.3f+0.3f*std::sin(d/64*3.14159f));
                    if (d>57) face=mix(face,p.dark,(d-57)/8);
                } else {
                    if(y<132 || y>895) face=cap;
                    else {
                        const float across=(x-left)/std::max(1.0f,right-left);
                        if(active==2) {
                            const float glow=std::exp(-std::pow((across-0.83f)/0.30f,2)-green_y);
                            face=mix(0x398c30,0x60df42,glow);
                        } else {
                            face=mix(p.mid,p.light,0.35f+0.25f*std::sin(across*3.14159f));
                            face=mix(face,p.dark,row_shade);
                        }
                        if(y>=132 && y<895) {
                            face=mix(face,p.high,glass);
                            if(y<137) face=mix(face,p.dark,0.15f);
                        }
                    }
                    if(edge<10) face=mix(p.dark,face,std::clamp(edge/10,0.0f,1.0f));
                }
                c=mix(c,face,std::clamp(edge+1,0.0f,1.0f));
            }
            pixels[size_t(py)*width+px]=0xff000000u|(c&0xffffffu);
        }
    }
    return true;
}

struct Result {
 chrome_rle::Backing packets[2][5];size_t bytes=0;int density=0;bool ok=false;
};
inline Result build(int density,size_t cap=128u<<20,int fail_after=-1){
 Result result;if(density!=1&&density!=2)return result;
 const size_t count=size_t(1920*1080)*density*density;
 std::vector<std::uint32_t> scratch(count);if(scratch.capacity()>count)return result;
 int completed=0;
 for(int f=0;f<2;++f)for(int a=f?1:0;a<5;++a){
  if(completed==fail_after)throw std::bad_alloc();
  if(!raster(scratch.data(),count,density,a,f)||!result.packets[f][a].assign(scratch.data(),count,cap-result.bytes))return Result{};
  result.bytes+=result.packets[f][a].bytes();++completed;
 }
 result.density=density;result.ok=true;return result;
}
class Job {
 std::thread thread_;Result result_;bool active_=false;
public:
 ~Job(){join();}Job()=default;Job(const Job&)=delete;Job&operator=(const Job&)=delete;
 bool start(int density,size_t cap=128u<<20,int fail_after=-1){
  if(active_)return false;result_=Result{};
  try{thread_=std::thread([this,density,cap,fail_after]{try{result_=build(density,cap,fail_after);}catch(...){result_=Result{};}});active_=true;return true;}catch(...){return false;}
 }
 bool take(Result& result){if(!active_)return false;join();result=std::move(result_);return true;}
 void join(){if(thread_.joinable())thread_.join();active_=false;}
};
}
