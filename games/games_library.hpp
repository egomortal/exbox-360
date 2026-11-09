// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace exbox_games {
inline constexpr const char* player_title = "PPSA50011";
inline constexpr const char* default_root = "/data/homebrew/PPSA50011/assets/roms";
enum class Availability { Unknown, Available, Unavailable };
enum class Platform { Xbox360, PS5 };
struct Source {
    std::string id = "ps5x360-default";
    std::string root = default_root;
    Availability availability = Availability::Unknown;
};
// Supplied inventory only. No directory scanning or game-content access.
struct Folder {
    std::string name;
    std::string display_name;
    std::string owner_art_key;
};
struct Entry {
    std::string key, source_id, folder, title, owner_art_key;
    Availability availability = Availability::Unknown;
    Platform platform = Platform::Xbox360;
    std::string launch_title = player_title;
};
struct Library {
    Source source;
    std::vector<Entry> entries;
    std::size_t rejected = 0;
};
inline bool component(const std::string& value) {
    if (value.empty() || value == "." || value == "..") return false;
    for (unsigned char c : value)
        if (c < 32 || c == 127 || c == '/' || c == '\\') return false;
    return true;
}
inline std::string key_part(const std::string& value) {
    return std::to_string(value.size()) + ":" + value;
}
inline std::string identity(const std::string& source, const std::string& folder) {
    return "game:" + key_part(source) + key_part(folder);
}
inline Library build_library(Source source, const std::vector<Folder>& folders) {
    if (!component(source.id)) throw std::invalid_argument("Invalid source ID");
    Library result{source, {}, 0};
    std::map<std::string, Entry> unique;
    for (const auto& folder : folders) {
        if (!component(folder.name)) { ++result.rejected; continue; }
        const auto key = identity(source.id, folder.name);
        // First supplied metadata wins for duplicate directory records.
        unique.emplace(key, Entry{key, source.id, folder.name,
            folder.display_name.empty() ? folder.name : folder.display_name,
            folder.owner_art_key, source.availability});
    }
    for (auto& pair : unique) result.entries.push_back(std::move(pair.second));
    std::sort(result.entries.begin(), result.entries.end(), [](const Entry& a, const Entry& b) {
        return a.title == b.title ? a.key < b.key : a.title < b.title;
    });
    return result;
}
struct InstalledTitle { std::string title_id, title, owner_art_key; };
inline bool ps5_title_id(const std::string& id) {
    if (id.size() != 9 || id.compare(0, 4, "PPSA") != 0) return false;
    return std::all_of(id.begin()+4, id.end(), [](char c) { return c >= '0' && c <= '9'; });
}
inline std::vector<Entry> installed_entries(const std::vector<InstalledTitle>& titles) {
    std::map<std::string, Entry> unique;
    for (const auto& title : titles) {
        if (!ps5_title_id(title.title_id) || title.title_id == player_title) continue;
        const auto key = "title:" + title.title_id;
        unique.emplace(key, Entry{key, "ps5-installed", "",
            title.title.empty() ? title.title_id : title.title, title.owner_art_key,
            Availability::Available, Platform::PS5, title.title_id});
    }
    std::vector<Entry> result;
    for (auto& pair : unique) result.push_back(std::move(pair.second));
    return result;
}
} // namespace exbox_games
