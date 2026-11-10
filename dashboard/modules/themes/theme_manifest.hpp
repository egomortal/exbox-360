// SPDX-License-Identifier: MIT
#pragma once

#include "theme_catalog.hpp"

#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace exbox::themes {

struct ManifestResult {
    std::vector<CatalogEntry> entries;
    std::size_t rejected{};
    std::string error;
    explicit operator bool() const { return error.empty(); }
};

inline bool lowercase_sha256(const std::string& value) {
    if (value.size() != 64) return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

inline bool display_name(const std::string& value) {
    if (value.empty() || value.size() > 160) return false;
    for (unsigned char c : value) if (c < 32 || c == 127) return false;
    return true;
}

inline bool stable_id(const std::string& value) {
    if (value.size() != 22 || value.compare(0, 6, "theme-") != 0) return false;
    for (std::size_t i = 6; i < value.size(); ++i) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

inline EncodedImage encoding(const std::string& value) {
    if (value == "png") return EncodedImage::png;
    if (value == "jpeg") return EncodedImage::jpeg;
    return EncodedImage::unsupported;
}

inline ManifestResult parse_manifest(const std::string& text,
                                     std::size_t max_entries = 4096,
                                     std::size_t max_assets = 20000) {
    struct Pending {
        CatalogEntry entry;
        bool declared_complete{};
        std::vector<Asset> identity_assets;
    };
    ManifestResult result;
    if (text.size() > (16u << 20)) { result.error = "manifest too large"; return result; }
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line) || line != "EXBOX-THEME-CATALOG 1") {
        result.error = "unsupported manifest header"; return result;
    }
    std::map<std::string, Pending> themes;
    std::size_t assets = 0, line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        std::string kind;
        row >> kind;
        if (kind == "THEME") {
            std::string id, name, directory, tail; int complete = -1;
            if (!(row >> std::quoted(id) >> std::quoted(name) >> std::quoted(directory) >> complete) || (row >> tail) ||
                !stable_id(id) || !display_name(name) || !safe_relative_path(directory) || (complete != 0 && complete != 1) ||
                themes.size() >= max_entries || themes.count(id)) {
                result.error = "invalid theme declaration at line " + std::to_string(line_number); return result;
            }
            Pending pending; pending.entry.stable_id = id; pending.entry.display_name = name; pending.entry.relative_directory = directory;
            pending.declared_complete = complete != 0; themes.emplace(id, std::move(pending));
            continue;
        }
        if (kind == "ASSET") {
            std::string id, role, path, sha, type, tail; std::uint64_t bytes = 0;
            if (!(row >> std::quoted(id) >> std::quoted(role) >> std::quoted(path) >> bytes >> std::quoted(sha) >> type) ||
                (row >> tail) || assets >= max_assets || bytes == 0 || bytes > (64u << 20) ||
                !safe_relative_path(path) || !lowercase_sha256(sha) || encoding(type) == EncodedImage::unsupported) {
                result.error = "invalid asset at line " + std::to_string(line_number); return result;
            }
            const auto found = themes.find(id);
            if (found == themes.end()) { result.error = "asset before theme at line " + std::to_string(line_number); return result; }
            Asset asset{path, bytes, sha, encoding(type)};
            std::optional<Asset>* destination = nullptr;
            if (role == "thumbnail") destination = &found->second.entry.thumbnail;
            else if (role.size() == 10 && role.compare(0, 9, "wallpaper") == 0 && role[9] >= '1' && role[9] <= '4')
                destination = &found->second.entry.wallpapers[std::size_t(role[9] - '1')];
            if (!destination || destination->has_value()) {
                result.error = "duplicate or unknown role at line " + std::to_string(line_number); return result;
            }
            *destination = asset; found->second.identity_assets.push_back(std::move(asset)); ++assets;
            continue;
        }
        result.error = "unknown record at line " + std::to_string(line_number); return result;
    }
    result.entries.reserve(themes.size());
    for (auto& item : themes) {
        auto& pending = item.second;
        const bool complete = std::all_of(pending.entry.wallpapers.begin(), pending.entry.wallpapers.end(),
                                          [](const auto& asset) { return asset.has_value(); });
        pending.entry.complete = complete;
        if (complete != pending.declared_complete ||
            stable_identity(pending.entry.relative_directory, pending.identity_assets) != pending.entry.stable_id) {
            ++result.rejected; continue;
        }
        result.entries.push_back(std::move(pending.entry));
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const CatalogEntry& a, const CatalogEntry& b) {
        const auto an = normalized(a.display_name), bn = normalized(b.display_name);
        return an == bn ? a.stable_id < b.stable_id : an < bn;
    });
    return result;
}

} // namespace exbox::themes
