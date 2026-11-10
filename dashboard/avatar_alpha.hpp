#pragma once
#include <SDL2/SDL.h>
#include "x21/surface_generations.hpp"
#include <cstdint>
#include <vector>

// Prepare transparent avatar sprites for linear sampling without softening
// their visible pixels. RGB is extended into fully transparent texels so a
// bilinear sample cannot pull black/green fringe into the silhouette.
namespace avatar_alpha {
inline bool clean(SDL_Surface* surface) {
    if(!surface||!surface->format||surface->format->BytesPerPixel!=4||!surface->format->Amask)return false;
    if(SDL_MUSTLOCK(surface)&&SDL_LockSurface(surface))return false;
    const int w=surface->w,h=surface->h;const std::size_t count=std::size_t(w)*std::size_t(h);
    std::vector<Uint8> red(count),green(count),blue(count),alpha(count),known(count);
    for(int y=0;y<h;++y){
        auto* row=reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels)+y*surface->pitch);
        for(int x=0;x<w;++x){const std::size_t i=std::size_t(y)*w+x;SDL_GetRGBA(row[x],surface->format,&red[i],&green[i],&blue[i],&alpha[i]);known[i]=alpha[i]>0;}
    }
    const auto nearby=[&](int x,int y,Uint8 threshold){
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            if(!dx&&!dy)continue;const int xx=x+dx,yy=y+dy;
            if(xx>=0&&yy>=0&&xx<w&&yy<h&&alpha[std::size_t(yy)*w+xx]>=threshold)return true;
        }
        return false;
    };
    // Remove only detached sub-3% coverage dust. Connected antialiasing and
    // fine hair/clothing details remain unchanged.
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const std::size_t i=std::size_t(y)*w+x;if(alpha[i]<=7&&!nearby(x,y,24)){alpha[i]=0;known[i]=0;red[i]=green[i]=blue[i]=0;}}
    // Two hidden-colour texels cover bilinear sampling and the first mip edge.
    for(int pass=0;pass<2;++pass){
        auto next=known;auto nr=red,ng=green,nb=blue;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const std::size_t i=std::size_t(y)*w+x;if(alpha[i]||known[i])continue;
            unsigned sr=0,sg=0,sb=0,n=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                if(!dx&&!dy)continue;const int xx=x+dx,yy=y+dy;if(xx<0||yy<0||xx>=w||yy>=h)continue;
                const std::size_t j=std::size_t(yy)*w+xx;if(!known[j])continue;
                sr+=red[j];sg+=green[j];sb+=blue[j];++n;
            }
            if(n){nr[i]=Uint8(sr/n);ng[i]=Uint8(sg/n);nb[i]=Uint8(sb/n);next[i]=1;}
        }
        red.swap(nr);green.swap(ng);blue.swap(nb);known.swap(next);
    }
    surface_generations::invalidate(surface);
    for(int y=0;y<h;++y){
        auto* row=reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels)+y*surface->pitch);
        for(int x=0;x<w;++x){const std::size_t i=std::size_t(y)*w+x;row[x]=SDL_MapRGBA(surface->format,red[i],green[i],blue[i],alpha[i]);}
    }
    if(SDL_MUSTLOCK(surface))SDL_UnlockSurface(surface);
    surface_generations::seal(surface);return true;
}
}
