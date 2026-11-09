#include "paged_vfs.h"
#include "ps5_platform.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace av2::platform {
namespace {
AssetIndexLog index_log=nullptr;
std::string indexed_root; // startup-only configuration, immutable during use
constexpr size_t index_stride = 1040, index_header = 24;
std::uint32_t le32(const unsigned char* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint64_t le64(const unsigned char* p) {
  return le32(p) | (std::uint64_t(le32(p + 4)) << 32);
}
int read_at(int fd, void* buffer, std::size_t bytes, std::uint64_t offset) {
  auto* out = static_cast<unsigned char*>(buffer);
  while (bytes) {
    const auto got = pread(fd, out, bytes, static_cast<off_t>(offset));
    if (got < 0) { if (errno == EINTR) continue; const int error=errno;
      if(index_log)index_log("asset-index","function=pread fd=%d offset=%llu errno=%d",fd,static_cast<unsigned long long>(offset),error);
      return error; }
    if (!got) {
      if(index_log)index_log("asset-index","function=pread fd=%d offset=%llu short_read=1 errno=%d",fd,static_cast<unsigned long long>(offset),EIO);
      return EIO;
    }
    out += got; offset += static_cast<std::uint64_t>(got);
    bytes -= static_cast<std::size_t>(got);
  }
  return 0;
}
// Validate each record on access, including path traversal and record type.
int index_record(int fd, std::uint32_t row, std::string& path, std::uint64_t* size=nullptr, std::uint8_t* type=nullptr) {
  unsigned char bytes[index_stride]{};
  int error=read_at(fd,bytes,sizeof(bytes),index_header+std::uint64_t(row)*index_stride);
  if(error)return error;
  const auto* end=static_cast<const unsigned char*>(std::memchr(bytes,0,1024));
  if(!end || end==bytes || (bytes[1032]!=1 && bytes[1032]!=2))return EINVAL;
  if(size)*size=le64(bytes+1024);
  if(type)*type=bytes[1032];
  path.assign(reinterpret_cast<const char*>(bytes),size_t(end-bytes));
  if(path.front()=='/' || path.back()=='/' || path.find('\\')!=std::string::npos || path.find(':')!=std::string::npos)return EINVAL;
  size_t start=0;
  while(start<path.size()) {
    const auto slash=path.find('/',start);
    auto part=path.substr(start,slash==std::string::npos?slash:slash-start);
    if(part.empty() || part=="." || part==".." || part.size()>=256)return EINVAL;
    if(slash==std::string::npos)break;
    start=slash+1;
  }
  return 0;
}
std::string parent_of(const std::string& path) {
  const auto slash=path.rfind('/');return slash==std::string::npos?std::string{}:path.substr(0,slash);
}
int open_index(const std::string& root,std::uint32_t& count) {
  const int fd=::open((root+"/.av2-index").c_str(),O_RDONLY);
  if(fd<0){const int error=errno;if(index_log)index_log("asset-index","function=open flags=O_RDONLY path=%s/.av2-index errno=%d",root.c_str(),error);errno=error;return -1;}
  unsigned char header[index_header]{};struct stat st{};
  int error=read_at(fd,header,sizeof(header),0);
  if(!error && stat((root+"/.av2-index").c_str(),&st)) {
    error=errno;if(index_log)index_log("asset-index","function=stat path=%s/.av2-index errno=%d",root.c_str(),error);
  }
  count=le32(header+12);
  if(!error && (!S_ISREG(st.st_mode) || std::memcmp(header,"AV2IDX01",8) || le32(header+8)!=index_stride ||
      count>1000000 || le64(header+16)!=0 || st.st_size<0 ||
      std::uint64_t(st.st_size)!=index_header+std::uint64_t(count)*index_stride))error=EINVAL;
  if(error){close(fd);errno=error;return -1;}
  return fd;
}
bool guid_text(std::string_view value) {
  if (value.size() != 36) return false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
    else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}
} // namespace

void set_asset_index_logger(AssetIndexLog logger){index_log=logger;}
int indexed_asset_info(std::string_view full_path,std::uint64_t& bytes,std::uint8_t& type) {
  bytes=0;type=0;
  const std::string full(full_path);
  if(indexed_root.empty() || full.compare(0,indexed_root.size()+1,indexed_root+"/"))return ENOTSUP;
  const std::string relative=full.substr(indexed_root.size()+1),parent=parent_of(relative);
  if(relative.empty())return EINVAL;
  std::uint32_t count=0;const int fd=open_index(indexed_root,count);if(fd<0)return errno;
  std::uint32_t low=0,high=count;
  while(low<high) {
    const auto mid=low+(high-low)/2;std::string path;
    const int error=index_record(fd,mid,path,&bytes,&type);
    if(error){close(fd);return error;}
    const auto found_parent=parent_of(path);
    const int order=found_parent==parent?path.compare(relative):found_parent.compare(parent);
    if(!order){close(fd);return 0;}
    if(order<0)low=mid+1;else high=mid;
  }
  close(fd);bytes=0;type=0;return ENOENT;
}
int configure_asset_index(std::string root) {
  if(root.empty()){indexed_root.clear();return 0;}
  while(root.size()>1 && root.back()=='/')root.pop_back();
  if(root.front()!='/' || root=="/")return EINVAL;
  std::uint32_t count=0;const int fd=open_index(root,count);
  if(fd<0)return errno;
  close(fd);indexed_root=std::move(root);return 0;
}
DirectoryPage::~DirectoryPage() {
  if (directory_) (void)closedir(directory_);
  if (index_fd_>=0) (void)close(index_fd_);
}
int DirectoryPage::open(const char* root, std::string_view relative) {
  if (directory_ || index_fd_>=0) return EBUSY;
  if(!root)return EINVAL;
  std::string full=root;
  if(!relative.empty()){full+='/';full+=relative;}
  while(full.size()>1 && full.back()=='/')full.pop_back();
  if(!indexed_root.empty() && (full==indexed_root || full.compare(0,indexed_root.size()+1,indexed_root+"/")==0)) {
    index_parent_=full==indexed_root?"":full.substr(indexed_root.size()+1);
    if(!index_parent_.empty()) {
      // Callers may supply root/relative; reject traversal before consulting metadata.
      std::string checked="/"+index_parent_+"/";
      if(checked.find("/../")!=std::string::npos || checked.find("/./")!=std::string::npos ||
          checked.find("//")!=std::string::npos || checked.find('\\')!=std::string::npos)return EINVAL;
    }
    const int fd=open_index(indexed_root,index_count_);
    if(fd<0)return errno;
    std::uint32_t low=0,high=index_count_;
    while(low<high) {
      const auto mid=low+(high-low)/2;std::string path;
      const int error=index_record(fd,mid,path);
      if(error){close(fd);return error;}
      if(parent_of(path)<index_parent_)low=mid+1;else high=mid;
    }
    index_fd_=fd;index_cursor_=low;return 0;
  }
#ifdef __PROSPERO__
  // Native fdopendir may require fcntl descriptor operations unavailable in a
  // sandboxed title. VFS resolution supplies an already validated absolute
  // directory root with an empty relative path; opendir owns its descriptor.
  // Indexed roots return above; native root validation uses stat, never lstat.
  if(root && relative.empty()) {
    struct stat st{};
    if(stat(root,&st)){const int error=errno;if(index_log)index_log("asset-index","function=stat root=%s errno=%d",root,error);return error;}
    if(!S_ISDIR(st.st_mode))return ENOTDIR;
    directory_=opendir(root);
    return directory_?0:errno;
  }
#endif
  const int fd = open_asset_directory(root, relative);
  if (fd < 0) return errno;
  directory_ = fdopendir(fd);
  if (!directory_) { const int error = errno; (void)close(fd); return error; }
  return 0;
}
int DirectoryPage::next(std::array<Entry, 32>& entries, std::size_t& count) {
  count = 0;
  if(index_fd_>=0) {
    while(count<entries.size() && index_cursor_<index_count_) {
      std::string path;const int error=index_record(index_fd_,index_cursor_,path,&entries[count].bytes,&entries[count].type);
      if(error)return error;
      if(parent_of(path)!=index_parent_)break;
      ++index_cursor_;
      const auto slash=path.rfind('/');const auto name=path.substr(slash==std::string::npos?0:slash+1);
      std::memcpy(entries[count++].name,name.c_str(),name.size()+1);
    }
    return 0;
  }
  if (!directory_) return EBADF;
  while (count < entries.size()) {
    errno = 0;
    const auto* entry = readdir(directory_);
    if (!entry) return errno;
    if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
    const auto length = std::strlen(entry->d_name);
    if (length >= sizeof(entries[count].name)) return ENAMETOOLONG;
    // A listing confers no authority to open: open_asset revalidates each path.
    std::memcpy(entries[count].name, entry->d_name, length + 1);
    ++count;
  }
  return 0;
}
Catalog::~Catalog() { if (fd_ >= 0) (void)close(fd_); }
int Catalog::open(const char* root, std::string_view path) {
  if (fd_ >= 0) return EBUSY;
  const int fd = open_asset(root, path);
  if (fd < 0) return errno;
  unsigned char header[64]{};
  struct stat info{};
  int error = read_at(fd, header, sizeof(header), 0);
  if (!error && fstat(fd, &info)) error = errno;
  const auto count = le32(header + 8);
  const auto index = le64(header + 24);
  if (!error && (std::memcmp(header, "AV2CAT01", 8) || le32(header + 12) != 512 ||
      le64(header + 16) != 64 || count > 1000000 ||
      index != 64 + std::uint64_t(count) * 512 || info.st_size < 0 ||
      static_cast<std::uint64_t>(info.st_size) != index + std::uint64_t(count) * 40))
    error = EINVAL;
  if (error) { (void)close(fd); return error; }
  fd_ = fd; count_ = count; index_offset_ = index;
  return 0;
}
int Catalog::row(std::uint32_t index, CatalogItem& result) const {
  if (fd_ < 0) return EBADF;
  if (index >= count_) return EINVAL;
  unsigned char bytes[512];
  const int error = read_at(fd_, bytes, sizeof(bytes), 64 + std::uint64_t(index) * 512);
  if (error) return error;
  if (!guid_text({reinterpret_cast<const char*>(bytes), 36}) ||
      !std::memchr(bytes + 48, 0, 464) || !bytes[44] || bytes[44] > 3) return EINVAL;
  result = {};
  std::memcpy(result.guid, bytes, 36);
  result.categories = le32(bytes + 40); result.bodies = bytes[44];
  std::memcpy(result.name, bytes + 48, sizeof(result.name));
  return 0;
}
int Catalog::page(std::uint32_t& cursor, std::uint32_t categories, std::uint8_t bodies,
                  std::array<CatalogItem, 32>& result, std::size_t& count) const {
  count = 0;
  if (fd_ < 0) return EBADF;
  if (cursor > count_ || bodies > 3) return EINVAL;
  while (cursor < count_ && count < result.size()) {
    CatalogItem item;
    const int error = row(cursor, item);
    if (error) return error;
    ++cursor;
    if (categories && !(item.categories & categories)) continue;
    if (bodies && !(item.bodies & bodies)) continue;
    result[count++] = item;
  }
  return 0;
}
int Catalog::find(std::string_view guid, CatalogItem& result) const {
  if (fd_ < 0) return EBADF;
  char key[36];
  if (guid.size() != sizeof(key)) return EINVAL;
  for (std::size_t i = 0; i < sizeof(key); ++i)
    key[i] = guid[i] >= 'A' && guid[i] <= 'F' ? char(guid[i] + ('a' - 'A')) : guid[i];
  if (!guid_text({key, sizeof(key)})) return EINVAL;
  std::uint32_t low = 0, high = count_;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    unsigned char entry[40];
    const int error = read_at(fd_, entry, sizeof(entry), index_offset_ + std::uint64_t(mid) * 40);
    if (error) return error;
    const int comparison = std::memcmp(key, entry, sizeof(key));
    if (comparison < 0) high = mid;
    else if (comparison > 0) low = mid + 1;
    else {
      const int status = row(le32(entry + 36), result);
      return status ? status : (std::memcmp(key, result.guid, sizeof(key)) ? EINVAL : 0);
    }
  }
  return ENOENT;
}
} // namespace av2::platform
