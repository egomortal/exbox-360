// AV2: streaming directory metadata; each XFile owns its cursor and result.
#include "paged_vfs.h"
namespace rex::filesystem {
class Av2DirectoryCursor final : public DirectoryCursor {
 public:
  explicit Av2DirectoryCursor(HostPathEntry* parent) : parent_(parent) {
    failed_ = page_.open(parent_->host_path().c_str(), "") != 0;
  }
  Entry* Next(const WildcardEngine& engine) override {
    if (failed_) return nullptr;
    current_.reset();
    for (;;) {
      if (position_ == count_) {
        if (page_.next(batch_, count_) != 0) { failed_=true; return nullptr; }
        position_=0;
        if (!count_) return nullptr;
      }
      const std::string name=batch_[position_++].name;
      if (!engine.Match(name)) continue;
      const auto path=parent_->host_path()/rex::to_path(name);
      std::error_code error;
#if defined(__PROSPERO__) || defined(AV2_NATIVE_INDEX_TEST)
      // The indexed PC tree excluded symlinks. Native lstat is refused;
      // stat verifies availability/type, not later console symlink changes.
      auto status=std::filesystem::status(path,error);
      if(error)continue;
#else
      auto status=std::filesystem::symlink_status(path,error);
      if (error || std::filesystem::is_symlink(status)) continue;
#endif
      FileInfo info;
      if (!GetInfo(path,&info)) continue;
#if defined(__PROSPERO__) || defined(AV2_NATIVE_INDEX_TEST)
      if (batch_[position_-1].type &&
          (batch_[position_-1].type==2)!=std::filesystem::is_directory(status))continue;
#endif
      current_.reset(HostPathEntry::Create(parent_->device(),parent_,path,info));
      return current_.get();
    }
  }
 private:
  HostPathEntry* parent_;
  av2::platform::DirectoryPage page_;
  std::array<av2::platform::DirectoryPage::Entry,32> batch_{};
  size_t position_=0, count_=0;
  bool failed_=false;
  std::unique_ptr<Entry> current_;
};
std::unique_ptr<DirectoryCursor> HostPathEntry::OpenDirectoryCursor() {
  return std::make_unique<Av2DirectoryCursor>(this);
}
} // namespace rex::filesystem
