// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <png.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace exbox_games::art {
struct Pixels {
    unsigned width = 0, height = 0;
    std::vector<unsigned char> rgba;
};
using Image = std::shared_ptr<const Pixels>;
inline Image placeholder() {
    static Image image = [] {
        auto p = std::make_shared<Pixels>();
        p->width = 32; p->height = 48; p->rgba.resize(32*48*4);
        for (unsigned y=0; y<48; ++y) for (unsigned x=0; x<32; ++x) {
            auto i = (y*32+x)*4;
            const unsigned char shade = (x==0 || y==0 || x==31 || y==47) ? 100 : 45;
            p->rgba[i]=shade; p->rgba[i+1]=shade; p->rgba[i+2]=shade; p->rgba[i+3]=255;
        }
        return p;
    }();
    return image;
}
enum class Status { Placeholder, Ready, Missing, Rejected, Invalid, TooLarge };
struct Result { Image pixels = placeholder(); Status status = Status::Placeholder; };
// Caller supplies budgets from its admitted worker/staging allocation.
struct Limits { std::size_t encoded_bytes, decoded_bytes; unsigned dimension; };
struct Request {
    std::uint64_t generation;
    std::string game_key;
    std::filesystem::path root, relative;
};
inline bool below(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto r=root.begin(), p=path.begin();
    for (; r!=root.end(); ++r,++p) if (p==path.end() || *r!=*p) return false;
    return p!=path.end();
}
// Execute on an application-owned asset worker, never the render/input thread.
inline Result load(const Request& request, Limits limits) noexcept {
    try {
        if (request.root.empty() || request.relative.empty()) return {};
        if (request.relative.is_absolute() || request.relative.has_root_name()) return {placeholder(),Status::Rejected};
        const auto text=request.relative.generic_string();
        if (text.find('\0')!=std::string::npos || text.find('\\')!=std::string::npos || text.find(':')!=std::string::npos)
            return {placeholder(),Status::Rejected};
        for (const auto& part : request.relative)
            if (part==".." || part==".") return {placeholder(),Status::Rejected};
        if (request.relative.extension()!=".png" && request.relative.extension()!=".PNG")
            return {placeholder(),Status::Rejected};
        std::error_code ec;
        const auto root=std::filesystem::canonical(request.root,ec);
        if (ec) return {placeholder(),Status::Missing};
        const auto path=std::filesystem::canonical(root/request.relative,ec);
        if (ec) return {placeholder(),Status::Missing};
        if (!below(root,path) || !std::filesystem::is_regular_file(path)) return {placeholder(),Status::Rejected};
        const auto size=std::filesystem::file_size(path);
        if (size>limits.encoded_bytes || size>std::size_t(PTRDIFF_MAX)) return {placeholder(),Status::TooLarge};
        std::ifstream stream(path,std::ios::binary);
        if (!stream) return {placeholder(),Status::Missing};
        std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
        if (!stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size)) || stream.peek()!=EOF)
            return {placeholder(),Status::Invalid};
        png_image image{}; image.version=PNG_IMAGE_VERSION;
        struct Cleanup { png_image* image; ~Cleanup(){png_image_free(image);} } cleanup{&image};
        if (!png_image_begin_read_from_memory(&image,bytes.data(),bytes.size())) return {placeholder(),Status::Invalid};
        if (!image.width || !image.height || image.width>limits.dimension || image.height>limits.dimension ||
            std::uint64_t(image.width)*image.height*4>limits.decoded_bytes)
            return {placeholder(),Status::TooLarge};
        image.format=PNG_FORMAT_RGBA;
        auto pixels=std::make_shared<Pixels>();
        pixels->width=image.width; pixels->height=image.height;
        pixels->rgba.resize(static_cast<std::size_t>(image.width)*image.height*4);
        if (!png_image_finish_read(&image,nullptr,pixels->rgba.data(),0,nullptr)) return {placeholder(),Status::Invalid};
        return {pixels,Status::Ready};
    } catch (...) { return {placeholder(),Status::Invalid}; }
}
// UI-thread owned. Requests/results cross a worker queue by value.
class Slot {
    std::uint64_t generation_ = 0;
    std::string game_key_;
public:
    Result current;
    Request begin(std::string key, std::filesystem::path root, std::filesystem::path relative) {
        ++generation_; game_key_=std::move(key); current={};
        return {generation_,game_key_,std::move(root),std::move(relative)};
    }
    bool complete(const Request& request, Result result) {
        if (request.generation!=generation_ || request.game_key!=game_key_) return false;
        current=std::move(result); return true;
    }
};
struct Catalog {
    std::filesystem::path root;
    std::map<std::string,std::filesystem::path> bindings;
    Request request(Slot& slot,const std::string& game_key,const std::string& art_key) const {
        const auto found=bindings.find(art_key);
        return slot.begin(game_key,root,found==bindings.end() ? std::filesystem::path{} : found->second);
    }
};
} // namespace exbox_games::art
