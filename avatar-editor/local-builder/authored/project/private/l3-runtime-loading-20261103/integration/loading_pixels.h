#pragma once
#include "loading_art.hpp"
#include <cstddef>
#include <cstdint>
namespace av2::loading {
constexpr uint32_t width=exbox::transition::width,height=exbox::transition::height;
inline bool fill_rgba(uint8_t* out,size_t stride){if(!out||stride<size_t(width)*4)return false;for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){const uint32_t c=exbox::transition::pixel(int(x),int(y));auto* d=out+size_t(y)*stride+size_t(x)*4;d[0]=uint8_t(c>>16);d[1]=uint8_t(c>>8);d[2]=uint8_t(c);d[3]=uint8_t(c>>24);}return true;}
struct Accounting {bool visible=false;uint64_t guest_presents=0;bool allow_guest(bool front)const{return front||!visible;}void loading_shown(){visible=true;}uint64_t guest_shown(){visible=false;return ++guest_presents;}};
}
