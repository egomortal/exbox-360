#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
namespace av2::native {
// The original manifest and already-rendered PNG payloads remain byte-exact.
// No NXAK conversion, palette quantization, resizing or recompression occurs.
class AvatarSaveStore {
 public:
  explicit AvatarSaveStore(std::filesystem::path folder);
  bool manifest(const void* bytes,std::size_t size,bool explicit_save);
  bool image(const char* name,const void* bytes,std::size_t size);
  bool finalize();
  bool saved() const;
 private:
  struct Picture {std::uint32_t width=0,height=0;bool written=false;};
  std::filesystem::path folder_;
  mutable std::mutex mutex_;
  bool saved_=false,failed_=false;
  Picture fullbody_,gamerpic_;
};
void configure_avatar_save_store(const std::filesystem::path&);
bool persist_avatar_manifest(const void*,std::size_t,bool explicit_save);
bool persist_avatar_image(const char*,const void*,std::size_t);
bool finalize_avatar_save();
bool avatar_was_saved();
}
