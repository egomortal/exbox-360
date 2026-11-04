// Ex-Box 360: reading the PS5's game library and profile cache.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps5_library.hpp"
#include "system_data.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <limits>

namespace ps5lib
{
namespace
{
// Reads a JSON string literal starting at the opening quote; handles the
// common escapes and drops \u sequences it cannot print.
std::string json_string_at(const std::string &s, std::size_t quote)
{
    std::string out;
    for (std::size_t i = quote + 1; i < s.size(); ++i)
    {
        char c = s[i];
        if (c == '"')
            break;
        if (c == '\\' && i + 1 < s.size())
        {
            char e = s[++i];
            if (e == 'u')
            {
                // Keep ASCII escapes, skip the rest.
                if (i + 4 < s.size())
                {
                    unsigned v = unsigned(std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16));
                    if (v >= 0x20 && v < 0x7f)
                        out += char(v);
                    i += 4;
                }
                continue;
            }
            c = e == 'n' ? ' ' : e == 't' ? ' ' : e;
        }
        out += c;
    }
    return out;
}

// The value of "key": "…" after `from`, or empty.
std::string string_field(const std::string &s, const std::string &key, std::size_t from = 0,
                         std::size_t *at = nullptr)
{
    const std::string needle = "\"" + key + "\"";
    std::size_t k = s.find(needle, from);
    if (k == std::string::npos)
        return {};
    std::size_t colon = s.find(':', k + needle.size());
    if (colon == std::string::npos)
        return {};
    std::size_t q = s.find_first_not_of(" \t\r\n", colon + 1);
    if (q == std::string::npos || s[q] != '"')
        return {};
    if (at)
        *at = q;
    return json_string_at(s, q);
}

int int_field(const std::string &s, const std::string &key)
{
    const std::string needle = "\"" + key + "\"";
    std::size_t k = s.find(needle);
    if (k == std::string::npos)
        return 0;
    std::size_t colon = s.find(':', k + needle.size());
    if (colon == std::string::npos)
        return 0;
    return std::atoi(s.c_str() + colon + 1);
}

bool exists(const std::string &path)
{
    struct stat st;
    return sysdata::stat(path, &st) == 0 && st.st_size > 0;
}

std::vector<unsigned char> read_bytes(const std::string &path, std::size_t limit)
{
    std::vector<unsigned char> out;
    if (sysdata::remote(path)) {const auto bytes=sysdata::read(path,limit);return {bytes.begin(),bytes.end()};}
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return out;
    unsigned char buf[4096];
    std::size_t n;
    while (out.size() < limit && (n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.insert(out.end(), buf, buf + std::min(n, limit - out.size()));
    std::fclose(f);
    return out;
}

std::string trim(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
    return s.substr(i);
}
} // namespace

bool title_id(const std::string &id)
{
    if (id.size() != 9)
        return false;
    for (int i = 0; i < 4; ++i)
        if (!std::isupper(static_cast<unsigned char>(id[std::size_t(i)])))
            return false;
    for (int i = 4; i < 9; ++i)
        if (!std::isdigit(static_cast<unsigned char>(id[std::size_t(i)])))
            return false;
    return true;
}

bool hidden_title(const std::string &id)
{
    return id.rfind("NPXS", 0) == 0 || id.rfind("FAKE", 0) == 0 || id.rfind("LAPY", 0) == 0 ||
           id == "PPSA99360";
}

std::string json_title(const std::string &json)
{
    // Prefer the default language's block when the file names one.
    std::size_t start = 0;
    const std::string lang = string_field(json, "defaultLanguage");
    if (!lang.empty())
    {
        std::size_t at = json.find("\"" + lang + "\"");
        // Skip the defaultLanguage value itself.
        std::size_t def = json.find("\"defaultLanguage\"");
        if (at != std::string::npos && def != std::string::npos && at < def + 40)
            at = json.find("\"" + lang + "\"", at + 1);
        if (at != std::string::npos)
            start = at;
    }
    std::string t = string_field(json, "titleName", start);
    if (t.empty())
        t = string_field(json, "titleName");
    return trim(t);
}

std::string sfo_title(const std::vector<unsigned char> &d)
{
    auto rd32 = [&](std::size_t o) -> unsigned {
        return o + 4 <= d.size() ? unsigned(d[o]) | unsigned(d[o + 1]) << 8 | unsigned(d[o + 2]) << 16 |
                                       unsigned(d[o + 3]) << 24
                                 : 0u;
    };
    if (d.size() < 20 || rd32(0) != 0x46535000u)
        return {};
    const unsigned keys = rd32(8), data = rd32(12), n = std::min(rd32(16), 256u);
    for (unsigned i = 0; i < n; ++i)
    {
        const std::size_t e = 20 + std::size_t(i) * 16;
        if (e + 16 > d.size())
            break;
        const unsigned koff = unsigned(d[e]) | unsigned(d[e + 1]) << 8;
        const unsigned len = rd32(e + 4), doff = rd32(e + 12);
        const std::size_t k = std::size_t(keys) + koff;
        if (k + 6 > d.size() || std::memcmp(&d[k], "TITLE\0", 6) != 0)
            continue;
        const std::size_t v = std::size_t(data) + doff;
        if (v >= d.size())
            return {};
        std::string out(reinterpret_cast<const char *>(&d[v]), std::min<std::size_t>(len, d.size() - v));
        out = out.substr(0, out.find('\0'));
        return trim(out);
    }
    return {};
}

std::vector<dash::App> installed_games(const std::string &prefix)
{
    std::vector<dash::App> out;
    const char *roots[] = {"/user/appmeta", "/system_data/priv/appmeta"};
    bool bulk_loaded=false;
    if(prefix.empty()&&sysdata::companion){
        std::vector<sysdata::AppRecord> records;
        bulk_loaded=sysdata::apps(records);
        if(bulk_loaded)for(const auto& record:records){
            if(!title_id(record.id)||hidden_title(record.id))continue;
            if(std::any_of(out.begin(),out.end(),[&](const dash::App& app){return app.title_id==record.id;}))continue;
            dash::App game;game.title_id=record.id;game.title=record.name.empty()?record.id:record.name;
            for(const auto* root:roots){const auto icon=std::string(root)+"/"+record.id+"/icon0.png";if(exists(icon)){game.icon=game.playstation_icon=icon;break;}}
            out.push_back(std::move(game));
        }
    }
    // Older feed implementations can still use the original metadata walk.
    if(!bulk_loaded)
    for (const char *root : roots)
    {
        const std::string dir = prefix + root;
        for (const auto &entry : sysdata::list(dir))
        {
            const std::string id = entry.name;
            if (!title_id(id) || hidden_title(id))
                continue;
            const std::string base = dir + "/" + id;
            std::string name = json_title(read_file(base + "/param.json"));
            if (name.empty())
                name = sfo_title(read_bytes(base + "/param.sfo", 64 * 1024));
            auto existing = std::find_if(out.begin(), out.end(), [&](const dash::App &a) { return a.title_id == id; });
            if (existing != out.end())
            {
                if (existing->title == id && !name.empty())
                    existing->title = name;
                if (existing->icon.empty() && exists(base + "/icon0.png"))
                    existing->icon = existing->playstation_icon = base + "/icon0.png";
                continue;
            }
            if (out.size() >= 512)
                continue;
            if (name.empty())
                name = id;
            dash::App game;
            game.title = name;
            game.title_id = id;
            if (exists(base + "/icon0.png"))
                game.icon = game.playstation_icon = base + "/icon0.png";
            out.push_back(game);
        }
    }
    for (auto &game : out)
    {
        const std::string art = prefix + "/data/homebrew/ExBox360/game_art/" + game.title_id;
        if (exists(art + ".jpg"))
            game.icon = game.xbox_cover = art + ".jpg";
        else if (exists(art + ".png"))
            game.icon = game.xbox_cover = art + ".png";
    }
    std::sort(out.begin(), out.end(), [](const dash::App &a, const dash::App &b) {
        std::string x = a.title, y = b.title;
        for (auto &c : x)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        for (auto &c : y)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return x != y ? x < y : a.title_id < b.title_id;
    });
    return out;
}

Trophies parse_trophies(const std::string &online)
{
    Trophies t;
    // trophySummary is a JSON document stored as a string: unescape it first.
    std::size_t at = 0;
    const std::string inner = string_field(online, "trophySummary", 0, &at);
    const std::string &doc = inner.empty() ? online : inner;
    if (doc.find("earnedTrophies") == std::string::npos)
        return t;
    t.known = true;
    t.level = int_field(doc, "level");
    t.platinum = int_field(doc, "platinum");
    t.gold = int_field(doc, "gold");
    t.silver = int_field(doc, "silver");
    t.bronze = int_field(doc, "bronze");
    return t;
}

namespace
{
std::string lower(std::string s)
{
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool ends_with(const std::string &s, const char *tail)
{
    const std::size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}
void walk_captures(const std::string &dir, int depth, std::vector<std::string> &out, std::size_t limit)
{
    if (depth > 5 || out.size() >= limit)
        return;
    std::vector<std::string> names;
    for (const auto &entry : sysdata::list(dir))
        if (!entry.name.empty() && entry.name[0] != '.') names.push_back(entry.name);
    for (const auto &name : names)
    {
        if (out.size() >= limit)
            break;
        const std::string path = dir + "/" + name;
        struct stat st;
        if (sysdata::stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            walk_captures(path, depth + 1, out, limit);
        else if (S_ISREG(st.st_mode) && st.st_size > 0)
        {
            const std::string l = lower(name);
            if (ends_with(l, ".jpg") || ends_with(l, ".jpeg") || ends_with(l, ".png") || ends_with(l, ".jxr"))
                out.push_back(path);
        }
    }
}
std::string base_name(const std::string &path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
} // namespace

bool viewable_picture(const std::string &path)
{
    const std::string l = lower(path);
    return ends_with(l, ".jpg") || ends_with(l, ".jpeg") || ends_with(l, ".png");
}

std::vector<std::string> find_captures(const std::string &prefix, std::size_t limit)
{
    std::vector<std::string> out;
    walk_captures(prefix + "/user/av_contents/photo", 0, out, limit);
    // "20261017_200850_00357409.jpg": the name sorts by when it was taken.
    std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
        const std::string x = base_name(a), y = base_name(b);
        return x != y ? x > y : a > b;
    });
    return out;
}

dash::StorageInventory storage_inventory(const std::vector<dash::App> &games,
    const std::vector<std::string> &music_roots, const std::string &prefix, const std::string &trailers)
{
    dash::StorageInventory out;
    std::map<dev_t,int> volumes;
    // /user: installed_games/find_captures; /data: Ex-Box's data. External roots
    // are the mounted volumes already used by music::default_roots(). No guessed
    // /mnt paths for game payloads; appmeta does not identify the install volume.
    const char *roots[]={"/user","/data","/mnt/usb0","/mnt/usb1","/mnt/ext0","/mnt/ext1"};
    auto byte_count=[](unsigned long long count,unsigned long long unit) -> long long {
        const auto max=static_cast<unsigned long long>(std::numeric_limits<long long>::max());
        return unit && count<=max/unit ? static_cast<long long>(count*unit) : -1;
    };
    for (const char *root_path:roots) {
        const std::string path=prefix+root_path;
        struct stat st{};
        if (sysdata::lstat(path,&st)!=0 || !S_ISDIR(st.st_mode)) continue;
        const bool external=std::string(root_path).rfind("/mnt/",0)==0;
        // An empty mount-point directory on the parent filesystem is not a device.
        struct stat parent{};
        if (external && sysdata::stat(prefix+"/mnt",&parent)==0 && parent.st_dev==st.st_dev) continue;
        if (volumes.count(st.st_dev)) continue;
        dash::StorageDevice device;
        // Ex-Box addition: no 360 original. These are actual PS5 mount paths,
        // not Xbox hard-drive model names or manufactured capacity claims.
        device.name=std::string(external?"Extended Storage":"Internal Storage")+" ("+root_path+")";
        device.path=root_path;device.external=external;
        struct statvfs info{};
        if (sysdata::statvfs(path,&info)==0) {
            device.capacity=byte_count(info.f_blocks,info.f_frsize);
            device.free=byte_count(info.f_bavail,info.f_frsize);
        }
        volumes[st.st_dev]=int(out.devices.size());out.devices.push_back(device);
    }
    for (const auto &game:games) {
        dash::StorageItem item;item.name=game.title;item.category=dash::StorageGames;
        out.items.push_back(item); // no metadata file size presented as a game's size
    }
    std::set<std::string> seen;
    constexpr size_t limit=4096;
    auto add_file=[&](const std::string &path,int category) {
        if (!seen.insert(path).second) return;
        struct stat st{};
        if (sysdata::lstat(path,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<0) return;
        if (out.items.size()>=limit) {out.limited=true;return;}
        dash::StorageItem item;item.name=base_name(path);item.category=category;item.bytes=st.st_size;
        item.path=!prefix.empty() && path.rfind(prefix+"/",0)==0?path.substr(prefix.size()):path;
        const auto device=volumes.find(st.st_dev);
        if (device!=volumes.end()) item.device=device->second;
        if (category==dash::StoragePictures) {
            // PS5 screenshot filenames encode capture time. Do not reuse mtime
            // for other files: it says when copied/changed, not when created.
            const std::string stamp=base_name(path);
            if (stamp.size()>=15 && stamp[8]=='_' &&
                std::all_of(stamp.begin(),stamp.begin()+8,[](char c){return c>='0' && c<='9';}) &&
                std::all_of(stamp.begin()+9,stamp.begin()+15,[](char c){return c>='0' && c<='9';}))
                item.created=stamp.substr(0,4)+"-"+stamp.substr(4,2)+"-"+stamp.substr(6,2)+" "+
                    stamp.substr(9,2)+":"+stamp.substr(11,2)+":"+stamp.substr(13,2);
        }
        out.items.push_back(item);
    };
    // Traverse a bounded, explicit set of Ex-Box data roots, skipping symlinks.
    // The walker never opens game payloads, captures, music, or avatar contents.
    size_t visited=0;
    auto walk=[&](auto &&self,const std::string &dir,int category,const std::vector<std::string> &extensions,int depth) -> void {
        if (depth>8 || visited>=16384 || out.items.size()>=limit) {out.limited=true;return;}
        struct stat st{};
        if (sysdata::lstat(dir,&st)!=0 || !S_ISDIR(st.st_mode)) return;
        for (const auto &entry:sysdata::list(dir)) {
            if (entry.name.empty() || entry.name[0]=='.') continue;
            if (++visited>16384 || out.items.size()>=limit) {out.limited=true;break;}
            const std::string path=dir+"/"+entry.name;
            if (sysdata::lstat(path,&st)!=0) continue;
            if (S_ISDIR(st.st_mode)) self(self,path,category,extensions,depth+1);
            else if (S_ISREG(st.st_mode)) {
                const std::string name=lower(entry.name);
                const bool match=extensions.empty() || std::any_of(extensions.begin(),extensions.end(),
                    [&](const std::string &extension){return ends_with(name,extension.c_str());});
                if (match) add_file(path,category);
            }
        }
    };
    // Same capture root/extensions as find_captures, without the viewer's 500 limit.
    walk(walk,prefix+"/user/av_contents/photo",dash::StoragePictures,{".jpg",".jpeg",".png",".jxr"},0);
    for (const auto &path:music_roots)
        walk(walk,prefix+path,dash::StorageMusic,{".mp3",".flac",".ogg",".oga",".wav"},0);
    // The existing video viewer reads packed trailers, not native PS5 videos.
    const std::string video_root=trailers.rfind("/data/",0)==0?prefix+trailers:trailers;
    walk(walk,video_root,dash::StorageVideos,{".frames",".wav"},0);
    const std::string app=prefix+"/data/homebrew/ExBox360/";
    walk(walk,app+"private_art/avatar",dash::StorageAvatars,{".nxak",".frames",".png"},0);
    walk(walk,app+"avatar",dash::StorageAvatars,{".nxak"},0);
    walk(walk,app+"era-skins",dash::StorageThemes,{".png",".jpg",".jpeg"},0);
    // No real source for Xbox profiles, demos or system items: keep them empty.
    std::sort(out.items.begin(),out.items.end(),[](const dash::StorageItem &a,const dash::StorageItem &b) {
        return a.name!=b.name?a.name<b.name:a.path<b.path;
    });
    return out;
}

std::string profile_dir(unsigned user, const std::string &prefix)
{
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%08X", user);
    return prefix + "/system_data/priv/cache/profile/" + hex;
}

Trophies read_trophies(unsigned user, const std::string &prefix)
{
    return parse_trophies(read_file(profile_dir(user, prefix) + "/online.json"));
}

std::string read_file(const std::string &path, std::size_t limit)
{
    const std::vector<unsigned char> b = read_bytes(path, limit);
    return std::string(b.begin(), b.end());
}

Unlock unlocked(const Trophies &before, const Trophies &after)
{
    Unlock u;
    if (!before.known || !after.known)
        return u;
    const int plat = after.platinum - before.platinum, gold = after.gold - before.gold,
              silver = after.silver - before.silver, bronze = after.bronze - before.bronze;
    const int earned = std::max(0, plat) + std::max(0, gold) + std::max(0, silver) + std::max(0, bronze);
    if (earned <= 0)
        return u;
    const int points = std::max(0, plat) * 300 + std::max(0, gold) * 90 + std::max(0, silver) * 30 +
                       std::max(0, bronze) * 15;
    u.title = earned == 1 ? "Achievement unlocked" : std::to_string(earned) + " achievements unlocked";
    if (earned == 1)
        u.text = std::string(plat > 0 ? "Platinum" : gold > 0 ? "Gold" : silver > 0 ? "Silver" : "Bronze") +
                 " trophy - " + std::to_string(points) + " points";
    else
        u.text = std::to_string(points) + " points";
    if (after.level > before.level)
        u.text += " - level " + std::to_string(after.level);
    return u;
}

std::string save_trophies(const Trophies &t)
{
    if (!t.known)
        return "";
    return std::to_string(t.platinum) + " " + std::to_string(t.gold) + " " + std::to_string(t.silver) + " " +
           std::to_string(t.bronze) + " " + std::to_string(t.level);
}

Trophies load_trophies(const std::string &line)
{
    Trophies t;
    int v[5];
    if (std::sscanf(line.c_str(), "%d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5 && v[0] >= 0 &&
        v[1] >= 0 && v[2] >= 0 && v[3] >= 0 && v[4] >= 0)
    {
        t.known = true;
        t.platinum = v[0];
        t.gold = v[1];
        t.silver = v[2];
        t.bronze = v[3];
        t.level = v[4];
    }
    return t;
}
} // namespace ps5lib
