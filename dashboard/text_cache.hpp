#pragma once
#include "render_density.hpp"
#include <list>
#include <string>
#include <memory>
#include <new>
namespace text_cache {
inline constexpr size_t byte_limit=8u<<20,entry_limit=256;
struct Lease {
    SDL_Surface* image=nullptr;bool transient=false;
    Lease(SDL_Surface*s,bool owned):image(s),transient(owned){}
    Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
    ~Lease(){if(transient)render_density::free_surface(image);}
};
struct Entry {
    std::uint64_t serial;std::string value;SDL_Color color;
    int style,outline,hinting,kerning,height;SDL_Surface* image;size_t cost;
};
inline std::list<Entry> entries;
inline size_t bytes=0,hits=0,misses=0;
inline void clear(){for(auto&e:entries)render_density::free_surface(e.image);entries.clear();bytes=0;}
inline Lease acquire(TTF_Font*font,const std::string&value,SDL_Color color) {
    if(!font||value.empty())return {nullptr,false};
    const auto serial=render_density::font_serial(font);
    const int style=TTF_GetFontStyle(font),outline=TTF_GetFontOutline(font),hinting=TTF_GetFontHinting(font),kerning=TTF_GetFontKerning(font),height=TTF_FontHeight(font);
    if(serial)for(auto i=entries.begin();i!=entries.end();++i) {
        const auto&e=*i;
        if(e.serial==serial&&e.value==value&&e.color.r==color.r&&e.color.g==color.g&&e.color.b==color.b&&e.color.a==color.a&&e.style==style&&e.outline==outline&&e.hinting==hinting&&e.kerning==kerning&&e.height==height) {
            auto*image=e.image;entries.splice(entries.begin(),entries,i);++hits;return {image,false};
        }
    }
    ++misses;auto*image=render_density::text(font,value.c_str(),color);
    if(!image)return {nullptr,false};
    const size_t pixels=size_t(image->pitch)*image->h;
    if(!serial||value.size()>4096||pixels>byte_limit)return {image,true};
    std::unique_ptr<SDL_Surface,void(*)(SDL_Surface*)> pending(image,render_density::free_surface);
    try {
        Entry item{serial,value,color,style,outline,hinting,kerning,height,image,0};
        item.cost=pixels+sizeof(Entry)+2*sizeof(void*)+item.value.capacity()+1;
        if(item.cost>byte_limit)return {pending.release(),true};
        while(!entries.empty()&&(entries.size()>=entry_limit||bytes+item.cost>byte_limit)) {
            bytes-=entries.back().cost;render_density::free_surface(entries.back().image);entries.pop_back();
        }
        entries.push_front(std::move(item));bytes+=entries.front().cost;pending.release();return {image,false};
    }catch(const std::bad_alloc&){return {pending.release(),true};}
}
}
