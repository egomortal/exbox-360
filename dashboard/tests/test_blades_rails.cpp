// Verify cached straight-alpha rail pixels against direct source-over layers.
#include "../blades_rail_roles.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>

int main() {
    const int revisions[]={1888,2241,2255,2258,2858,2868,4532,4548,4552,5759,5766,5787,6683,6690,6717,6770};
    const int recovered_points[]={23,23,23,23,23,23,23,23,23,21,21,21,18,18,18,18};
    for (int i=0;i<16;i++) {
        if (blades_rails::source_points(revisions[i])!=recovered_points[i]) return 1;
    }
    auto *cached=SDL_CreateRGBSurfaceWithFormat(0,256,256,32,SDL_PIXELFORMAT_ARGB8888);
    auto *target=SDL_CreateRGBSurfaceWithFormat(0,256,256,32,SDL_PIXELFORMAT_ARGB8888);
    if (!cached || !target) return 2;
    for (int shadow=0;shadow<256;shadow++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(cached->pixels)+shadow*cached->pitch);
        for (int ink=0;ink<256;ink++) {
            const auto pixel=blades_rails::composite_coverage(Uint8(ink),Uint8(shadow));
            row[ink]=SDL_MapRGBA(cached->format,pixel.gray,pixel.gray,pixel.gray,pixel.alpha);
        }
    }
    SDL_SetSurfaceBlendMode(cached,SDL_BLENDMODE_BLEND);
    const SDL_Color backgrounds[]={{0,0,0,255},{255,255,255,255},{200,200,200,255},{214,156,48,255},{43,114,208,255}};
    int maximum_error=0;
    for (const auto background:backgrounds) {
        SDL_FillRect(target,nullptr,SDL_MapRGBA(target->format,background.r,background.g,background.b,255));
        SDL_BlitSurface(cached,nullptr,target,nullptr);
        for (int shadow=0;shadow<256;shadow++) {
            const auto *row=reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(target->pixels)+shadow*target->pitch);
            for (int ink=0;ink<256;ink++) {
                Uint8 red,green,blue,alpha;
                SDL_GetRGBA(row[ink],target->format,&red,&green,&blue,&alpha);
                const Uint8 actual[]={red,green,blue};
                const Uint8 backdrop[]={background.r,background.g,background.b};
                const double ink_alpha=double(ink)/255*150/255;
                const double shadow_alpha=double(shadow)/255*128/255;
                for (int channel=0;channel<3;channel++) {
                    const double shadow_layer=235*shadow_alpha+backdrop[channel]*(1-shadow_alpha);
                    const int direct=int(std::lround(15*ink_alpha+shadow_layer*(1-ink_alpha)));
                    maximum_error=std::max(maximum_error,std::abs(int(actual[channel])-direct));
                }
            }
        }
    }
    SDL_FreeSurface(cached);
    SDL_FreeSurface(target);
    std::printf("16 recovered revision mappings; 327680 cached-pixel/background cases; maximum direct-layer RGB error %d\n",maximum_error);
    return maximum_error<=3?0:3;
}
