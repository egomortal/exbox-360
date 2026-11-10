#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace exbox::themes {

enum class EncodedImage { unsupported, png, jpeg };

struct Asset {
    std::string relative_path;
    std::uint64_t bytes{};
    std::string content_sha256;
    EncodedImage encoding{EncodedImage::unsupported};
};

struct CatalogEntry {
    std::string stable_id;
    std::string display_name;
    std::string relative_directory;
    std::optional<Asset> thumbnail;
    std::array<std::optional<Asset>, 4> wallpapers;
    bool complete{};
};

inline char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

inline std::string normalized(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool slash = false;
    for (char c : s) {
        if (c == '\\' || c == '/') {
            if (!slash && !out.empty()) out.push_back('/');
            slash = true;
            continue;
        }
        slash = false;
        out.push_back(ascii_lower(c));
    }
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

inline bool safe_relative_path(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\') return false;
    if (path.size() >= 2 && path[1] == ':') return false;
    const std::string p = normalized(path);
    std::size_t begin = 0;
    while (begin <= p.size()) {
        const std::size_t end = p.find('/', begin);
        const std::string_view part(p.data() + begin,
            (end == std::string::npos ? p.size() : end) - begin);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}

inline EncodedImage sniff(const std::vector<std::uint8_t>& prefix) {
    static constexpr std::array<std::uint8_t, 8> png{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    if (prefix.size() >= png.size()) {
        bool match = true;
        for (std::size_t i = 0; i < png.size(); ++i) match &= prefix[i] == png[i];
        if (match) return EncodedImage::png;
    }
    if (prefix.size() >= 3 && prefix[0] == 0xff && prefix[1] == 0xd8 && prefix[2] == 0xff)
        return EncodedImage::jpeg;
    return EncodedImage::unsupported;
}

inline std::optional<unsigned> wallpaper_slot(std::string_view filename) {
    const std::string n = normalized(filename);
    for (unsigned i = 1; i <= 4; ++i) {
        if (n == "wallpaper" + std::to_string(i) + ".png") return i - 1;
    }
    return std::nullopt;
}

inline bool thumbnail_name(std::string_view filename) {
    return normalized(filename) == "thumbnail.png";
}

inline std::string stable_identity(std::string_view relative_directory,
                                   const std::vector<Asset>& assets) {
    // Deterministic catalog key. Production import supplies SHA-256 for every asset;
    // this helper deliberately never keys residency on addresses or catalog indices.
    std::uint64_t h = 1469598103934665603ull;
    auto add = [&](std::string_view text) {
        for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
        h ^= 0xff; h *= 1099511628211ull;
    };
    add(normalized(relative_directory));
    std::vector<std::string> records;
    records.reserve(assets.size());
    for (const Asset& a : assets) {
        records.push_back(normalized(a.relative_path) + "\n" +
                          std::to_string(a.bytes) + "\n" + a.content_sha256);
    }
    std::sort(records.begin(), records.end());
    for (const std::string& record : records) add(record);
    static constexpr char hex[] = "0123456789abcdef";
    std::string out = "theme-";
    for (int shift = 60; shift >= 0; shift -= 4) out.push_back(hex[(h >> shift) & 15]);
    return out;
}

} // namespace exbox::themes

