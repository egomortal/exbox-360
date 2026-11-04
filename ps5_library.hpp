// Ex-Box 360: what the PS5 itself knows, read from its files. No SDL.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Installed games: /user/appmeta/<TITLE>/ (param.json + icon0.png) for PS5
// titles and /system_data/priv/appmeta/<TITLE>/ (param.sfo) for PS4 ones, the
// same places PuckBridge's game list and PS5 Upload read. The profile cache,
// /system_data/priv/cache/profile/0x<UID>/, holds the signed-in user's
// picture (avatar.png) and online.json with a trophy summary, as PS5 Upload's
// profile tools describe it. Every reader tolerates a missing file.
#pragma once

#include "dash_model.hpp"

#include <string>
#include <vector>

namespace ps5lib
{
// A title id: four capitals then five digits.
bool title_id(const std::string &id);
// System apps, homebrew launch tiles and the old broken Ex-Box tile.
bool hidden_title(const std::string &id);

// "titleName" from param.json, preferring the default language's block.
std::string json_title(const std::string &json);
// "TITLE" from a PS4 param.sfo.
std::string sfo_title(const std::vector<unsigned char> &sfo);

// Installed games under the given appmeta roots, sorted by name, deduplicated
// by title id. `prefix` is prepended to every path (empty on the console).
std::vector<dash::App> installed_games(const std::string &prefix = "");

// Screenshots under /user/av_contents/photo (any depth up to five folders),
// newest first: the PS5 names them by date and time. .jpg/.jpeg/.png can be
// shown; .jxr (the HDR format) is listed so the viewer can say why it cannot.
std::vector<std::string> find_captures(const std::string &prefix = "", std::size_t limit = 500);
bool viewable_picture(const std::string &path);

// Snapshot only: statvfs/stat, directory reads; no mutation or content reads.
// Game payload sizes/locations are not in appmeta, so those remain unknown and
// appear only under All Devices. Paths use the same desktop prefix as captures.
dash::StorageInventory storage_inventory(const std::vector<dash::App> &games,
    const std::vector<std::string> &music_roots, const std::string &prefix = "",
    const std::string &trailers = "/data/homebrew/ExBox360/trailers");

struct Trophies
{
    bool known = false;
    int level = 0, platinum = 0, gold = 0, silver = 0, bronze = 0;
    int total() const
    {
        return platinum + gold + silver + bronze;
    }
    // PSN's own trophy point values: 15 / 30 / 90 / 300.
    int points() const
    {
        return bronze * 15 + silver * 30 + gold * 90 + platinum * 300;
    }
};
// The trophySummary inside online.json (itself a JSON string, escaped).
Trophies parse_trophies(const std::string &online_json);

// What the 360's "Achievement unlocked" toast says when the trophy counts grew
// between two readings; empty when nothing new was earned.
struct Unlock
{
    std::string title, text;
    bool empty() const
    {
        return title.empty();
    }
};
Unlock unlocked(const Trophies &before, const Trophies &after);
// "plat gold silver bronze level" on one line, as kept between runs.
std::string save_trophies(const Trophies &t);
Trophies load_trophies(const std::string &line);

std::string profile_dir(unsigned user, const std::string &prefix = "");
Trophies read_trophies(unsigned user, const std::string &prefix = "");
std::string read_file(const std::string &path, std::size_t limit = 256 * 1024);
} // namespace ps5lib
