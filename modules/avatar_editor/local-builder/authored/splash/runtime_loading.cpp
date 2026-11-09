#include "runtime_loading.h"
#include <array>
#include <cerrno>
#include <cstring>
#include <new>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "stb_image.h"
namespace av2::loading {
namespace {
constexpr size_t max_file=40u*1024u*1024u;
uint32_t be(const uint8_t*p){return(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
constexpr auto crc_table(){std::array<uint32_t,256>a{};for(unsigned i=0;i<256;++i){uint32_t c=i;for(unsigned n=0;n<8;++n)c=(c>>1)^((c&1)?0xedb88320u:0);a[i]=c;}return a;}
uint32_t crc(const uint8_t*p,size_t n){static constexpr auto a=crc_table();uint32_t c=~0u;for(size_t i=0;i<n;++i)c=a[(c^p[i])&255]^(c>>8);return~c;}
ExternalPixels fail(ExternalStatus s){ExternalPixels r;r.status=s;return r;}
bool target_ok(uint32_t w,uint32_t h){return w&&h&&w<=4096&&h<=2160;}
}
static ExternalPixels decode_impl(const uint8_t*p,size_t n,uint32_t tw,uint32_t th){
 if(!target_ok(tw,th))return fail(ExternalStatus::WrongDimensions);
 if(n>max_file)return fail(ExternalStatus::TooLarge);
 const uint8_t sig[]={137,80,78,71,13,10,26,10};
 if(!p||n<57||std::memcmp(p,sig,8))return fail(ExternalStatus::InvalidPng);
 uint32_t w=0,h=0,channels=0;bool header=false,idat=false,ended_idat=false,end=false;std::vector<uint8_t>z;
 for(size_t at=8;at<n;){
  if(n-at<12)return fail(ExternalStatus::InvalidPng);const size_t length=be(p+at);if(length>n-at-12)return fail(ExternalStatus::InvalidPng);
  const uint8_t*type=p+at+4,*body=p+at+8;if(crc(type,length+4)!=be(body+length))return fail(ExternalStatus::InvalidPng);
  if(!header){
   if(std::memcmp(type,"IHDR",4)||length!=13)return fail(ExternalStatus::InvalidPng);header=true;w=be(body);h=be(body+4);
   if(w!=tw||h!=th)return fail(ExternalStatus::WrongDimensions);
   if(body[8]!=8||(body[9]!=2&&body[9]!=6)||body[10]||body[11]||body[12])return fail(ExternalStatus::Unsupported);
   channels=body[9]==6?4:3;
  }else if(!std::memcmp(type,"IDAT",4)){
   if(ended_idat)return fail(ExternalStatus::InvalidPng);idat=true;z.insert(z.end(),body,body+length);
  }else if(!std::memcmp(type,"IEND",4)){
   if(length||!idat||at+12!=n)return fail(ExternalStatus::InvalidPng);end=true;break;
  }else{
   if(idat)ended_idat=true;
   // Only known, cheap ancillary metadata is accepted; no APNG, palette or transparency semantics.
   if(std::memcmp(type,"sRGB",4)&&std::memcmp(type,"gAMA",4)&&std::memcmp(type,"cHRM",4)&&std::memcmp(type,"pHYs",4)&&std::memcmp(type,"tEXt",4))return fail(ExternalStatus::Unsupported);
  }
  at+=length+12;
 }
 if(!end||z.empty())return fail(ExternalStatus::InvalidPng);
 // Use the already linked decoder's fixed-buffer inflate exactly once, then
 // reconstruct the five PNG filters without permitting allocation growth.
 const size_t row=size_t(w)*channels+1,raw_size=row*h;
 std::vector<uint8_t>raw(raw_size);const int count=stbi_zlib_decode_buffer(reinterpret_cast<char*>(raw.data()),int(raw.size()),reinterpret_cast<const char*>(z.data()),int(z.size()));
 if(count!=int(raw_size))return fail(ExternalStatus::DecodeFailed);
 ExternalPixels out;out.width=w;out.height=h;out.rgba.resize(size_t(w)*h*4);
 const auto paeth=[](int a,int b,int c){const int p=a+b-c;const int pa=std::abs(p-a),pb=std::abs(p-b),pc=std::abs(p-c);return pa<=pb&&pa<=pc?a:pb<=pc?b:c;};
 for(uint32_t y=0;y<h;++y){
  const unsigned filter=raw[size_t(y)*row];if(filter>4)return fail(ExternalStatus::InvalidPng);
  uint8_t*cur=raw.data()+size_t(y)*row+1;const uint8_t*prev=y?cur-row:nullptr;const size_t bytes=row-1;
  if(filter==1){for(size_t x=channels;x<bytes;++x)cur[x]=uint8_t(cur[x]+cur[x-channels]);}
  else if(filter==2){if(prev)for(size_t x=0;x<bytes;++x)cur[x]=uint8_t(cur[x]+prev[x]);}
  else if(filter==3){for(size_t x=0;x<bytes;++x)cur[x]=uint8_t(cur[x]+((unsigned(x>=channels?cur[x-channels]:0)+unsigned(prev?prev[x]:0))/2));}
  else if(filter==4){for(size_t x=0;x<bytes;++x)cur[x]=uint8_t(cur[x]+paeth(x>=channels?cur[x-channels]:0,prev?prev[x]:0,prev&&x>=channels?prev[x-channels]:0));}
  auto*dest=out.rgba.data()+size_t(y)*w*4;
  if(channels==4)std::memcpy(dest,cur,bytes);else for(uint32_t x=0;x<w;++x){dest[x*4]=cur[x*3];dest[x*4+1]=cur[x*3+1];dest[x*4+2]=cur[x*3+2];dest[x*4+3]=255;}
 }
 out.status=ExternalStatus::Ready;return out;
}
static ExternalPixels load_impl(uint32_t w,uint32_t h,const char*path){
 if(!target_ok(w,h)||!path)return fail(ExternalStatus::WrongDimensions);
 int fd=::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return fail(errno==ENOENT?ExternalStatus::Missing:ExternalStatus::IoError);
 struct Close{int fd;~Close(){::close(fd);}}close{fd};struct stat st{};
 if(::fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0)return fail(ExternalStatus::IoError);
 if(uint64_t(st.st_size)>max_file)return fail(ExternalStatus::TooLarge);
 std::vector<uint8_t>bytes(size_t(st.st_size));size_t at=0;
 while(at<bytes.size()){auto got=::read(fd,bytes.data()+at,bytes.size()-at);if(got<0&&errno==EINTR)continue;if(got<=0)return fail(ExternalStatus::IoError);at+=size_t(got);}
 uint8_t extra;ssize_t got;do{got=::read(fd,&extra,1);}while(got<0&&errno==EINTR);if(got!=0)return fail(ExternalStatus::IoError);
 return decode_external_png(bytes.data(),bytes.size(),w,h);
}
ExternalPixels decode_external_png(const uint8_t*p,size_t n,uint32_t w,uint32_t h){try{return decode_impl(p,n,w,h);}catch(const std::bad_alloc&){return fail(ExternalStatus::AllocationFailed);}}
ExternalPixels load_external_png(uint32_t w,uint32_t h,const char*p){try{return load_impl(w,h,p);}catch(const std::bad_alloc&){return fail(ExternalStatus::AllocationFailed);}}
ExternalPixels load_startup_png(uint32_t w,uint32_t h,const char* override_path,
    const char* packaged_1080p,const char* packaged_2160p){
 auto external=load_external_png(w,h,override_path);
 if(external.ready())return external;
 const char* packaged=(w==1920&&h==1080)?packaged_1080p:
                      (w==3840&&h==2160)?packaged_2160p:nullptr;
 if(!packaged)return external;
 return load_external_png(w,h,packaged);
}
}
