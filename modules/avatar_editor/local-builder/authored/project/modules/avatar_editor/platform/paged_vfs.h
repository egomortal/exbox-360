#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <dirent.h>
#include <string_view>
#include <string>

namespace av2::platform {
// Configure once before guest threads start. Empty root disables indexing.
// Only paths inside this exact asset root use the index; saves stay live.
int configure_asset_index(std::string root);
using AssetIndexLog = void(*)(const char*,const char*,...);
void set_asset_index_logger(AssetIndexLog logger);
// Metadata from the locally generated, symlink-free index. ENOTSUP means the
// path is outside its root. It does not attest to later console tree changes.
int indexed_asset_info(std::string_view full_path, std::uint64_t& bytes, std::uint8_t& type);
// One open directory cursor, no recursive tree and no retained child entries.
class DirectoryPage {
 public:
  struct Entry { char name[256]{}; std::uint64_t bytes=0; std::uint8_t type=0; }; // type 0=live/unknown, 1=file, 2=directory
  ~DirectoryPage();
  DirectoryPage() = default;
  DirectoryPage(const DirectoryPage&) = delete;
  DirectoryPage& operator=(const DirectoryPage&) = delete;
  int open(const char* root, std::string_view relative);
  int next(std::array<Entry, 32>& entries, std::size_t& count);
 private:
  DIR* directory_ = nullptr;
  int index_fd_ = -1;
  std::uint32_t index_count_ = 0, index_cursor_ = 0;
  std::string index_parent_;
};

struct CatalogItem {
  char guid[37]{};
  std::uint32_t categories = 0;
  std::uint8_t bodies = 0;
  char name[464]{}; // UTF-8, same bytes/order as AER Closet::Load.
};
class Catalog {
 public:
  Catalog() = default;
  ~Catalog();
  Catalog(const Catalog&) = delete;
  Catalog& operator=(const Catalog&) = delete;
  int open(const char* asset_root, std::string_view index_path);
  // cursor counts disk rows, including rows rejected by the filters. Zero masks
  // select all. Caller owns at most 32 records; EOF is count=0, cursor=size().
  int page(std::uint32_t& cursor, std::uint32_t categories, std::uint8_t bodies,
           std::array<CatalogItem, 32>& result, std::size_t& count) const;
  int find(std::string_view guid, CatalogItem& result) const;
  std::uint32_t size() const { return count_; }
 private:
  int row(std::uint32_t index, CatalogItem& result) const;
  int fd_ = -1;
  std::uint32_t count_ = 0;
  std::uint64_t index_offset_ = 0;
};
} // namespace av2::platform
