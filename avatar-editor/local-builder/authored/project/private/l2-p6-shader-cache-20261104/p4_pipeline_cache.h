#pragma once
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>

namespace av2::pipeline_cache {
constexpr size_t MaxBytes=16u*1024u*1024u;
constexpr size_t EnvelopeBytes=64;
struct Identity { uint32_t vendor=0,device=0,driver=0; uint8_t uuid[16]{}; };
inline uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint64_t u64(const uint8_t* p){return uint64_t(u32(p))|(uint64_t(u32(p+4))<<32);}
inline void put32(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));}
inline void put64(uint8_t* p,uint64_t v){put32(p,uint32_t(v));put32(p+4,uint32_t(v>>32));}
inline uint64_t checksum(const uint8_t* data,size_t size){uint64_t h=14695981039346656037ULL;for(size_t i=0;i<size;++i){h^=data[i];h*=1099511628211ULL;}return h;}
inline bool valid_vulkan(const uint8_t* data,size_t size,const Identity& id){
 return size>=32 && size<=MaxBytes && u32(data)==32 && u32(data+4)==1 && u32(data+8)==id.vendor && u32(data+12)==id.device && std::memcmp(data+16,id.uuid,16)==0;
}
inline bool all_read(int fd,void* dst,size_t size){auto* p=static_cast<uint8_t*>(dst);while(size){ssize_t n=read(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;size-=size_t(n);}return true;}
inline bool all_write(int fd,const void* src,size_t size){auto* p=static_cast<const uint8_t*>(src);while(size){ssize_t n=write(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;size-=size_t(n);}return true;}
inline bool sync_file(int fd){int rc;do{rc=fsync(fd);}while(rc<0&&errno==EINTR);return rc==0;}
inline bool load(const std::string& path,const Identity& id,std::vector<uint8_t>& blob){
 blob.clear();int fd=open(path.c_str(),O_RDONLY);if(fd<0)return false;
 struct stat st{};uint8_t h[EnvelopeBytes]{};bool ok=fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size>=ssize_t(EnvelopeBytes+32) && uint64_t(st.st_size)<=EnvelopeBytes+MaxBytes;
 if(ok)ok=all_read(fd,h,sizeof(h)) && std::memcmp(h,"AV2PC01\0",8)==0 && u32(h+8)==1 && u32(h+12)==EnvelopeBytes && u64(h+16)==uint64_t(st.st_size)-EnvelopeBytes && u32(h+32)==id.driver && u32(h+36)==0 && std::memcmp(h+40,id.uuid,16)==0 && u32(h+56)==id.vendor && u32(h+60)==id.device;
 if(ok){try{blob.resize(size_t(u64(h+16)));}catch(...){ok=false;}}
 if(ok)ok=all_read(fd,blob.data(),blob.size()) && checksum(blob.data(),blob.size())==u64(h+24) && valid_vulkan(blob.data(),blob.size(),id);
 (void)close(fd);if(!ok)blob.clear();return ok;
}
inline bool save_atomic(const std::string& path,const Identity& id,const std::vector<uint8_t>& blob){
 if(!valid_vulkan(blob.data(),blob.size(),id))return false;
 const auto slash=path.find_last_of('/');if(slash==std::string::npos)return false;
 const std::string directory=path.substr(0,slash);if(mkdir(directory.c_str(),0755)!=0 && errno!=EEXIST)return false;
 const std::string temporary=path+".tmp";int fd=open(temporary.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd<0)return false;
 uint8_t h[EnvelopeBytes]{};std::memcpy(h,"AV2PC01\0",8);put32(h+8,1);put32(h+12,EnvelopeBytes);put64(h+16,blob.size());put64(h+24,checksum(blob.data(),blob.size()));put32(h+32,id.driver);std::memcpy(h+40,id.uuid,16);put32(h+56,id.vendor);put32(h+60,id.device);
 bool ok=all_write(fd,h,sizeof(h)) && all_write(fd,blob.data(),blob.size()) && sync_file(fd);if(close(fd)!=0)ok=false;
 // POSIX rename replaces atomically: never remove the last good cache first.
 if(ok)ok=rename(temporary.c_str(),path.c_str())==0;
 if(!ok){(void)unlink(temporary.c_str());return false;}
 // File content is durable before replacement. Directory sync is best effort
 // because native titles may refuse opening a directory; rename stays atomic.
 const int dir_fd=open(directory.c_str(),O_RDONLY);if(dir_fd>=0){(void)sync_file(dir_fd);(void)close(dir_fd);}
 return true;
}
}
