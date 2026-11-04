// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
#include <algorithm>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstring>
extern "C" unsigned char* stbi_load_from_memory(const unsigned char*,int,int*,int*,int*,int);
extern "C" void stbi_image_free(void*);
namespace exbox::editor {
using rex::X_RESULT;
struct GamerPixels {std::vector<uint8_t> argb;uint32_t w=0,h=0;};
const GamerPixels& gamer_pixels(){
    static const GamerPixels cache=[]{
        GamerPixels result;auto name=rex::cvar::Query<std::string>("user_gamerpic");if(name.empty())return result;
        std::filesystem::path path=name;if(path.is_relative())path=rex::filesystem::GetExecutableFolder()/path;
        std::vector<uint8_t> bytes;
        if(auto* file=rex::filesystem::OpenFile(path,"rb")){
            std::fseek(file,0,SEEK_END);auto length=std::ftell(file);std::fseek(file,0,SEEK_SET);
            if(length>0){bytes.resize(size_t(length));std::fread(bytes.data(),1,bytes.size(),file);}std::fclose(file);
        }
        if(bytes.empty())return result;
        int w=0,h=0,channels=0;std::unique_ptr<uint8_t,decltype(&stbi_image_free)> rgba(stbi_load_from_memory(bytes.data(),int(bytes.size()),&w,&h,&channels,4),&stbi_image_free);
        if(!rgba)return result;result.w=w;result.h=h;result.argb.resize(size_t(w)*h*4);
        for(size_t i=0;i<result.argb.size();i+=4){result.argb[i]=rgba.get()[i+3];std::memcpy(result.argb.data()+i+1,rgba.get()+i,3);}
        return result;
    }();return cache;
}
u32 read_texture(u32,u32,u64 tile,u32,mapped_void target,u32 stride,u32 height,u32 overlap){
    const auto& picture=gamer_pixels();auto size=size_t(stride)*uint32_t(height);
    if(!picture.argb.empty()&&target&&stride>=4&&height){
        auto* out=target.as<uint8_t*>();std::memset(out,0,size);
        uint32_t w=std::min<uint32_t>(stride/4,256),h=std::min<uint32_t>(height,256);
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
            size_t src=(size_t(y)*picture.h/h*picture.w+size_t(x)*picture.w/w)*4;
            std::memcpy(out+size_t(y)*stride+x*4,picture.argb.data()+src,4);
        }
    }else{
        if(!tile)return X_ERROR_INVALID_PARAMETER;
        if(!target&&size)return X_ERROR_INVALID_PARAMETER; // Defensive only; invalid memory is outside parity.
        if(size)std::memset(target.as<uint8_t*>(),255,size);
    }
    if(overlap){rex::system::kernel_state()->CompleteOverlappedImmediate(overlap,X_ERROR_SUCCESS);return X_ERROR_IO_PENDING;}
    return X_ERROR_SUCCESS;
}
}
REX_HOOK(__wrap___imp__XamReadTileToTexture,exbox::editor::read_texture)
