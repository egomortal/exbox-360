// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
#include <vector>
#include <cstring>
// Explicit remaining codec dependency; preserve the pinned compressor bytes.
namespace rex::avatars {
bool EncodePng(const uint8_t*,uint32_t,uint32_t,std::vector<uint8_t>&);
bool EncodePngRgb(const uint8_t*,uint32_t,uint32_t,uint32_t,std::vector<uint8_t>&);
}
namespace exbox::editor {
using rex::X_RESULT;
u32 encode_guest_image(mapped_void pixels,u32 width,u32 height,u32 stride,
                       mapped_void output,mapped_u32 size,u32,mapped_void overlap) {
    uint32_t status=X_ERROR_INVALID_PARAMETER;
    if (pixels && output && size && width && height && width<=4096 && height<=4096 && stride>=width*4) {
        const auto* source=pixels.as<const uint8_t*>();
        bool alpha=false;
        for(uint32_t y=0;y<height && !alpha;++y)
            for(uint32_t x=0;x<width;++x) if(source[size_t(y)*stride+x*4+3]!=255){alpha=true;break;}
        std::vector<uint8_t> packed,encoded;
        bool success;
        if(alpha) {
            packed.resize(size_t(width)*height*4);
            for(uint32_t y=0;y<height;++y)
                std::memcpy(packed.data()+size_t(y)*width*4,source+size_t(y)*stride,size_t(width)*4);
            success=rex::avatars::EncodePng(packed.data(),width,height,encoded);
        } else success=rex::avatars::EncodePngRgb(source,width,height,stride,encoded);
        if(!success) status=X_ERROR_FUNCTION_FAILED;
        else {
            const uint32_t capacity=*size;
            *size=static_cast<uint32_t>(encoded.size());
            status=encoded.size()<=capacity?X_ERROR_SUCCESS:X_ERROR_INSUFFICIENT_BUFFER;
            if(status==X_ERROR_SUCCESS)std::memcpy(output.as<uint8_t*>(),encoded.data(),encoded.size());
            // The guest may alias size with output; match the original write order.
            *size=static_cast<uint32_t>(encoded.size());
        }
    }
    if(overlap){rex::system::kernel_state()->CompleteOverlappedImmediate(overlap.guest_address(),status);return 0x800703e5u;}
    return status?0x80070000u|(status&0xffffu):0;
}
}
REX_HOOK(__wrap___imp__XamPngEncodeEx, exbox::editor::encode_guest_image)
