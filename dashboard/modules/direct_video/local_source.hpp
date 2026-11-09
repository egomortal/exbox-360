// Local-file adapter for Omni's established demux/audio path. No browser or network.
#pragma once
#include "omni/OmniVideoDecoder.h"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace exbox::video {
// The session owns one descriptor. pread avoids shared seek state and whole-file copies.
class LocalSource final : public OmniMedia::ByteSource {
    int fd_ = -1;
    int64_t size_ = 0;
    explicit LocalSource(int fd, int64_t size) : fd_(fd), size_(size) {}
public:
    ~LocalSource() override { if (fd_ >= 0) ::close(fd_); }
    LocalSource(const LocalSource&) = delete;
    LocalSource& operator=(const LocalSource&) = delete;
    static std::shared_ptr<LocalSource> open(const std::string& path) {
        if (path.empty() || path.find("://") != std::string::npos || path.find('\0') != std::string::npos) return {};
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) return {};
        struct stat st{};
        if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) { ::close(fd); return {}; }
        return std::shared_ptr<LocalSource>(new LocalSource(fd, st.st_size));
    }
    int64_t size() const override { return size_; }
    int read(int64_t offset, uint8_t* buffer, int length, double& waited) override {
        waited = 0;
        if (offset < 0 || offset > size_ || length <= 0 || !buffer) return -1;
        const size_t count = size_t(std::min<int64_t>(length, size_ - offset));
        ssize_t result;
        do { result = pread(fd_, buffer, count, offset); } while (result < 0 && errno == EINTR);
        return result < 0 ? -1 : int(result);
    }
};
}
