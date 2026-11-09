#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace av2::prompt_art {
constexpr unsigned width=960,height=480,states=3;
constexpr size_t max_encoded_bytes=1024*1024;
inline bool decode(uint8_t* dst,size_t pitch,const uint8_t* src,size_t size){
 if(!dst||!src||pitch<width*4)return false;
 size_t at=0,pixel=0;
 while(at<size){
  if(size-at<6)return false;
  const unsigned count=unsigned(src[at])|(unsigned(src[at+1])<<8);
  if(!count||count>size_t(width)*height-pixel)return false;
  for(unsigned n=0;n<count;++n,++pixel){auto*q=dst+(pixel/width)*pitch+(pixel%width)*4;for(unsigned c=0;c<4;++c)q[c]=src[at+2+c];}
  at+=6;
 }
 return pixel==size_t(width)*height;
}
inline const char* runtime_path(){
#if defined(AV2_PROMPT_HOST_QA)
 if(const char* override_path=std::getenv("A1_PROMPT_ATLAS_PATH"))return override_path;
#endif
 return "/app0/assets/ui/back-prompt.rle";
}
class Atlas {
 std::vector<uint8_t> bytes_;
 uint32_t offsets_[4]{};
 static uint32_t u32(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 public:
 bool load(const char*path){
  bytes_.clear();
  if(!path||!*path)return false;
  FILE*f=std::fopen(path,"rb");if(!f)return false;
  uint8_t header[48]{};bool ok=std::fread(header,1,sizeof(header),f)==sizeof(header);
  if(!ok||std::memcmp(header,"A1PROMPT",8)||u32(header+8)!=1||u32(header+12)!=width||u32(header+16)!=height||u32(header+20)!=states){std::fclose(f);return false;}
  const uint32_t size=u32(header+24),checksum=u32(header+28);
  if(!size||size>max_encoded_bytes){std::fclose(f);return false;}
  for(unsigned n=0;n<4;++n)offsets_[n]=u32(header+32+4*n);
  if(offsets_[0]!=0||offsets_[3]!=size){std::fclose(f);return false;}
  for(unsigned n=0;n<3;++n)if(offsets_[n]>=offsets_[n+1]||offsets_[n+1]>size||(offsets_[n+1]-offsets_[n])%6){std::fclose(f);return false;}
  try{bytes_.resize(size);}catch(...){std::fclose(f);return false;}
  ok=std::fread(bytes_.data(),1,size,f)==size;
  uint8_t extra=0;if(std::fread(&extra,1,1,f)!=0||std::ferror(f))ok=false;
  if(std::fclose(f)!=0)ok=false;
  uint32_t actual=2166136261u;for(uint8_t byte:bytes_)actual=(actual^byte)*16777619u;
  if(!ok||actual!=checksum){bytes_.clear();return false;}
  return true;
 }
 bool fill(uint8_t*dst,size_t pitch,unsigned choice)const{
  return choice<states&&!bytes_.empty()&&decode(dst,pitch,bytes_.data()+offsets_[choice],offsets_[choice+1]-offsets_[choice]);
 }
};
inline unsigned selected(uint64_t s){return uint32_t(s)<states?uint32_t(s):2;}
inline bool valid(uint64_t s){return (s>>32)!=0&&uint32_t(s)<states;}
}
