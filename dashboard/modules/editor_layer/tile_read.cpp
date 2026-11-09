// SPDX-License-Identifier: GPL-3.0-or-later
#include "tile_sampling.hpp"
#include "../../private/av2/title-port-source/kernel/xam/avatars/closet.h"
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
#include <memory>
#include <cstring>
extern "C" unsigned char* stbi_load_from_memory(const unsigned char*,int,int*,int*,int*,int);
extern "C" void stbi_image_free(void*);
namespace exbox::editor {
using rex::X_RESULT;
thread_local uint32_t tile_hint=0;
uint32_t read_tile_bytes(uint32_t game,mapped_void output,mapped_u32 size) {
    const auto hint=tile_hint;tile_hint=0; // Consume even on invalid-buffer calls.
    if(!output||!size)return X_ERROR_INVALID_PARAMETER;
    std::vector<uint8_t> bytes;
    if(!game||!rex::avatars::GetCloset().ReadTitleIcon(game,bytes)){
        static const auto blank=[]{std::vector<uint8_t> pixels(64*64*4),png;rex::avatars::EncodePng(pixels.data(),64,64,png);return png;}();
        if(blank.empty()||uint32_t(*size)<blank.size())return X_ERROR_FILE_NOT_FOUND;
        std::memcpy(output.as<uint8_t*>(),blank.data(),blank.size());*size=blank.size();return X_ERROR_SUCCESS;
    }
    const uint32_t capacity=*size;
    int w=0,h=0,channels=0;
    std::unique_ptr<uint8_t,decltype(&stbi_image_free)> rgba(stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&channels,4),&stbi_image_free);
    if(rgba){
        std::vector<uint8_t> fitted;
        for(const auto side:tile_size_ladder(hint)){
            if(w<int(side)&&h<int(side))continue;
            auto pixels=area_tile({rgba.get(),size_t(w)*h*4},w,h,side);
            std::vector<uint8_t> png;
            if(rex::avatars::EncodePng(pixels.data(),side,side,png)&&png.size()<=capacity){fitted=std::move(png);break;}
        }
        if(fitted.empty()&&w>0&&h>0&&w<=128&&h<=128){
            std::vector<uint8_t> png;
            if(rex::avatars::EncodePng(rgba.get(),w,h,png)&&png.size()<=capacity)fitted=std::move(png);
        }
        if(!fitted.empty())bytes=std::move(fitted);
    }
    if(bytes.size()>capacity){*size=bytes.size();return X_ERROR_INSUFFICIENT_BUFFER;}
    std::memcpy(output.as<uint8_t*>(),bytes.data(),bytes.size());*size=bytes.size();return X_ERROR_SUCCESS;
}
u32 read_tile(u32,u32 game,u64,u32,mapped_void output,mapped_u32 size,mapped_void overlap){
    const auto result=read_tile_bytes(game,output,size);
    if(overlap){rex::system::kernel_state()->CompleteOverlappedImmediate(overlap.guest_address(),result);return X_ERROR_IO_PENDING;}
    return result;
}
u32 read_tile_ex(u32,u32 game,u64,u32,u32,u32,mapped_void output,mapped_u32 size){return read_tile_bytes(game,output,size);}
}
// External title hooks set a one-shot hint; keep it in the same thread as reads.
extern "C" void __wrap__ZN3rex6kernel3xam15SetTileSizeHintEj(uint32_t side){exbox::editor::tile_hint=side;}
REX_HOOK(__wrap___imp__XamReadTile,exbox::editor::read_tile)
REX_HOOK(__wrap___imp__XamReadTileEx,exbox::editor::read_tile_ex)
