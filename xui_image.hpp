#pragma once
#include <algorithm>
namespace xui {
struct ImagePlacement {float x=0,y=0,width=0,height=0;bool clip=true;};
// Owner 1888 XAM, image layout 0x819221c8. These are ordered flag tests.
inline ImagePlacement image_placement(float width,float height,float image_width,float image_height,unsigned mode) {
    ImagePlacement out{0,0,image_width,image_height,true};
    if (image_width<=0 || image_height<=0) return {};
    if (mode&1) {out.clip=false;return out;}
    if (mode&2) {out.x=(width-image_width)*.5f;out.y=(height-image_height)*.5f;return out;}
    if (mode&4) {out.width=width;out.height=height;return out;}
    if (mode&(8|16)) {
        const float scale=std::min(width/image_width,height/image_height);out.width=image_width*scale;out.height=image_height*scale;
        if (mode&16) {out.x=(width-out.width)*.5f;out.y=(height-out.height)*.5f;}
    }
    return out;
}
}
