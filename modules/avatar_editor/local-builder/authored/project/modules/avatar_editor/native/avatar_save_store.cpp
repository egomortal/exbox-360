#include "avatar_save_store.h"
#include "ticket_file.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
namespace av2::native {
namespace {
std::unique_ptr<AvatarSaveStore> active;
bool png_size(const void* data,std::size_t size,std::uint32_t& w,std::uint32_t& h) {
  const auto* p=static_cast<const unsigned char*>(data);
  static constexpr unsigned char signature[]={137,80,78,71,13,10,26,10};
  if(!p || size<33 || std::memcmp(p,signature,8)!=0 || std::memcmp(p+12,"IHDR",4)!=0) return false;
  auto be=[&](std::size_t at){return (std::uint32_t(p[at])<<24)|(std::uint32_t(p[at+1])<<16)|(std::uint32_t(p[at+2])<<8)|p[at+3];};
  if(be(8)!=13) return false;
  w=be(16);h=be(20);return w && h && w<=4096 && h<=4096;
}
bool publish(const std::filesystem::path& path,const void* data,std::size_t size,bool backup) {
  const auto bytes=std::string_view(static_cast<const char*>(data),size);
  const auto result=write_atomic_bytes(path,bytes,backup);
  if(result==TicketWrite::published) std::fprintf(stderr,"AV2 save published; directory fsync unavailable: %s\n",path.c_str());
  return result!=TicketWrite::failed;
}
}
AvatarSaveStore::AvatarSaveStore(std::filesystem::path folder):folder_(std::move(folder)) {
  std::error_code error;std::filesystem::create_directories(folder_,error);
  failed_=bool(error);
  // A gamer picture is an explicit photo-booth choice. Keep that choice across
  // later clothing edits; metadata says whether it was retaken this session.
  const int fd=open((folder_/"gamerpic.png").c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
  if(fd>=0){
    unsigned char header[33]{};struct stat st{};
    if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=33&&st.st_size<=128*1024*1024&&read(fd,header,sizeof(header))==sizeof(header))
      (void)png_size(header,sizeof(header),gamerpic_.width,gamerpic_.height);
    close(fd);
  }
}
bool AvatarSaveStore::manifest(const void* bytes,std::size_t size,bool explicit_save) {
  std::lock_guard lock(mutex_);
  if(!bytes || !size || size>65536) return false;
  const bool ok=publish(folder_/"avatar_manifest.bin",bytes,size,true);
  if(!ok) failed_=true;
  if(ok && explicit_save) saved_=true;
  return ok;
}
bool AvatarSaveStore::image(const char* name,const void* bytes,std::size_t size) {
  std::lock_guard lock(mutex_);
  if(!name || (std::strcmp(name,"fullbody.png") && std::strcmp(name,"gamerpic.png") && std::strcmp(name,"gamerpic_small.png"))) return false;
  Picture image;
  if(size>128u*1024u*1024u || !png_size(bytes,size,image.width,image.height)) {failed_=true;return false;}
  if(!publish(folder_/name,bytes,size,false)){failed_=true;return false;}
  image.written=true;
  if(std::strcmp(name,"fullbody.png")==0) fullbody_=image;
  if(std::strcmp(name,"gamerpic.png")==0) gamerpic_=image;
  return true;
}
bool AvatarSaveStore::saved() const {std::lock_guard lock(mutex_);return saved_;}
bool AvatarSaveStore::finalize() {
  std::lock_guard lock(mutex_);
  if(!saved_) return !failed_;
  // Metadata is committed last, after the guest's Save and Exit work drains.
  // A partial image failure is explicit; never advertise stale art as current.
  const bool complete=!failed_ && fullbody_.written && gamerpic_.width && gamerpic_.height;
  const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::ostringstream json;
  json<<"{\n  \"schema\": 1,\n  \"saved_at_unix\": "<<seconds<<",\n  \"manifest\": \"avatar_manifest.bin\",\n  \"complete\": "<<(complete?"true":"false")<<",\n";
  auto picture=[&](const char* name,const Picture& image,bool comma){json<<"  \""<<name<<"\": {\"width\": "<<image.width<<", \"height\": "<<image.height<<", \"written_this_session\": "<<(image.written?"true":"false")<<"}"<<(comma?",":"")<<"\n";};
  picture("fullbody.png",fullbody_,true);picture("gamerpic.png",gamerpic_,false);json<<"}\n";
  const auto bytes=json.str();return publish(folder_/"avatar.json",bytes.data(),bytes.size(),false) && complete;
}
void configure_avatar_save_store(const std::filesystem::path& folder){active=std::make_unique<AvatarSaveStore>(folder);}
bool persist_avatar_manifest(const void* bytes,std::size_t size,bool explicit_save){return active && active->manifest(bytes,size,explicit_save);}
bool persist_avatar_image(const char* name,const void* bytes,std::size_t size){return active && active->image(name,bytes,size);}
bool finalize_avatar_save(){return active && active->finalize();}
bool avatar_was_saved(){return active && active->saved();}
}
