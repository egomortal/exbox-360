// Host tests for reading the PS5 game library and profile cache.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../ps5_library.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

static void mkdirs(const std::string &path)
{
    for (std::size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/')
            mkdir(path.substr(0, i).c_str(), 0755);
}
static void write(const std::string &path, const std::string &data)
{
    mkdirs(path.substr(0, path.rfind('/')));
    FILE *f = std::fopen(path.c_str(), "wb");
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}
static std::string sfo(const std::string &title)
{
    // Header, one index entry, key table, data table.
    std::string keys = std::string("TITLE") + '\0' + '\0' + '\0';
    std::string value = title + '\0';
    const unsigned key_off = 20 + 16, data_off = key_off + unsigned(keys.size());
    auto le32 = [](unsigned v) { return std::string{char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; };
    std::string out = le32(0x46535000u) + le32(0x101) + le32(key_off) + le32(data_off) + le32(1);
    out += std::string{0, 0, 4, 2} + le32(unsigned(value.size())) + le32(unsigned(value.size())) + le32(0);
    return out + keys + value;
}

static void parses_titles()
{
    const std::string json = R"({"localizedParameters":{"defaultLanguage":"en-US",
        "de-DE":{"titleName":"Das Spiel"},"en-US":{"titleName":"  Astro's Playroom "}},"titleId":"PPSA01949"})";
    CHECK(ps5lib::json_title(json) == "Astro's Playroom");
    CHECK(ps5lib::json_title(R"({"titleName":"Plain"})") == "Plain");
    CHECK(ps5lib::json_title("not json").empty());
    const std::string s = sfo("Red Dead Redemption 2");
    CHECK(ps5lib::sfo_title(std::vector<unsigned char>(s.begin(), s.end())) == "Red Dead Redemption 2");
    CHECK(ps5lib::sfo_title({1, 2, 3}).empty());
}

static void title_ids()
{
    CHECK(ps5lib::title_id("PPSA01949") && ps5lib::title_id("CUSA12345"));
    CHECK(!ps5lib::title_id("PPSA0194") && !ps5lib::title_id("ppsa01949") && !ps5lib::title_id("PPSA0194X"));
    CHECK(ps5lib::hidden_title("NPXS40002") && ps5lib::hidden_title("FAKE93601") && ps5lib::hidden_title("PPSA99360"));
    CHECK(!ps5lib::hidden_title("PPSA01949"));
}

static void scans_a_console()
{
    char tmpl[] = "/tmp/ps5libXXXXXX";
    const std::string root = mkdtemp(tmpl);
    write(root + "/user/appmeta/PPSA01949/param.json",
          R"({"localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":"Astro's Playroom"}}})");
    write(root + "/user/appmeta/PPSA01949/icon0.png", "png");
    write(root + "/user/appmeta/PPSA04607/param.json", R"({"titleName":"bayonetta"})");
    write(root + "/user/appmeta/NPXS40002/param.json", R"({"titleName":"Settings"})");
    write(root + "/user/appmeta/FAKE93601/param.json", R"({"titleName":"Ex-Box 360"})");
    write(root + "/user/appmeta/notatitle/param.json", R"({"titleName":"x"})");
    write(root + "/user/appmeta/PPSA00001/param.json", "{}"); // no name: falls back to the id
    write(root + "/user/appmeta/CUSA12345/icon0.png", "png"); // art without title metadata
    write(root + "/system_data/priv/appmeta/CUSA12345/param.sfo", sfo("Zelda-like PS4 Game"));
    write(root + "/system_data/priv/appmeta/PPSA01949/param.sfo", sfo("Duplicate"));
    const auto games = ps5lib::installed_games(root);
    CHECK(games.size() == 4);
    if (games.size() == 4)
    {
        CHECK(games[0].title == "Astro's Playroom" && games[0].title_id == "PPSA01949");
        CHECK(games[0].icon == root + "/user/appmeta/PPSA01949/icon0.png");
        CHECK(games[1].title == "bayonetta" && games[1].icon.empty());
        CHECK(games[2].title == "PPSA00001");
        CHECK(games[3].title == "Zelda-like PS4 Game" && games[3].title_id == "CUSA12345");
        CHECK(games[3].icon == root + "/user/appmeta/CUSA12345/icon0.png");
        for (const auto &g : games)
            CHECK(g.executable.empty());
    }
    CHECK(ps5lib::installed_games(root + "/missing").empty());

    write(root + "/data/homebrew/ExBox360/game_art/CUSA12345.jpg", "jpg");
    const auto covered = ps5lib::installed_games(root);
    const auto cover = std::find_if(covered.begin(), covered.end(), [](const dash::App &g) {
        return g.title_id == "CUSA12345";
    });
    CHECK(cover != covered.end() && cover->icon == root + "/data/homebrew/ExBox360/game_art/CUSA12345.jpg");
    if (cover != covered.end())
        CHECK(cover->playstation_icon == root + "/user/appmeta/CUSA12345/icon0.png" &&
              cover->xbox_cover == cover->icon);

    // Profile cache: online.json carries trophySummary as an escaped string.
    write(ps5lib::profile_dir(0x10000001, root) + "/online.json",
          R"({"onlineId":"celestials","trophySummary":"{\"level\":12,\"progress\":40,\"earnedTrophies\":{\"platinum\":2,\"gold\":9,\"silver\":30,\"bronze\":120}}"})");
    const auto t = ps5lib::read_trophies(0x10000001, root);
    CHECK(t.known && t.level == 12 && t.platinum == 2 && t.gold == 9 && t.silver == 30 && t.bronze == 120);
    CHECK(t.total() == 161 && t.points() == 120 * 15 + 30 * 30 + 9 * 90 + 2 * 300);
    CHECK(ps5lib::profile_dir(0x1a2b, "") == "/system_data/priv/cache/profile/0x00001A2B");
    CHECK(!ps5lib::read_trophies(0x2, root).known);
    CHECK(!ps5lib::parse_trophies(R"({"onlineId":"x"})").known);
}

static void finds_screenshots()
{
    char tmpl[] = "/tmp/ps5capXXXXXX";
    const std::string root = mkdtemp(tmpl);
    const std::string dir = root + "/user/av_contents/photo/NPXS40087/NPXS40087/168";
    write(dir + "/20261017_200850_00357409.jxr", "hdr");
    write(dir + "/20261017_200850_00357409.meta", "meta");
    write(root + "/user/av_contents/photo/PPSA01949/PPSA01949/1/20261018_101010_00000001.JPG", "jpg");
    write(root + "/user/av_contents/photo/PPSA01949/PPSA01949/1/20261019_090000_00000002.png", "png");
    write(root + "/user/av_contents/photo/PPSA01949/PPSA01949/1/empty.png", "");
    const auto shots = ps5lib::find_captures(root);
    CHECK(shots.size() == 3); // the .meta and the empty file are not pictures
    if (shots.size() == 3)
    {
        CHECK(shots[0].find("20261019_090000") != std::string::npos); // newest first
        CHECK(shots[2].find(".jxr") != std::string::npos);
        CHECK(ps5lib::viewable_picture(shots[0]) && ps5lib::viewable_picture(shots[1]));
        CHECK(!ps5lib::viewable_picture(shots[2]));
    }
    CHECK(ps5lib::find_captures(root + "/nothing").empty());
    CHECK(ps5lib::find_captures(root, 1).size() == 1);
}

static void achievement_toasts()
{
    ps5lib::Trophies a = ps5lib::load_trophies("1 4 10 50 9");
    CHECK(a.known && a.platinum == 1 && a.gold == 4 && a.silver == 10 && a.bronze == 50 && a.level == 9);
    CHECK(ps5lib::save_trophies(a) == "1 4 10 50 9");
    CHECK(!ps5lib::load_trophies("").known && !ps5lib::load_trophies("1 2 3").known);
    CHECK(!ps5lib::load_trophies("1 -2 3 4 5").known && ps5lib::save_trophies({}).empty());
    // Nothing new, or nothing known before: no toast.
    CHECK(ps5lib::unlocked(a, a).empty() && ps5lib::unlocked({}, a).empty());
    ps5lib::Trophies b = a;
    b.gold += 1;
    auto u = ps5lib::unlocked(a, b);
    CHECK(u.title == "Achievement unlocked" && u.text == "Gold trophy - 90 points");
    b.bronze += 2;
    b.level = 10;
    u = ps5lib::unlocked(a, b);
    CHECK(u.title == "3 achievements unlocked" && u.text == "120 points - level 10");
    // A count going down (another profile, a reset) is not an unlock.
    ps5lib::Trophies c = a;
    c.bronze -= 5;
    CHECK(ps5lib::unlocked(a, c).empty());
}

static void storage_snapshot_is_read_only()
{
    char tmpl[]="/tmp/ps5storageXXXXXX";
    const char *temp=mkdtemp(tmpl);CHECK(temp!=nullptr);if (!temp) return;
    const std::string root=temp;
    const std::string shot="/user/av_contents/photo/PPSA00001/20261028_123456_1.jpg";
    write(root+shot,"picture");
    write(root+"/data/nxe/music/song.wav","wave");
    write(root+"/data/nxe/music/empty.wav","");
    write(root+"/data/nxe/music/notes.txt","ignored");
    write(root+"/data/homebrew/ExBox360/trailers/packed/clip.frames","frames");
    write(root+"/data/homebrew/ExBox360/era-skins/9199/background.jpg","theme");
    write(root+"/data/homebrew/ExBox360/avatar/kit.nxak","kit");
    mkdirs(root+"/mnt/usb0"); // directory on the same filesystem is not mounted storage
    CHECK(symlink((root+shot).c_str(),(root+"/data/nxe/music/link.wav").c_str())==0);
    dash::App game;game.title="Installed title";game.title_id="PPSA00001";
    const auto snapshot=ps5lib::storage_inventory({game},{"/data/nxe/music"},root,root+"/data/homebrew/ExBox360/trailers");
    CHECK(snapshot.devices.size()==1 && !snapshot.limited); // /user and /data deduplicated by st_dev
    struct statvfs info{};CHECK(statvfs((root+"/user").c_str(),&info)==0);
    if (!snapshot.devices.empty()) {
        CHECK(snapshot.devices[0].capacity==static_cast<long long>(info.f_blocks)*static_cast<long long>(info.f_frsize));
        CHECK(snapshot.devices[0].free>=0 && snapshot.devices[0].free<=snapshot.devices[0].capacity);
    }
    CHECK(snapshot.items.size()==7); // title, capture, two songs, trailer, theme and avatar kit
    for (const auto &item:snapshot.items) {
        if (item.name=="Installed title") CHECK(item.bytes==-1 && item.device==-1 && item.path.empty());
        else {
            CHECK(item.device==0 && item.bytes>=0 && item.owner.empty());
            CHECK(item.path.rfind(root,0)!=0); // desktop fixture prefix is not a console location
            CHECK(static_cast<long long>(ps5lib::read_file(root+item.path).size())==item.bytes);
            if (item.category==dash::StoragePictures) CHECK(item.created=="2026-10-28 12:34:56");
            else CHECK(item.created.empty()); // mtime is never called creation time
        }
        CHECK(item.category!=dash::StorageProfiles && item.category!=dash::StorageDemos && item.category!=dash::StorageSystem);
        CHECK(item.name!="link.wav" && item.name!="notes.txt");
    }
    CHECK(ps5lib::read_file(root+shot)=="picture");
    CHECK(ps5lib::read_file(root+"/data/nxe/music/song.wav")=="wave");
    const auto absent=ps5lib::storage_inventory({}, {},root+"/missing",root+"/missing/trailers");
    CHECK(absent.devices.empty() && absent.items.empty());
}

int main()
{
    parses_titles();
    title_ids();
    scans_a_console();
    achievement_toasts();
    finds_screenshots();
    storage_snapshot_is_read_only();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
