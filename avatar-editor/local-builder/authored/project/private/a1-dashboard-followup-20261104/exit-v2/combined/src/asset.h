#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace av2::legend_art {
constexpr unsigned width=256,height=40;
constexpr size_t max_encoded_bytes=64*1024;
inline bool decode(uint8_t*dst,size_t pitch,const uint8_t*src,size_t size){
 if(!dst||!src||pitch<width*4)return false;
 size_t at=0,pixel=0;
 while(at<size){
  if(size-at<6)return false;unsigned n=unsigned(src[at])|(unsigned(src[at+1])<<8);
  if(!n||n>size_t(width)*height-pixel)return false;
  for(unsigned i=0;i<n;++i,++pixel){auto*q=dst+(pixel/width)*pitch+(pixel%width)*4;for(unsigned c=0;c<4;++c)q[c]=src[at+2+c];}at+=6;
 }return pixel==size_t(width)*height;
}
inline const char* runtime_path(){
#if defined(AV2_LEGEND_HOST_QA)
 if(const char*p=std::getenv("A1_LEGEND_ASSET_PATH"))return p;
#endif
 return "/app0/assets/ui/dashboard-legend.rle";
}
class Asset {
 std::vector<uint8_t> data_;
 static uint32_t u32(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 public:
 bool load(const char*path){
  data_.clear();if(!path||!*path)return false;FILE*f=std::fopen(path,"rb");if(!f)return false;
  uint8_t h[32]{};bool ok=std::fread(h,1,sizeof(h),f)==sizeof(h);
  const uint32_t size=u32(h+20),expected=u32(h+24);
  if(!ok||std::memcmp(h,"A1LEGEND",8)||u32(h+8)!=1||u32(h+12)!=width||u32(h+16)!=height||u32(h+28)||!size||size>max_encoded_bytes||size%6){std::fclose(f);return false;}
  try{data_.resize(size);}catch(...){std::fclose(f);return false;}
  ok=std::fread(data_.data(),1,size,f)==size;uint8_t extra=0;if(std::fread(&extra,1,1,f)||std::ferror(f))ok=false;
  if(std::fclose(f)!=0)ok=false;uint32_t hash=2166136261u;for(uint8_t b:data_)hash=(hash^b)*16777619u;
  if(!ok||hash!=expected){data_.clear();return false;}return true;
 }
 bool fill(uint8_t*dst,size_t pitch)const{return !data_.empty()&&decode(dst,pitch,data_.data(),data_.size());}
};
}
