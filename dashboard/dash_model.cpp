// Ex-Box 360: dashboard state and button behaviour. Pure.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dash_model.hpp"
#include "dashboard_layout.hpp"
#include "modules/games_frontend/rom_guide_input.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace dash
{
namespace
{
struct Fixed
{
    const char *title;
    int icon;
    Goal goal;
};

// My Xbox keeps its first card in front; attached apps and games follow it.
const Fixed kMyXbox[] = {
    {"Your Homebrew", IconDisc, Goal::ToApps},     {"Games", IconGames, Goal::ToGames},
    {"Media", IconMusic, Goal::ToMedia},           {"Pictures", IconPhotos, Goal::ToPictures},
    {"My Avatar", IconAvatar, Goal::ToAvatar},
    {"Controller Map", IconSettings, Goal::ControllerMap},
};
const Fixed kFriends[] = {
    {"Friends", IconFriends, Goal::Unavailable},
    {"Messages", IconMessages, Goal::Unavailable},
};
const Fixed kMetroSocial[] = {
    {"Friends", IconFriends, Goal::Unavailable},
    {"Social Apps", IconMessages, Goal::Unavailable},
    {"Sign In or Out", IconAvatar, Goal::Unavailable},
};
const Fixed kMedia[] = {
    {"Music", IconMusic, Goal::ToMusic},
    {"Pictures", IconPhotos, Goal::ToPictures},
    {"Videos", IconVideo, Goal::ToVideos},
};
constexpr int kMyXboxFixed = int(sizeof kMyXbox / sizeof kMyXbox[0]);
// NXE 9199 homepage/xbox360channel.xml slots (Disk in Tray only with a disc):
// Gamer Card, Games Library, Video Library, Music Library, Picture Library,
// Windows Media Center, Settings.
// The homebrew front card stands in the disc slot, which leads when a disc
// is in the tray.
// NXE 9199 homepage/xbox360channel.xml, in order (the disc slot is the
// owner's homebrew; Mediaroom and HD-DVD only appeared with that hardware).
const Fixed kNxeMyXbox[] = {
    {"Your Homebrew", IconDisc, Goal::ToApps},
    {kBladesGamercard, IconAvatar, Goal::ToProfile},   {"Game Library", IconGames, Goal::ToSearch},
    {"Video Library", IconVideo, Goal::ToVideos},     {"Music Library", IconMusic, Goal::ToMusic},
    {"Picture Library", IconPhotos, Goal::ToPictures},{"Windows Media Center", IconVideo, Goal::ToMediaCenter},
    {"Solutions", IconSettings, Goal::ControllerMap}, {"System Settings", IconSettings, Goal::ToSettings},
};
// Kinect 13604's list for an ordinary profile: Kinect Hub, Family Center and
// Built-in Wireless are conditional slots; Family Settings sits before Settings.
const Fixed kKinectMyXbox[] = {
    {"Your Homebrew", IconDisc, Goal::ToApps},        {kBladesGamercard, IconAvatar, Goal::ToProfile},
    {"Game Library", IconGames, Goal::ToSearch},
    {"Video Library", IconVideo, Goal::ToVideos},     {"Music Library", IconMusic, Goal::ToMusic},
    {"Picture Library", IconPhotos, Goal::ToPictures},{"Windows Media Center", IconVideo, Goal::ToMediaCenter},
    {"Family Settings", IconSettings, Goal::Unavailable},{"System Settings", IconSettings, Goal::ToSettings},
};

struct FixedList {const Fixed *items; int count;};
FixedList my_xbox_slots(int era)
{
    if (era == 1) return {kNxeMyXbox, int(sizeof kNxeMyXbox / sizeof kNxeMyXbox[0])};
    if (era == 2) return {kKinectMyXbox, int(sizeof kKinectMyXbox / sizeof kKinectMyXbox[0])};
    return {kMyXbox, kMyXboxFixed};
}
Card fixed_slot(const FixedList &list, int index)
{
    const Fixed &f = list.items[std::clamp(index, 0, list.count - 1)];
    Card card;
    card.title = f.title;
    card.icon = f.icon;
    card.goal = f.goal;
    // Second lines, verbatim from homepage/strings.xus (IDS_*_LINE2 / _DESC).
    const std::string title = f.title;
    if (title == "Windows Media Center") card.subtitle = "TV and media from your PC";
    if (title == "Solutions") card.subtitle = "Help, How-to, and Tips";
    return card;
}

template <std::size_t N> Card fixed(const Fixed (&list)[N], int index)
{
    const Fixed &f = list[std::size_t(std::clamp(index, 0, int(N) - 1))];
    Card card;
    card.title = f.title;
    card.icon = f.icon;
    card.goal = f.goal;
    return card;
}

int app_index(const std::vector<App> &apps, const std::string &executable)
{
    for (std::size_t i = 0; i < apps.size(); ++i)
        if (apps[i].executable == executable)
            return int(i);
    return -1;
}

int game_index(const std::vector<App> &games, const std::string &key)
{
    if (key.rfind("title:", 0) != 0)
        return -1;
    const std::string id = key.substr(6);
    for (std::size_t i = 0; i < games.size(); ++i)
        if (games[i].title_id == id)
            return int(i);
    return -1;
}

// A PlayStation title id: four capitals and five digits (PPSA01949, CUSA12345).
bool title_id_ok(const std::string &id)
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

bool title_less(const std::string &a, const std::string &b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
        [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) <
                   std::tolower(static_cast<unsigned char>(y));
        });
}

Card app_card(const std::vector<App> &apps, int i)
{
    Card c;
    c.title = apps[std::size_t(i)].title;
    c.kind = Kind::App;
    c.icon = IconGames;
    c.app = i;
    c.image = apps[std::size_t(i)].icon;
    return c;
}

Card game_card(const std::vector<App> &games, int i, const std::map<std::string, int> &choices)
{
    Card c;
    const App &game = games[std::size_t(i)];
    c.title = game.title;
    c.kind = Kind::Game;
    c.icon = IconDisc;
    c.game = i;
    const auto choice = choices.find(game.title_id);
    const int source = choice == choices.end() ? 0 : choice->second;
    c.image = source == 1 ? game.playstation_icon : source == 2 && !game.xbox_cover.empty() ? game.xbox_cover : game.icon;
    if (c.image.empty()) c.image = game.playstation_icon;
    return c;
}
} // namespace

std::string Model::blade_title(int blade) const
{
    switch (blade)
    {
    case BladeApps: return era>=1?"Marketplace":"Apps";
    case BladeGames: return era==3 && !metro_v1() ? "Games & Apps" : "Games";
    case BladeHome: return player.empty() ? "Player" : player;
    case BladeMedia: return "Media";
    default: return "Settings";
    }
}

std::vector<GuideEntry> Model::blades_items(int which) const
{
    using A = GuideAction;
    switch (which)
    {
    // The gamercard leads each list (ctlGamerCard is the scenes' first nav
    // control and their default focus); A on it opens the owner's avatar.
    case Friends: // xbox live (live/liveSignedInUI, liveSignedOutUI)
        if (blades_signed_out)
            return {{kBladesGamercard,A::Profile},{"Join Xbox Live",A::Unavailable},{"Recover Gamertag from Xbox Live",A::Unavailable}};
        return {{kBladesGamercard,A::Profile},{"Messages",A::NotificationHistory},{"Friends",A::Unavailable},{"Marketplace",A::LaunchSystem,1}};
    case Games: // gamesbla/gamesSignedIn, gamesSignedOut
        if (blades_signed_out)
            return {{kBladesGamercard,A::Profile},{"Create Gamer Profile",A::Avatar},{"Xbox Live Arcade",A::AllGames},
                    {"Demos",A::Unavailable},{"Trailers",A::Videos}};
        return {{kBladesGamercard,A::Profile},{"Achievements",A::Achievements},{"Played Games",A::RecentGames},{"Xbox Live Arcade",A::AllGames},
                {"Demos",A::Unavailable},{"Trailers",A::Videos}};
    case Media: // mediabla/mediaSignedIn
        return {{kBladesGamercard,A::Profile},{"Music",A::Music},{"Pictures",A::Pictures},{"Video",A::Videos},{"Media Center",A::LaunchSystem,2}};
    case MyXbox: // dashmain System blade
        if (blades_page == 1) // consoles/dashSysCslSet: its nine buttons, in order
            return {{"Display",A::Unavailable},{"Audio",A::ToggleSounds},{"Language",A::Unavailable},{"Clock",A::Unavailable},
                    {"Locale",A::Unavailable},{"Auto-Off",A::Unavailable},{"Screen Saver",A::ToggleDimming},
                    {"Remote Control",A::ControllerMap},{"System Info",A::LibrarySummary}};
        if (blades_page == 2) // memory/DeviceSelector
            return {{"All Devices",A::LibrarySummary},{"Hard Drive",A::LibrarySummary}};
        if (blades_page == 4) // consoles/dashSysCslSetPControlSelect
            return {{"Console Controls",A::ConsoleSettings},{"Xbox Live Controls",A::Unavailable}};
        if (blades_page == 5) // consoles/dashSysCslSetPControl: Family Settings stay off on PS5
            return {{"Game Ratings",A::Unavailable},{"DVD Movie Ratings",A::Unavailable},{"Access to Xbox Live",A::Unavailable},
                    {"Xbox Live Membership Creation",A::Unavailable},{"Original Xbox Games",A::Unavailable},
                    {"Set Pass Code",A::Unavailable},{"Turn Off Family Settings",A::Unavailable}};
        if (blades_page == 3) // memory/Categories
            return {{"Games",A::AllGames},{"Music",A::Music},{"Videos and Trailers",A::Videos},
                    {"Gamer Pictures",A::ChangeGamerpic},{"Themes",A::SelectTheme}};
        return {{"Console Settings",A::ConsoleSettings},{"Family Settings",A::Unavailable},{"Memory",A::LibrarySummary},
                {"Network Settings",A::NetworkSettings},{"Computers",A::Unavailable},{"Initial Setup",A::InitialSetup}};
    default: return {};
    }
}

std::vector<GuideEntry> Model::blade_entries(int blade) const
{
    if (blade==BladeHome && guide_exbox_page) return exbox_guide_entries();
    // Music drill-down remains inside the ordinary Media blade. It is not a
    // second menu system and uses the same renderer, focus and blade chrome.
    if(blade==BladeMedia && guide_music_page>0)return host_blade_entries(blade,true);
    using A=GuideAction;
    std::vector<GuideEntry> out;
    const auto append_unique=[&](const std::vector<GuideEntry>& additions) {
        for(const GuideEntry &utility:additions){
            const bool duplicate=std::any_of(out.begin(),out.end(),[&](const GuideEntry &existing){
                if(existing.label==utility.label)return true;
                return utility.action!=A::Unavailable&&existing.action==utility.action&&existing.app==utility.app;
            });
            if(!duplicate)out.push_back(utility);
        }
    };
    if (era==0) {
        if (blade!=BladeHome) return host_blade_entries(blade);
        // Original CommunityBar positions must not launch unrelated host apps.
        // Xbox network services are unavailable; retain the source semantics.
        out={{"Messages",A::Unavailable,-1,"btnMessages"},
                {"Friends",A::Unavailable,-1,"btnFriends"},
                {"Players",A::Unavailable,-1,"btnRecentPlayers"},
                {dashboard_build>=5759?"Chat and IM":"Private Chat",A::Unavailable,-1,"btnQuickChat"},
                {"Personal Settings",A::OpenGuideSettings,-1,"btnOptions"},
                {"Select Music",A::Music,-1,"btnPlaylist"},
                {"Xbox Dashboard",A::MyXbox,-1,"btnGameBanner"}};
        // Keep the dashboard-owned page in the seven visible Blades rows. The
        // first three Community buttons retain their special horizontal group.
        out.insert(out.begin()+3,{"Ex-Box Menu",A::OpenExBoxMenu});
        const GuideEntry play=preferred_guide_play();
        if(play.action!=A::Unavailable)out.insert(out.begin()+4,play);
        append_unique(host_blade_entries(blade,true));
        return out;
    }
    // hud/GuideMain and *TabSignedIn.xur, 9199/13604/15574/17559.
    // Preserve scene order; source-hidden conditional/debug rows stay hidden.
    auto row=[&](const char *id,const char *label,A action=A::Unavailable) {
        out.push_back({label,action,-1,id});
    };
    switch (blade) {
    case BladeApps:
        if (era==3 && !metro_v1()) break;
        if (era>=2) {
            row("btnActiveDownloads","Refresh Homebrew",A::Refresh);row("btnRedeemCode","Library Summary",A::LibrarySummary);
        }
        row("btnGameMarketplace","Apps",A::Apps);
        if (era<3) row("btnVideoMarketplace","Media",A::Media);
        row("btnAvatarMarketplace",avatar_editable||external_avatar_editor?"Avatar Editor":"My Avatar",avatar_editable||external_avatar_editor?A::AvatarEdit:A::Avatar);
        if (era==1) {
            row("btnActiveDownloads","Refresh Homebrew",A::Refresh);row("btnRedeemCode","Library Summary",A::LibrarySummary);
        }
        break;
    case BladeGames:
        row("btnAchievements","Achievements",A::Achievements);
        row("btnAvatarAwards","Refresh Games & Homebrew",A::Refresh);
        if (era>=2) row("btnQuickLaunch",era==2?"Quick Launch":metro_v1()?"Quickplay":"Recent",A::RecentGames);
        row("btnGamesLibrary",era<3?"Game Library":"My Games",A::AllGames);
        break;
    case BladeHome:
        row("btnDashboard",era<3?"Xbox Dashboard":"Xbox Home",A::MyXbox);
        if (era==1) row("btnJumpIn","Quick Launch",A::RecentGames);
        row("btnConnectToLive","Notifications",A::NotificationHistory);
        {
            const GuideEntry play=preferred_guide_play();
            if(play.action==A::Unavailable)row("btnDiscInTray","Disc in Tray");
            else out.push_back({play.label,play.action,play.app,"btnDiscInTray"});
        }
        break;
    case BladeMedia:
        row("btnVideoLibrary",era<3?"Video Library":metro_v1()?"Video Player":"System Video Player",A::Videos);
        row("btnMusicLibrary",era<3?"Music Library":metro_v1()?"Music Player":"System Music Player",A::Music);
        row("btnPictureLibrary",era<3?"Picture Library":"Picture Viewer",A::Pictures);
        // Media Center is not present on PS5. Reuse its native row for the
        // dashboard's recorder instead of appending another utility panel.
        if(recorder_busy)row("btnMCX","Screen Recorder Working...",A::RecordScreen);
        else if(recorder_recording){
            const int total=std::max(0,int(recorder_seconds));char time[16];
            std::snprintf(time,sizeof time,"%02d:%02d",total/60,total%60);
            row("btnMCX",(std::string(recorder_paused?"Recording Paused ":"Recording ")+time).c_str(),A::RecordScreen);
        }else row("btnMCX",recorder_available?"Start Recording":"Record Screen (Create)",A::RecordScreen);
        // Keep the existing scene-driven mini player's ordering and actions.
        if (media_player) {
            const GuideEntry song{now_playing>=0?now_playing_title:"Select Music",A::Music};
            if (media_player==2) out.push_back(song);
            if (now_playing>=0) {
                out.push_back({music_paused?"Resume Music":"Pause Music",A::MusicPause});
                out.push_back({"Previous Song",A::MusicPrevious});out.push_back({"Stop Music",A::MusicStop});
                out.push_back({"Next Song",A::MusicNext});out.push_back({music_shuffle?"Shuffle Off":"Shuffle On",A::MusicShuffle});
                out.push_back({"Volume " + std::to_string(music_volume * 10) + "%",A::MusicVolume});
            }
            if (media_player==1) out.push_back(song);
        }
        break;
    default:
        row("btnProfile","Profile",A::Profile);
        if (era==1) row("btnNotifications","Notifications",A::NotificationHistory);
        row("btnPersonalSettings","Themes",A::SelectTheme);
        row("btnFamilySettings","Controller Map",A::ControllerMap);
        row("btnSystemSettings","System Settings",A::SystemSettings);
        row("btnAccountManagement",avatar_editable||external_avatar_editor?"Avatar Editor":"Avatar Settings",avatar_editable||external_avatar_editor?A::AvatarEdit:A::Avatar);
        if (era==3) row("btnKinectTuner","Notifications",A::NotificationHistory);
        row("btnShutdown","Turn Off Console",A::ExitToPs5);
        break;
    }
    // The clean Home-blade subpage is the immediate route to Ex-Box features.
    // Retain the existing contextual rows as well: old return paths, pinned
    // entries and direct game/app launching must remain compatible.
    if(blade==BladeHome) out.push_back({"Ex-Box Menu",A::OpenExBoxMenu});
    append_unique(host_blade_entries(blade,true));
    // Metro V2 calls this blade "Games & Apps" and has no separate Apps tab.
    // Keep the app launch/refresh rows native to that combined blade instead of
    // requiring the retired secondary Utilities menu.
    if(blade==BladeGames && era==3 && !metro_v1())
        append_unique(host_blade_entries(BladeApps,true));
    return out;
}

GuideEntry Model::preferred_guide_play() const
{
    using A=GuideAction;
    // Match the 360's "Play <disc>" idea with the owner's most relevant local
    // title. Recent activity wins, then installed games, then game-classified
    // homebrew. A non-tool homebrew remains a useful final fallback.
    for(const std::string &key:recent){
        const int g=game_index(games,key);if(g>=0)return {"Play "+games[std::size_t(g)].title,A::LaunchTitle,g};
        const int a=app_index(apps,key);if(a>=0)return {"Play "+apps[std::size_t(a)].title,A::Launch,a};
    }
    const auto ordered=game_order();
    if(!ordered.empty()){const int g=ordered.front();return {"Play "+games[std::size_t(g)].title,A::LaunchTitle,g};}
    const auto classified=game_homebrew();
    if(!classified.empty()){const int a=classified.front();return {"Play "+apps[std::size_t(a)].title,A::Launch,a};}
    const auto utility=[](std::string value){
        std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});
        for(const char *token:{"ftp","payload","websrv","itemzflow","omni","kstuff","loader","manager","tool"})
            if(value.find(token)!=std::string::npos)return true;
        return false;
    };
    for(const int a:app_homebrew())if(!utility(apps[std::size_t(a)].title+" "+apps[std::size_t(a)].executable))
        return {"Play "+apps[std::size_t(a)].title,A::Launch,a};
    return {"Disc in Tray",A::Unavailable};
}

std::vector<GuideEntry> Model::exbox_guide_entries() const
{
    using A=GuideAction;
    std::vector<GuideEntry> out;
    const GuideEntry play=preferred_guide_play();
    if(play.action!=A::Unavailable)out.push_back(play);
    out.push_back({"Games & Homebrew",A::Games});
    out.push_back({"Apps",A::Apps});
    out.push_back({"Profile",A::Profile});
    out.push_back({"My Avatar",A::Avatar});
    if(avatar_editable||external_avatar_editor)out.push_back({"Avatar Editor",A::AvatarEdit});
    out.push_back({"System Settings",A::SystemSettings});
    out.push_back({"Exit to PS5 Home",A::ExitToPs5});
    return out;
}

// Ex-Box additions expressed as ordinary Guide rows and nested blade pages.
std::vector<GuideEntry> Model::host_blade_entries(int blade,bool full) const
{
    using A = GuideAction;
    std::vector<GuideEntry> out;
    const auto record_label=[&] {
        if(recorder_busy)return std::string("Screen Recorder Working...");
        if(recorder_recording){const int total=std::max(0,int(recorder_seconds));char time[16];std::snprintf(time,sizeof time,"%02d:%02d",total/60,total%60);return std::string(recorder_paused?"Screen Recording Paused ":"Screen Recording ")+time;}
        return std::string(recorder_available?"Start Screen Recording":"Record Screen (Create)");
    };
    const auto append_recording=[&] {
        out.push_back({record_label(),A::RecordScreen});
    };
    if (era==0 && blade==BladeHome) return {
        {"Messages",A::NotificationHistory},{"Avatar",A::Avatar},{"Games",A::Games},
        {"PS5 Home",A::ExitToPs5},{"Personal Settings",A::OpenGuideSettings},
        {"Select Music",A::Music},{"Xbox Dashboard",A::MyXbox}
    };
    switch (blade)
    {
    case BladeApps:
        out.push_back({"Apps Channel", A::Apps});
        for (int i : app_homebrew())
            out.push_back({apps[std::size_t(i)].title, A::Launch, i});
        if (apps.empty())
            out.push_back({"Refresh Homebrew", A::Refresh});
        break;
    case BladeGames:
        out.push_back({"Games Channel", A::Games});
        for (int i : game_order())
            out.push_back({games[std::size_t(i)].title, A::LaunchTitle, i});
        for (int i : game_homebrew())
            out.push_back({apps[std::size_t(i)].title, A::Launch, i});
        if (era==3) for (int i : app_homebrew())
            out.push_back({apps[std::size_t(i)].title, A::Launch, i});
        out.push_back({"Refresh Games & Homebrew",A::Refresh});
        // The PS5's own game apps, last.
        out.push_back({kSystemApps[0].title, A::LaunchSystem, 0});
        out.push_back({kSystemApps[1].title, A::LaunchSystem, 1});
        break;
    case BladeHome:
        // The home blade's head, as each era's Guide had it: NXE "Xbox Dashboard /
        // Quick Launch", Kinect "Xbox Dashboard", Metro "Xbox Home" then the social rows.
        out.push_back({era == 3 ? "Xbox Home" : "Xbox Dashboard", A::MyXbox});
        if (era == 1)
            out.push_back({"Quick Launch", A::RecentGames});
        if (era == 3)
        {
            out.push_back({"Friends", A::Unavailable});
            out.push_back({"Party", A::Unavailable});
            out.push_back({"Messages", A::NotificationHistory});
            out.push_back({"Beacons & Activity", A::Unavailable});
            out.push_back({"Chat", A::Unavailable});
            out.push_back({"Open Tray", A::AllGames});
        }
        {
            int shown = 0;
            for (const std::string &key : pins)
            {
                const int g = game_index(games, key);
                const int a = g < 0 ? app_index(apps, key) : -1;
                if (g >= 0)
                    out.push_back({"Attached: " + games[std::size_t(g)].title, A::LaunchTitle, g});
                else if (a >= 0)
                    out.push_back({"Attached: " + apps[std::size_t(a)].title, A::Launch, a});
                else
                    continue;
                if (++shown == 3) break;
            }
        }
        {
            int shown = 0;
            for (const std::string &key : recent)
            {
                const int g = game_index(games, key);
                const int a = g < 0 ? app_index(apps, key) : -1;
                // The 360 offered "Play <the game in the tray>"; the last played stands in.
                if (g >= 0)
                    out.push_back({"Play " + games[std::size_t(g)].title, A::LaunchTitle, g});
                else if (a >= 0)
                    out.push_back({"Play " + apps[std::size_t(a)].title, A::Launch, a});
                else
                    continue;
                if (++shown == 3) break;
            }
        }
        out.push_back({"Games", A::Games});
        out.push_back({"Apps", A::Apps});
        out.push_back({"Library Summary", A::LibrarySummary});
        if (era != 3) // Metro's Messages row already opens it
            out.push_back({"Notification History", A::NotificationHistory});
        // Ex-Box addition: no 360 original; shortcut to the dashboard profile.
        if (era >= 1) out.push_back({"Profile", A::Profile});
        out.push_back({"My Avatar", A::Avatar});
        if (avatar_editable || external_avatar_editor)
            out.push_back({"Avatar Editor", A::AvatarEdit});
        out.push_back({"Controller Map", A::ControllerMap});
        out.push_back({"Exit to PS5 Home", A::ExitToPs5});
        break;
    case BladeMedia:
        if (guide_music_page==5)
        {
            out.push_back({"Albums",A::GuideMusicAlbums});
            out.push_back({"Songs",A::GuideMusicSongs});
            out.push_back({"Current Queue",A::GuideMusicQueue});
            out.push_back({"Search Music",A::GuideMusicSearch});
            out.push_back({"Open Full Music",A::GuideMusicOpenFull});
            if (now_playing>=0)
            {
                out.push_back({"Now Playing: " + now_playing_title,A::GuideMusicQueue});
                out.push_back({music_paused?"Resume Music":"Pause Music",A::MusicPause});
                out.push_back({"Next Song",A::MusicNext});out.push_back({"Stop Music",A::MusicStop});
                out.push_back({"Previous Song",A::MusicPrevious});out.push_back({music_shuffle?"Shuffle Off":"Shuffle On",A::MusicShuffle});
                out.push_back({"Volume " + std::to_string(music_volume*10) + "%",A::MusicVolume});
            }
            break;
        }
        if (guide_music_page>0)
        {
            out.push_back({"< Music",A::GuideMusicBack});
            if (guide_music_page==4)
            {
                const char key=guide_music_key<26?char('A'+guide_music_key):' ';
                out.push_back({"Search: " + (guide_music_query.empty()?std::string("All songs"):guide_music_query),A::GuideMusicSearch});
                out.push_back({std::string(guide_music_search_edit?"Typing [":"Choose letter [")+key+"]",A::GuideMusicSearch});
            }
            for (size_t i=0;i<guide_music_rows.size();++i)
                out.push_back({guide_music_rows[i].label,A::GuideMusicPlay,int(i)});
            if (guide_music_rows.empty()) out.push_back({guide_music_page==3?"Queue is empty":"No matching music",A::Unavailable});
            break;
        }
        if (full)
        {
            out.push_back({"Media Channel",A::Media});
            out.push_back({"Pictures",A::Pictures});
            out.push_back({era==0?"Video":"Video Library",A::Videos});
            out.push_back({kSystemApps[2].title,A::LaunchSystem,2});
            out.push_back({kSystemApps[3].title,A::LaunchSystem,3});
            append_recording();
            break;
        }
        if (media_player && era >= 1)
        {
            out.push_back({"Media Channel", A::Media});
            out.push_back({"Pictures", A::Pictures});
            out.push_back({"Video Library", A::Videos});
            out.push_back({kSystemApps[2].title, A::LaunchSystem, 2});
            out.push_back({kSystemApps[3].title, A::LaunchSystem, 3});
            append_recording();
            // The mini player: the song (or Select Music) and its buttons.
            const GuideEntry song{now_playing >= 0 ? now_playing_title : "Select Music", A::Music};
            if (media_player == 2) out.push_back(song);
            if (now_playing >= 0)
            {
                out.push_back({music_paused ? "Resume Music" : "Pause Music", A::MusicPause});
                out.push_back({"Previous Song", A::MusicPrevious});
                out.push_back({"Stop Music", A::MusicStop});
                out.push_back({"Next Song", A::MusicNext});
                out.push_back({music_shuffle ? "Shuffle Off" : "Shuffle On", A::MusicShuffle});
                out.push_back({"Volume " + std::to_string(music_volume * 10) + "%", A::MusicVolume});
            }
            if (media_player == 1) out.push_back(song);
            break;
        }
        out.push_back({"Media Channel", A::Media});
        if (now_playing >= 0)
        {
            // What is playing, and the player's controls, as the 360's Guide had them.
            out.push_back({"Now Playing: " + now_playing_title, A::Music});
            out.push_back({music_paused ? "Resume Music" : "Pause Music", A::MusicPause});
            out.push_back({"Next Song", A::MusicNext});
            out.push_back({"Stop Music", A::MusicStop});
        }
        else
            out.push_back({"Music Library", A::Music});
        out.push_back({"Pictures", A::Pictures});
        out.push_back({era == 0 ? "Video" : "Video Library", A::Videos}); // mediabla / homepage strings.xus
        out.push_back({kSystemApps[2].title, A::LaunchSystem, 2});
        out.push_back({kSystemApps[3].title, A::LaunchSystem, 3});
        append_recording();
        break;
    default:
        out.push_back({"Controller Map", A::ControllerMap});
        out.push_back({"Themes",A::SelectTheme});
        out.push_back({"System Settings",A::SystemSettings});
        out.push_back({avatar_editable||external_avatar_editor?"Avatar Editor":"Avatar Settings",avatar_editable||external_avatar_editor?A::AvatarEdit:A::Avatar});
        out.push_back({"Change Gamerpic", A::ChangeGamerpic});
        out.push_back({sounds ? "Sounds: On" : "Sounds: Off", A::ToggleSounds});
        out.push_back({std::string("Dashboard: ") + kDashboardFamilies[dashboard_family(era,dashboard_build)].name + " >", A::CycleEra});
        constexpr const char *button_styles[]={"Glossy", "Retail Mix", "Simple"};
        out.push_back({std::string("Button Style: ") + button_styles[std::clamp(button_style,0,2)], A::CycleButtonStyle});
        out.push_back({dimming ? "Screen Dimming: On" : "Screen Dimming: Off", A::ToggleDimming});
        out.push_back({"Refresh Homebrew", A::Refresh});
        constexpr const char *orders[]={"Recent", "Title A-Z", "Title Z-A"};
        constexpr const char *notifications[]={"Off", "Short", "Standard", "Long"};
        out.push_back({std::string("Library Order: ") + orders[std::clamp(library_order,0,2)], A::CycleLibraryOrder});
        out.push_back({std::string("Notifications: ") + notifications[std::clamp(notification_level,0,3)], A::CycleNotifications});
        constexpr const char* promotion_modes[]={"Authentic","Local Personalized","Off"};
        out.push_back({std::string("Promotion Rotation: ")+promotion_modes[std::clamp(promotion_mode,0,2)],A::CyclePromotionMode});
        out.push_back({"Ads >",A::AdsSettings});
        out.push_back({"Exit to PS5 Home", A::ExitToPs5});
        break;
    }
    return out;
}

void Model::open_guide()
{
    guide = true;
    guide_volume_open = false;
    guide_host = false;
    guide_exbox_page = false;
    guide_blade = BladeHome;
    for (int &item : guide_item)
        item = 0;
}

int Model::count() const
{
    if (channel==Welcome) return era==2?6:4;
    if (era==0 && channel==MyXbox) return int(blades_items(MyXbox).size());
    if (era==0 && channel!=Apps)
        return int(blades_items(channel).size())+(channel==Friends && promotions && ad_mode[current_ad_profile()]!=0?int(promo_cards.size()):0)+1;
    if (era==0) return base_count()+1; // the tray-shaped library control
    if (era == 3)
    {
        const bool custom=promotions && (ad_mode[current_ad_profile()]==1 || ad_mode[current_ad_profile()]==2);
        if (metro_v1_home()) return custom?std::max(4,1+int(promo_cards.size())):4;
        if (metro_tab==1 && promotions && !promo_cards.empty()) return custom?std::max(7,1+int(promo_cards.size())):7;
        if (metro_tab == 2) return int(sizeof kMetroSocial / sizeof kMetroSocial[0]);
        if (metro_tab == 7) return metro_v1()?10:8;
        if (metro_tab == 0) return 1;
        if (channel == Media && metro_tab == 4) return 2;
        // Music is a dashboard page, not a single tile that hides every
        // useful choice one level deeper in the library.
        if (channel == Media && metro_tab == 5) return 7;
    }
    return base_count();
}

int Model::base_count() const
{
    switch (channel)
    {
    case Welcome: return era==2?6:4;
    case Friends: return int(sizeof kFriends / sizeof kFriends[0])+(era==0 && promotions && ad_mode[current_ad_profile()]!=0?int(promo_cards.size()):0);
    case Media: return int(sizeof kMedia / sizeof kMedia[0]);
    case Games: return std::max(1, int(games.size() + game_homebrew().size()) + (has_rom_game_guide_preview()?1:0));
    case Apps: return std::max(1, int(app_homebrew().size()));
    default: return my_xbox_slots(era).count + int(pins.size()) + (promotions && era!=0?int(promo_cards.size()):0);
    }
}

bool Model::has_rom_game_guide_preview() const
{
    if (std::any_of(games.begin(),games.end(),[](const App &game){return game.title_id==exbox_games::player_title;})) return true;
    return std::any_of(games_frontend.entries.begin(),games_frontend.entries.end(),[](const exbox_games::Entry &entry){
        return entry.platform==exbox_games::Platform::Xbox360;
    });
}

// The Music card names the song playing, on every dashboard's hub.
Card Model::card(int index) const
{
    Card c = card_base(index);
    if ((c.goal == Goal::ToMusic || c.goal == Goal::ToMusicNowPlaying) &&
        now_playing >= 0 && !now_playing_title.empty())
        c.subtitle = (music_paused ? "Paused: " : "Now Playing: ") + now_playing_title;
    return c;
}

Card Model::card_base(int index) const
{
    if (channel==Welcome) {
        const char *nxe[]={"WhatsNewSlotScene","XboxBasicsSlotScene","NxeVideoSlotScene","HideWelcomeSlotScene"};
        const char *kinect[]={"XboxBasicsSlotScene","XboxKinectSlotScene","XboxGoldFamilyPackSlotScene",
                              "DashFirstUseTutorialSlotScene","WirelessWelcomeSlotScene","HideWelcomeSlotScene"};
        const int i=std::clamp(index,0,era==2?5:3);
        const auto found=welcome_content.find(era==2?kinect[i]:nxe[i]);
        const char *nxe_labels[]={"What's Hot","Xbox Essentials","Xbox LIVE Video","Hide Channel"};
        const char *kinect_labels[]={"Getting Started","Kinect for Xbox 360","Family First","Xbox 360 Tutorials","Go Wireless","Hide Channel"};
        Card c;c.title=era==2?kinect_labels[i]:nxe_labels[i];
        c.goal=i==(era==2?5:3)?Goal::HideWelcome:(era==1?i<2:i==0)?Goal::ToWelcome:Goal::Unavailable;
        c.icon=IconSettings;
        if (found!=welcome_content.end()) {c.title=found->second.title;c.subtitle=found->second.body;c.image=found->second.art;}
        return c;
    }
    if (era==0 && channel!=MyXbox && index==count()-1)
        return {"Game Library",Kind::Hub,IconDisc,Goal::ToSearch,-1,-1,""};
    if (era==0 && channel!=Apps)
    {
        const auto entries=blades_items(channel);
        if (index>=int(entries.size()) && channel==Friends && index-int(entries.size())<int(promo_cards.size()))
            return promo_cards[size_t(index-int(entries.size()))];
        Card c;
        const GuideEntry &e=entries[std::size_t(std::clamp(index,0,int(entries.size())-1))];
        c.title=e.label;
        c.icon=e.action==GuideAction::Music?IconMusic:e.action==GuideAction::Pictures?IconPhotos:
               e.action==GuideAction::Videos?IconVideo:e.action==GuideAction::NotificationHistory?IconMessages:
               channel==Friends?IconFriends:channel==MyXbox?IconSettings:IconGames;
        return c;
    }
    if (era == 3)
    {
        if (metro_v1_home() && (!promotions || promo_cards.empty())) {
            if (index==0) return {"Game Videos",Kind::Hub,IconVideo,Goal::ToVideos,-1,-1,""};
            if (index==1) return fixed(kMyXbox,0);
            if (index==2) return fixed(kMyXbox,1);
            return fixed(kMyXbox,2);
        }
        if (metro_tab==1 && promotions && !promo_cards.empty()) {
            if (index==0) return promo_cards[0];
            if (index==1) return fixed(kMyXbox,0);
            if (index==2) return promo_cards.size()>1?promo_cards[1]:fixed(kMyXbox,1);
            if (index==3) return promo_cards.size()>2?promo_cards[2]:fixed(kMyXbox,2);
            if (index>=4 && index<int(promo_cards.size())+1) return promo_cards[size_t(index-1)];
            if (index==4) return {"Game Videos",Kind::Hub,IconVideo,Goal::ToVideos,-1,-1,""};
            if (index==5) return {"Music Library",Kind::Hub,IconMusic,Goal::ToMusic,-1,-1,""};
            return {"My Avatar",Kind::Hub,IconAvatar,Goal::ToAvatar,-1,-1,""};
        }
        // soclua/MeViewScene.lub (15574/17559): ChangeTheme on the social Me page.
        if (metro_tab == 2) {
            return fixed(kMetroSocial,index);
        }
        if (metro_tab == 0) return {"Search Games & Apps", Kind::Hub, IconGames, Goal::ToSearch, -1, -1, ""};
        if (metro_tab == 7) {
            // epix/emb_dashhome.xml (15574), dashhome-offline.xml (17559), strings.xus.
            // Template6 / Template6HD: four columns, two rows. V2 swaps Profile/Preferences.
            // Ex-Box addition: no 360 original placement for Xbox101 / NetworkHelp; second page.
            const char *labels[]={"System","Preferences","Profile","Kinect","Account","Privacy","Family","Turn Off","Xbox Basics","Check My Network"};
            const char *icons[]={"ico_64x_console.png","ico_64x_SettingsPreferences.png","ico_64x_pic.png","ico_64x_kinect_device.png",
                                 "ico_64x_accountmanagement.png","ico_64x_privacy.png","ico_64x_family.png","ico_64x_TurnOff.png","xboxBasics.jpg","xboxNetwork.jpg"};
            int i=std::clamp(index,0,metro_v1()?9:7);
            if (!metro_v1() && (i==1 || i==2)) i=3-i;
            Card c{labels[i],Kind::Hub,i==2?IconAvatar:IconSettings,
                   i==0?Goal::ToSettings:i==2?Goal::ToProfile:i==7?Goal::ExitToPs5:Goal::Unavailable,-1,-1,""};
            const auto art=welcome_content.find(std::string("tile/")+icons[i]);
            if (art!=welcome_content.end()) c.image=art->second.art;
            return c;
        }
        if (channel == Media && metro_tab == 4) return fixed(kMedia, index == 0 ? 2 : 1);
        if (channel == Media && metro_tab == 5) {
            static constexpr Fixed music_dashboard[] = {
                {"Music Library", IconMusic, Goal::ToMusic},
                {"Random Album", IconMusic, Goal::ToMusicRandomAlbum},
                {"Most Played", IconMusic, Goal::ToMusicMostPlayed},
                {"Saved Playlists", IconMusic, Goal::ToMusicPlaylists},
                {"Recently Added", IconMusic, Goal::ToMusicRecentlyAdded},
                {"Albums", IconMusic, Goal::ToMusicAlbums},
                {"Now Playing", IconMusic, Goal::ToMusicNowPlaying},
            };
            Card c=fixed(music_dashboard,index);
            if(index==1)c.subtitle="Surprise me";
            return c;
        }
    }
    switch (channel)
    {
    case Friends:
        if (era==0 && promotions && ad_mode[current_ad_profile()]!=0 && index>=int(sizeof kFriends/sizeof kFriends[0]))
            return promo_cards[size_t(index-int(sizeof kFriends/sizeof kFriends[0]))];
        return fixed(kFriends, index);
    case Media: return fixed(kMedia, index);
    case Games:
    {
        // Installed games first, then homebrew the owner listed as a game.
        const std::vector<int> extra = game_homebrew();
        const int content = int(games.size() + extra.size());
        const int total = content + (has_rom_game_guide_preview()?1:0);
        if (total == 0)
        {
            Card c;
            c.title = "No Games Found";
            c.kind = Kind::Empty;
            return c;
        }
        index = std::clamp(index, 0, total - 1);
        if (index==content && has_rom_game_guide_preview())
            return {exbox_rom_guide::preview_title,Kind::Hub,IconGames,Goal::ToRomGameGuidePreview,-1,-1,""};
        if (index < int(games.size()))
            return game_card(games, game_order()[std::size_t(index)], artwork_choice);
        return app_card(apps, extra[std::size_t(index) - games.size()]);
    }
    case Apps:
    {
        const std::vector<int> list = app_homebrew();
        if (list.empty())
        {
            Card c;
            c.title = "No Homebrew Found";
            c.kind = Kind::Empty;
            return c;
        }
        return app_card(apps, list[std::size_t(std::clamp(index, 0, int(list.size()) - 1))]);
    }
    default:
    {
        const FixedList slots=my_xbox_slots(era);
        const int promo_index=index-slots.count-int(pins.size());
        if (promotions && era!=0 && promo_index>=0 && promo_index<int(promo_cards.size())) return promo_cards[size_t(promo_index)];
        // Front card, then attached apps and games, then the rest of My Xbox.
        const int pf = pin_first();
        if (index >= pf && index < pf + int(pins.size()))
        {
            const std::string &pin = pins[std::size_t(index - pf)];
            Card c;
            const int g = game_index(games, pin);
            const int a = g >= 0 ? -1 : app_index(apps, pin);
            if (g >= 0)
                c = game_card(games, g, artwork_choice);
            else if (a >= 0)
                c = app_card(apps, a);
            else
            {
                c.icon = IconGames;
                c.title = pin.rfind("title:", 0) == 0 ? "Unavailable Game" : "Unavailable App";
                c.kind = Kind::Missing;
                return c;
            }
            c.kind = Kind::Pinned;
            return c;
        }
        if (index >= pf + int(pins.size()))
            index -= int(pins.size());
        Card slot=fixed_slot(slots, index);
        if (slot.goal == Goal::ToSearch && !recent.empty())
        {
            // Game Library's line two: the last thing played.
            const int g = game_index(games, recent.front());
            const int a = g < 0 ? app_index(apps, recent.front()) : -1;
            if (g >= 0) slot.subtitle = games[std::size_t(g)].title;
            else if (a >= 0) slot.subtitle = apps[std::size_t(a)].title;
            else for(const auto&entry:games_frontend.entries)if(entry.key==recent.front()){slot.subtitle=entry.title+" - Xbox 360";break;}
        }
        return slot;
    }
    }
}

bool Model::current_is_title() const
{
    if (!content_focused()) return false;
    const Kind kind = current_card().kind;
    return kind == Kind::App || kind == Kind::Game || kind == Kind::Pinned;
}

void Model::select_metro_tab(int tab)
{
    metro_selected[std::clamp(metro_tab, 0, 7)] = selected[channel];
    metro_tab = std::clamp(tab, 0, 7);
    constexpr int channels[] = {MyXbox, MyXbox, Friends, Games, Media, Media, Apps, MyXbox};
    channel = channels[metro_tab];
    viewer = Viewer::None;
    viewer_full = false;
    selected[channel] = std::clamp(metro_selected[metro_tab], 0, count() - 1);
    metro_focus = selected[channel] % metro_page_size();
}

std::vector<Card> Model::library_cards() const
{
    std::vector<Card> out;
    if (collection == Collection::All)
    {
        for (int i : game_order()) out.push_back(game_card(games, i, artwork_choice));
        for (std::size_t i = 0; i < apps.size(); ++i) out.push_back(app_card(apps, int(i)));
        std::stable_sort(out.begin(), out.end(), [](const Card &a, const Card &b) { return title_less(a.title, b.title); });
        if (library_filter > 0)
            out.erase(std::remove_if(out.begin(), out.end(), [&](const Card &c) {
                const unsigned char first = c.title.empty() ? 0 : static_cast<unsigned char>(c.title[0]);
                return library_filter == 27 ? !std::isdigit(first) : std::toupper(first) != 'A' + library_filter - 1;
            }), out.end());
    }
    else
    {
        const auto &keys = collection == Collection::Pins ? pins : recent;
        for (const auto &key : keys)
        {
            const int g = game_index(games, key), a = app_index(apps, key);
            if (g >= 0) out.push_back(game_card(games, g, artwork_choice));
            else if (a >= 0) out.push_back(app_card(apps, a));
            else if(collection==Collection::Recent)for(const auto&entry:games_frontend.entries)if(entry.key==key){Card c;c.title=entry.title;c.kind=Kind::Missing;c.icon=IconGames;c.subtitle="Xbox 360 - open in PS5X360 (dispatch pending)";c.image=entry.owner_art_key;out.push_back(std::move(c));break;}
        }
    }
    return out;
}

Effect Model::open_collection(Collection which)
{
    collection = which;
    library_filter = 0;
    return open_viewer(Viewer::Library);
}

bool Model::pinned(const std::string &key) const
{
    return std::find(pins.begin(), pins.end(), key) != pins.end();
}

int Model::recency(const std::string &key) const
{
    auto it = std::find(recent.begin(), recent.end(), key);
    return it == recent.end() ? int(recent.size()) : int(it - recent.begin());
}

void Model::played(const std::string &key)
{
    if (key.empty())
        return;
    recent.erase(std::remove(recent.begin(), recent.end(), key), recent.end());
    recent.insert(recent.begin(), key);
    if (recent.size() > kMaxRecent)
        recent.resize(kMaxRecent);
}

void Model::record_notification(const std::string &title, const std::string &text)
{
    notification_history.insert(notification_history.begin(), {title, text});
    if (notification_history.size() > 10)
        notification_history.resize(10);
}

void Model::load_recent(const std::vector<std::string> &lines)
{
    recent.clear();
    for (const auto &line : lines)
    {
        const bool path = line.rfind("/data/homebrew/", 0) == 0 || line.rfind("/mnt/", 0) == 0;
        const bool title = line.rfind("title:", 0) == 0 && title_id_ok(line.substr(6));
        const bool xbox = exbox_games::identity_valid(line);
        if ((path || title || xbox) && recency(line) == int(recent.size()) && recent.size() < kMaxRecent)
            recent.push_back(line);
    }
}

// Most recently played first, the rest in their usual (name) order.
std::vector<int> Model::game_order() const
{
    std::vector<int> out(games.size());
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = int(i);
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        if (library_order == 1)
            return title_less(games[std::size_t(a)].title, games[std::size_t(b)].title);
        if (library_order == 2)
            return title_less(games[std::size_t(b)].title, games[std::size_t(a)].title);
        return recency(game_key(games[std::size_t(a)])) < recency(game_key(games[std::size_t(b)]));
    });
    return out;
}

std::vector<int> Model::game_homebrew() const
{
    std::vector<int> out;
    for (std::size_t i = 0; i < apps.size(); ++i)
        if (game_apps.count(apps[i].executable))
            out.push_back(int(i));
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        if (library_order == 1)
            return title_less(apps[std::size_t(a)].title, apps[std::size_t(b)].title);
        if (library_order == 2)
            return title_less(apps[std::size_t(b)].title, apps[std::size_t(a)].title);
        return recency(apps[std::size_t(a)].executable) < recency(apps[std::size_t(b)].executable);
    });
    return out;
}

std::vector<int> Model::app_homebrew() const
{
    std::vector<int> out;
    for (std::size_t i = 0; i < apps.size(); ++i)
        if (!game_apps.count(apps[i].executable))
            out.push_back(int(i));
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        if (library_order == 1)
            return title_less(apps[std::size_t(a)].title, apps[std::size_t(b)].title);
        if (library_order == 2)
            return title_less(apps[std::size_t(b)].title, apps[std::size_t(a)].title);
        return recency(apps[std::size_t(a)].executable) < recency(apps[std::size_t(b)].executable);
    });
    return out;
}

void Model::load_categories(const std::vector<std::string> &lines)
{
    game_apps.clear();
    for (const auto &line : lines)
        if (line.rfind("/data/homebrew/", 0) == 0 || line.rfind("/mnt/", 0) == 0)
            game_apps.insert(line);
    clamp_all();
}

void Model::open_info(const Card &card)
{
    info = true;
    info_item = 0;
    if (card.kind == Kind::Game || (card.kind == Kind::Pinned && card.game >= 0))
        info_target = {game_key(games[std::size_t(card.game)]), card.title};
    else if (card.app >= 0)
        info_target = {apps[std::size_t(card.app)].executable, card.title};
    else
        info_target = {pins[std::size_t(current() - pin_first())], card.title}; // a missing attachment
}

Card Model::info_card() const
{
    const int g = game_index(games, info_target.key);
    if (g >= 0)
        return game_card(games, g, artwork_choice);
    const int a = app_index(apps, info_target.key);
    if (a >= 0)
        return app_card(apps, a);
    Card c;
    c.title = info_target.title;
    c.kind = Kind::Missing;
    return c;
}

std::vector<InfoEntry> Model::info_entries() const
{
    std::vector<InfoEntry> out;
    const Card c = info_card();
    const bool attached = pinned(info_target.key);
    if (c.kind != Kind::Missing)
        out.push_back({c.kind == Kind::Game ? "Play" : "Open", InfoAction::Play});
    out.push_back(attached ? InfoEntry{"Detach from My Xbox", InfoAction::Detach}
                           : InfoEntry{"Attach to My Xbox", InfoAction::Attach});
    if (attached && pins.size() > 1)
    {
        const auto at = std::find(pins.begin(), pins.end(), info_target.key) - pins.begin();
        if (at > 0)
            out.push_back({"Move Earlier on My Xbox", InfoAction::MoveEarlier});
        if (at + 1 < int(pins.size()))
            out.push_back({"Move Later on My Xbox", InfoAction::MoveLater});
    }
    if (c.kind == Kind::App)
        out.push_back(game_apps.count(info_target.key) ? InfoEntry{"Show in Apps", InfoAction::ToApps}
                                                       : InfoEntry{"Show in Games", InfoAction::ToGames});
    if (c.kind == Kind::Game)
        out.push_back({"Play Trailer", InfoAction::PlayTrailer});
    if (!trailers.empty() && c.kind != Kind::Missing)
    {
        out.push_back({"Trailer: " + trailer_label(info_target.key), InfoAction::CycleTrailer});
    }
    if (c.kind == Kind::Game)
    {
        out.push_back({"Artwork: " + artwork_label(games[std::size_t(c.game)].title_id), InfoAction::CycleArtwork});
        out.push_back({"Download Xbox Cover", InfoAction::DownloadCover});
        out.push_back({"Refresh Covers and Trailers", InfoAction::RefreshMedia});
        out.push_back({"Game Update: PS5 Home > Options", InfoAction::GameUpdates});
    }
    return out;
}

std::string Model::artwork_label(const std::string &title_id) const
{
    const int g = game_index(games, "title:" + title_id);
    if (g < 0 || games[std::size_t(g)].xbox_cover.empty())
        return "PlayStation";
    const auto it = artwork_choice.find(title_id);
    return it == artwork_choice.end() ? "Automatic" : it->second == 1 ? "PlayStation" : "Xbox Cover";
}

void Model::step_artwork(const std::string &title_id, int step)
{
    const int g = game_index(games, "title:" + title_id);
    if (g < 0 || games[std::size_t(g)].xbox_cover.empty())
        return;
    const auto it = artwork_choice.find(title_id);
    const int at = it == artwork_choice.end() ? 0 : it->second;
    const int next = ((at + step) % 3 + 3) % 3;
    if (next == 0)
        artwork_choice.erase(title_id);
    else
        artwork_choice[title_id] = next;
}

std::string Model::trailer_label(const std::string &key) const
{
    auto it = trailer_choice.find(key);
    if (it == trailer_choice.end())
        return "Automatic";
    if (it->second.empty())
        return "None";
    auto named = trailer_names.find(it->second);
    if (named != trailer_names.end() && !named->second.empty())
        return named->second;
    // Trailer keys are lower-case titles: show them in title case.
    std::string name = it->second;
    bool start = true;
    for (char &ch : name)
    {
        if (start)
            ch = char(std::toupper(static_cast<unsigned char>(ch)));
        start = ch == ' ';
    }
    return name;
}

void Model::step_trailer(const std::string &key, int step)
{
    // Positions: 0 = Automatic, 1..n = each trailer, n + 1 = None.
    const int n = int(trailers.size());
    const int span = n + 2;
    int at = 0;
    auto it = trailer_choice.find(key);
    if (it != trailer_choice.end())
    {
        if (it->second.empty())
            at = n + 1;
        else
        {
            auto pos = std::find(trailers.begin(), trailers.end(), it->second);
            at = pos == trailers.end() ? 0 : int(pos - trailers.begin()) + 1;
        }
    }
    at = ((at + step) % span + span) % span;
    if (at == 0)
        trailer_choice.erase(key);
    else if (at == n + 1)
        trailer_choice[key] = "";
    else
        trailer_choice[key] = trailers[std::size_t(at - 1)];
}

Effect Model::press_info(xb::Button button)
{
    const std::vector<InfoEntry> entries = info_entries();
    const int items = int(entries.size());
    info_item = std::clamp(info_item, 0, std::max(0, items - 1));
    switch (button)
    {
    case xb::Up: info_item = std::max(0, info_item - 1); return {};
    case xb::Down: info_item = std::min(items - 1, info_item + 1); return {};
    case xb::Left:
    case xb::Right:
        // Left and Right step through trailers on the Trailer row.
        if (items == 0)
            return {};
        if (entries[std::size_t(info_item)].action == InfoAction::CycleTrailer)
        {
            step_trailer(info_target.key, button == xb::Right ? 1 : -1);
            return {Command::SaveTrailers, -1};
        }
        if (entries[std::size_t(info_item)].action == InfoAction::CycleArtwork)
        {
            const Card c = info_card();
            const App &game = games[std::size_t(c.game)];
            if (game.xbox_cover.empty()) return {};
            step_artwork(game.title_id, button == xb::Right ? 1 : -1);
            return {Command::SaveArtwork, -1};
        }
        return {};
    case xb::B:
    case xb::Y: info = false; return {};
    case xb::Guide:
    case xb::Start:
        info = false;
        open_guide();
        return {};
    case xb::A: break;
    default: return {};
    }
    if (items == 0)
        return {};
    const std::string key = info_target.key;
    switch (entries[std::size_t(info_item)].action)
    {
    case InfoAction::Play:
    {
        info = false;
        const Card c = info_card();
        return c.kind == Kind::Game ? Effect{Command::LaunchTitle, c.game} : Effect{Command::Launch, c.app};
    }
    case InfoAction::Attach:
    case InfoAction::Detach:
    {
        const bool was_on_it = channel == MyXbox && current() >= pin_first() && current() < pin_first() + int(pins.size()) &&
                               pins[std::size_t(current() - pin_first())] == key;
        toggle_pin(key);
        if (was_on_it)
            info = false; // the card it was opened from has gone
        return {Command::SavePins, -1};
    }
    case InfoAction::MoveEarlier:
    case InfoAction::MoveLater:
    {
        const int step = entries[std::size_t(info_item)].action == InfoAction::MoveEarlier ? -1 : 1;
        const int at = int(std::find(pins.begin(), pins.end(), key) - pins.begin());
        std::swap(pins[std::size_t(at)], pins[std::size_t(at + step)]);
        // On My Xbox the selection follows the card it moved.
        if (channel == MyXbox && current() == at + pin_first())
            selected[MyXbox] += step;
        info_item = std::clamp(info_item, 0, int(info_entries().size()) - 1);
        return {Command::SavePins, -1};
    }
    case InfoAction::CycleTrailer:
    {
        // Automatic -> each trailer in turn -> None -> Automatic.
        step_trailer(key, 1);
        return {Command::SaveTrailers, -1};
    }
    case InfoAction::CycleArtwork:
    {
        const Card c = info_card();
        const App &game = games[std::size_t(c.game)];
        if (game.xbox_cover.empty()) return {};
        step_artwork(game.title_id, 1);
        return {Command::SaveArtwork, -1};
    }
    case InfoAction::RefreshMedia:
        return {Command::RefreshMedia, -1};
    case InfoAction::DownloadCover:
        return {Command::RequestCover, info_card().game};
    case InfoAction::PlayTrailer:
        return {Command::PlayTrailer, -1};
    case InfoAction::GameUpdates:
        info = false;
        return {Command::ExitToPs5, -1};
    case InfoAction::ToGames:
    case InfoAction::ToApps:
    {
        if (entries[std::size_t(info_item)].action == InfoAction::ToGames)
            game_apps.insert(key);
        else
            game_apps.erase(key);
        // The card leaves the channel it was on unless that is My Xbox.
        if (channel == Games || channel == Apps)
            info = false;
        clamp_all();
        return {Command::SaveCategories, -1};
    }
    }
    return {};
}

void Model::go(int channel_index)
{
    viewer = Viewer::None; // the hub, not a picture or video, is in front again
    viewer_full = false;
    channel = std::clamp(channel_index, 0, ChannelCount - 1);
    constexpr int tabs[] = {2, 4, 3, 6, 1, 1};
    metro_tab = tabs[channel];
    selected[channel] = std::clamp(selected[channel], 0, count() - 1);
    metro_focus = selected[channel] % metro_page_size();
}

void Model::clamp_all()
{
    const int keep = channel, active_selected=selected[channel];
    for (int c = 0; c < ChannelCount; ++c)
    {
        channel = c;
        const int n=era==0?count():base_count();
        selected[c] = std::clamp(selected[c], 0, n - 1);
    }
    channel = keep;
    selected[channel] = std::clamp(active_selected, 0, count() - 1);
    if (era==3 && metro_focus>=0 && metro_focus<metro_page_size())
        metro_focus=selected[channel]%metro_page_size();
}

void Model::set_apps(std::vector<App> found)
{
    apps = std::move(found);
    restore_context_focus();
    clamp_all();
}

void Model::set_games(std::vector<App> found)
{
    games = std::move(found);
    refresh_game_frontend();
    restore_context_focus();
    clamp_all();
}

std::string Model::saved_dashboard_context() const
{
    exbox_activity::Context c;c.channel=channel;c.metro_tab=metro_tab;c.viewer=int(viewer);c.viewer_item=viewer_item;c.music_view=music_view;c.music_page=music_page;c.music_group=music_group;
    if(viewer==Viewer::None&&content_focused()){const Card card=current_card();if(card.game>=0&&card.game<int(games.size()))c.focus_key=game_key(games[std::size_t(card.game)]);else if(card.app>=0&&card.app<int(apps.size()))c.focus_key=apps[std::size_t(card.app)].executable;}
    return exbox_activity::encode(c);
}

void Model::load_dashboard_context(const std::string &line)
{
    exbox_activity::Context c;if(!exbox_activity::decode(line,c))return;channel=c.channel;metro_tab=c.metro_tab;viewer_item=c.viewer_item;music_view=c.music_view;music_page=c.music_page;music_group=c.music_group;context_focus_key=c.focus_key;
    const Viewer requested=Viewer(c.viewer);viewer=requested==Viewer::Music||requested==Viewer::Library?requested:Viewer::None;viewer_full=false;guide=false;
}

void Model::restore_context_focus()
{
    if(context_focus_key.empty())return;for(std::size_t i=0;i<games.size();++i)if(game_key(games[i])==context_focus_key){channel=Games;for(std::size_t row=0;row<game_order().size();++row)if(game_order()[row]==int(i)){selected[Games]=int(row);context_focus_key.clear();return;}}
    for(std::size_t i=0;i<apps.size();++i)if(apps[i].executable==context_focus_key){const auto list=channel==Games?game_homebrew():app_homebrew();for(std::size_t row=0;row<list.size();++row)if(list[row]==int(i)){selected[channel]=int(row);context_focus_key.clear();return;}}
}

void Model::set_game_frontend_inventory(GameFolderInventory found)
{
    games_frontend_inventory = std::move(found);
    games_frontend_inventory_published = true;
    refresh_game_frontend();
}

void Model::refresh_game_frontend()
{
    std::vector<exbox_games::Entry> entries;
    games_frontend_rejected=0;
    games_frontend_limited=games_frontend_inventory.limited;
    try {
        auto xbox=exbox_games::build_library(games_frontend_inventory.source,games_frontend_inventory.folders);
        games_frontend_rejected=xbox.rejected;
        entries=std::move(xbox.entries);
    } catch (const std::invalid_argument &) {
        games_frontend_rejected=games_frontend_inventory.folders.size();
    }
    std::vector<exbox_games::InstalledTitle> installed;
    installed.reserve(games.size());
    for (const auto &game:games)
        installed.push_back({game.title_id,game.title,game.icon});
    auto ps5=exbox_games::installed_entries(installed);
    entries.insert(entries.end(),std::make_move_iterator(ps5.begin()),std::make_move_iterator(ps5.end()));
    games_frontend.entries=std::move(entries);
    games_frontend.refresh(); // preserves the selected stable key when it still exists
}

std::vector<std::string> Model::saved_game_frontend_settings() const
{
    return {"games_frontend_filter="+std::to_string(int(games_frontend.filter)),
            "games_frontend_show_xbox="+std::to_string(games_frontend.show_xbox?1:0),
            "games_frontend_show_ps5="+std::to_string(games_frontend.show_ps5?1:0)};
}

void Model::load_game_frontend_setting(const std::string &line)
{
    auto one=[](const std::string &text,const char *prefix,int &value) {
        const std::string p(prefix);if(text.rfind(p,0)!=0||text.size()!=p.size()+1||text.back()<'0'||text.back()>'9')return false;
        value=text.back()-'0';return true;
    };
    int value=0;
    if(one(line,"games_frontend_filter=",value)&&value<=2)games_frontend.filter=exbox_games::Filter(value);
    else if(one(line,"games_frontend_show_xbox=",value)&&value<=1)games_frontend.show_xbox=value!=0;
    else if(one(line,"games_frontend_show_ps5=",value)&&value<=1)games_frontend.show_ps5=value!=0;
    else return;
    games_frontend.refresh();
}

void Model::load_pins(const std::vector<std::string> &lines)
{
    pins.clear();
    for (const auto &line : lines)
    {
        if (pins.size() >= kMaxPins)
            break;
        const bool path = line.rfind("/data/homebrew/", 0) == 0 || line.rfind("/mnt/", 0) == 0;
        const bool title = line.rfind("title:", 0) == 0 && title_id_ok(line.substr(6));
        if ((path || title) && !pinned(line))
            pins.push_back(line);
    }
    clamp_all();
}

void Model::toggle_pin(const std::string &key)
{
    auto it = std::find(pins.begin(), pins.end(), key);
    if (it != pins.end())
        pins.erase(it);
    else if (pins.size() < kMaxPins)
        pins.push_back(key);
    clamp_all();
}

void Model::show(const std::string &title, const std::string &text)
{
    details = true;
    details_title = title;
    details_text = text;
}

void Model::launch_failed(const std::string &title)
{
    show(title, "Could not open this. Press X to refresh and try again.");
}

Effect Model::press(xb::Button button)
{
    if (controller_map)
    {
        // Every other button lights up on the map; only B leaves it.
        if (button == xb::B)
            controller_map = false;
        return {};
    }
    if (guide)
        return press_guide(button);
    if (details)
    {
        // OK, Back, and the controller's Back (View / Create) button all close it.
        if (button == xb::A || button == xb::B || button == xb::Back)
            details = false;
        else if (button == xb::Guide || button == xb::Start)
        {
            details = false;
            open_guide();
        }
        return {};
    }
    if (info)
        return press_info(button);
    if (viewer != Viewer::None)
        return press_viewer(button);
    return press_hub(button);
}

// An opened group's options (music/Strings.xus): Play Album / Artist / Genre /
// Saved Playlist, Add to Current Playlist, then Choose Cover for an album
// (Ex-Box addition) or Delete Saved Playlist.
std::vector<std::string> Model::music_group_actions() const
{
    if (music_view <= 0 || music_group < 0)
        return {};
    constexpr const char *kPlay[] = {"", "Play Album", "Play Artist", "Play Genre", "Play Saved Playlist"};
    std::vector<std::string> out{kPlay[std::clamp(music_view, 1, 4)], "Add to Current Playlist"};
    if (music_view == 1)
        {out.push_back("Choose Cover");out.push_back("Edit Album Info");}
    if (music_view == 4)
        out.push_back("Delete Saved Playlist");
    return out;
}

std::vector<std::string> Model::music_page_rows() const
{
    switch (music_page)
    {
    case MusicAlbumInfo: return {"Album: "+music_album_fields[0],"Artist: "+music_album_fields[1],
                                "Genre: "+music_album_fields[2],"Save Changes"}; // 1025_EditAlbumInfo
    case MusicSong: return {"Play Song", "Add to Current Playlist", "Choose Cover"};
    case MusicNowPlaying: return {"Edit or Save Playlist"};
    case MusicEditPlaylist: return {"Save Playlist", "Clear All", "Done"};
    case MusicConfirmDelete: return {"Delete Saved Playlist", "Cancel"};
    default: return {};
    }
}

int Model::music_rows() const
{
    switch (music_page)
    {
    case MusicLibrary: return songs;
    case MusicNowPlaying:
    case MusicEditPlaylist: return int(music_page_rows().size()) + music_queue;
    case MusicChooseCover: return music_covers;
    default: return int(music_page_rows().size());
    }
}

void Model::album_save_result(bool ok)
{
    if (!ok) {show("Edit Album Info","The album info could not be saved.");return;} // Ex-Box addition: no 360 original error
    music_album_original=music_album_fields;music_album_field=-1;music_album_changed=0;
    music_page=MusicLibrary;viewer_item=music_library_item;
}

// Ex-Box addition: no 360 original. A compact controller keyboard for album fields.
Effect Model::press_album_keyboard(xb::Button button)
{
    const int count=int(std::char_traits<char>::length(music_album_keys));
    switch (button) {
    case xb::Left: music_album_key=(music_album_key+count-1)%count; break;
    case xb::Right: music_album_key=(music_album_key+1)%count; break;
    case xb::Up: music_album_key=std::max(0,music_album_key-10); break;
    case xb::Down: music_album_key=std::min(count-1,music_album_key+10); break;
    case xb::A:
        if (music_album_input.size()<128) music_album_input+=music_album_keys[music_album_key];
        break;
    case xb::X:
        if (!music_album_input.empty()) {
            size_t at=music_album_input.size()-1;
            while (at>0 && (static_cast<unsigned char>(music_album_input[at])&0xc0)==0x80) --at;
            music_album_input.resize(at);
        }
        break;
    case xb::Y:
        music_album_changed|=1u<<unsigned(music_album_field);
        music_album_fields[size_t(music_album_field)]=music_album_input;
        music_album_field=-1;return {Command::MusicBrowse,-1};
    case xb::B: music_album_field=-1;break;
    case xb::Guide:
    case xb::Start: open_guide();break;
    default: break;
    }
    return {};
}

// The pages over the Music Library.
Effect Model::press_music_page(xb::Button button)
{
    music_tabs_focused = false; // Non-library pages have their own focus.
    if (music_page==MusicAlbumInfo && music_album_field>=0) return press_album_keyboard(button);
    const int n = music_rows();
    const int last = std::max(0, n - 1);
    viewer_item = std::clamp(viewer_item, 0, last);
    const int fixed = int(music_page_rows().size());
    auto library = [this] {
        music_page = MusicLibrary;
        viewer_item = music_library_item;
        return Effect{Command::MusicBrowse, -1};
    };
    auto edit = [this](MusicOp op, int target) {
        music_target = target;
        return Effect{Command::MusicEdit, int(op)};
    };
    switch (button)
    {
    case xb::Up: viewer_item = std::max(0, viewer_item - 1); return {};
    case xb::Down: viewer_item = std::min(last, viewer_item + 1); return {};
    case xb::Guide:
    case xb::Start: open_guide(); return {};
    default: break;
    }
    switch (music_page)
    {
    case MusicAlbumInfo:
        if (button==xb::B) {music_album_fields=music_album_original;music_album_changed=0;return library();}
        if (button!=xb::A) return {};
        if (viewer_item==3) return edit(MusicOp::SaveAlbumInfo,-1);
        music_album_field=viewer_item;music_album_key=0;
        music_album_input=music_album_fields[size_t(viewer_item)];
        return {};
    case MusicSong:
        if (button == xb::B)
            return library();
        if (button != xb::A)
            return {};
        if (viewer_item == 0)
        {
            music_paused = false;
            music_page = MusicLibrary;
            viewer_item = music_library_item;
            return edit(MusicOp::PlaySong, music_library_item);
        }
        if (viewer_item == 1)
        {
            music_page = MusicLibrary;
            viewer_item = music_library_item;
            return edit(MusicOp::AddSong, music_library_item);
        }
        music_cover_from = MusicSong;
        music_page = MusicChooseCover;
        viewer_item = 0;
        return edit(MusicOp::OpenCover, music_library_item);
    case MusicChooseCover:
        if (button == xb::B)
        {
            if (music_cover_from == MusicSong)
            {
                music_page = MusicSong;
                viewer_item = 2;
                return {Command::MusicBrowse, -1};
            }
            return library();
        }
        if (button == xb::X)
        {
            library();
            return edit(MusicOp::ClearCover, -1);
        }
        if (button == xb::A && n > 0)
        {
            const int picture = viewer_item;
            library();
            return edit(MusicOp::SetCover, picture);
        }
        return {};
    case MusicNowPlaying:
        switch (button)
        {
        case xb::B: return library();
        case xb::A:
            if (viewer_item < fixed)
            {
                music_page = MusicEditPlaylist;
                viewer_item = 0;
                return {Command::MusicBrowse, -1};
            }
            music_paused = false;
            return edit(MusicOp::Jump, viewer_item - fixed);
        case xb::X:
            if (now_playing < 0)
                return {};
            music_paused = !music_paused;
            return {Command::MusicPause, -1};
        case xb::Y:
            if (now_playing < 0)
                return {};
            now_playing = -1;
            music_paused = false;
            return {Command::MusicStop, -1};
        case xb::Left: return now_playing >= 0 ? Effect{Command::MusicPrevious, -1} : Effect{};
        case xb::Right: return now_playing >= 0 ? Effect{Command::MusicNext, -1} : Effect{};
        default: return {};
        }
    case MusicEditPlaylist:
    {
        const bool song_row = viewer_item >= fixed && viewer_item < n;
        const int song = viewer_item - fixed;
        switch (button)
        {
        case xb::B:
            music_page = MusicNowPlaying;
            viewer_item = 0;
            return {Command::MusicBrowse, -1};
        case xb::A:
            if (viewer_item == 0)
                return edit(MusicOp::SavePlaylist, -1);
            if (viewer_item == 1)
            {
                now_playing = -1;
                music_paused = false;
                music_queue = 0;
                music_page = MusicNowPlaying;
                viewer_item = 0;
                return edit(MusicOp::ClearPlaylist, -1);
            }
            if (viewer_item == 2)
            {
                music_page = MusicNowPlaying;
                viewer_item = 0;
                return {Command::MusicBrowse, -1};
            }
            return {};
        case xb::X:
            if (!song_row)
                return {};
            if (music_queue <= 1)
            {
                now_playing = -1;
                music_paused = false;
            }
            music_queue = std::max(0, music_queue - 1);
            viewer_item = std::clamp(viewer_item, 0, std::max(0, fixed + music_queue - 1));
            return edit(MusicOp::Remove, song);
        case xb::LB:
            if (!song_row || song == 0)
                return {};
            --viewer_item;
            return edit(MusicOp::MoveUp, song);
        case xb::RB:
            if (!song_row || song + 1 >= music_queue)
                return {};
            ++viewer_item;
            return edit(MusicOp::MoveDown, song);
        default: return {};
        }
    }
    case MusicConfirmDelete:
        if (button == xb::B || (button == xb::A && viewer_item == 1))
            return library();
        if (button == xb::A)
        {
            const int group = music_group;
            music_page = MusicLibrary;
            music_group = -1;
            viewer_item = std::max(0, group);
            return edit(MusicOp::DeletePlaylist, group);
        }
        return {};
    default:
        return library();
    }
}

Effect Model::open_viewer(Viewer which)
{
    viewer = which;
    video_pending.clear();
    viewer_item = 0;
    viewer_full = false;
    if (which == Viewer::Music)
    {
        music_tabs_focused = false;
        music_group = -1; // the view chosen last time stays; an opened album does not
        music_page = MusicLibrary;
    }
    if (which == Viewer::Pictures)
    {
        picture_group = -1; // as Music: the sort stays, an opened game does not
        reset_picture_view();
    }
    return which == Viewer::Pictures ? Effect{Command::ScanPictures, -1}
           : which == Viewer::Music  ? Effect{Command::ScanMusic, -1}
                                     : Effect{};
}

PictureFile picture_file(const std::string &path)
{
    PictureFile out;
    out.path = path;
    const std::string mark = "/av_contents/photo/";
    const size_t at = path.find(mark);
    if (at != std::string::npos)
    {
        const size_t from = at + mark.size(), to = path.find('/', from);
        if (to != std::string::npos)
            out.title_id = path.substr(from, to - from);
    }
    const size_t slash = path.rfind('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    bool stamped = name.size() >= 15 && name[8] == '_';
    for (size_t i = 0; stamped && i < 15; ++i)
        stamped = i == 8 || std::isdigit(static_cast<unsigned char>(name[i]));
    out.taken = stamped ? name.substr(0, 15) : name;
    return out;
}

std::string picture_date(const std::string &taken)
{
    if (taken.size() < 13 || taken[8] != '_')
        return taken;
    return taken.substr(0, 4) + "-" + taken.substr(4, 2) + "-" + taken.substr(6, 2) + "  " + taken.substr(9, 2) + ":" +
           taken.substr(11, 2);
}

namespace
{
std::string folded(std::string text)
{
    for (char &ch : text)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}
// Dated names first (the PS5's own), newest first; then any others.
bool newer(const PictureFile &a, const PictureFile &b)
{
    const bool da = a.taken.size() == 15 && a.taken[8] == '_', db = b.taken.size() == 15 && b.taken[8] == '_';
    if (da != db)
        return da;
    return a.taken != b.taken ? a.taken > b.taken : a.path > b.path;
}
} // namespace

void Model::set_pictures(std::vector<PictureFile> found)
{
    const PictureFile *was = picture();
    const std::string path = was ? was->path : std::string();
    const std::string group = picture_group >= 0 && was ? was->label() : std::string();
    picture_visited.clear();
    picture_background_confirm = false;
    picture_background_pending.clear();
    picture_rotation = 0;
    picture_files = std::move(found);
    pictures = int(picture_files.size());
    if (picture_group >= 0)
    {
        const auto games = picture_games();
        const auto it = std::find(games.begin(), games.end(), group);
        picture_group = it == games.end() ? -1 : int(it - games.begin());
        if (picture_group < 0)
            viewer_full = false; // that game's pictures are all gone
    }
    if (picture_groups())
    {
        viewer_item = std::clamp(viewer_item, 0, std::max(0, viewer_count() - 1));
        return;
    }
    const auto list = picture_list();
    int to = -1;
    for (size_t i = 0; i < list.size() && to < 0; ++i)
        if (picture_files[size_t(list[i])].path == path)
            to = int(i);
    viewer_item = to >= 0 ? to : std::clamp(viewer_item, 0, std::max(0, int(list.size()) - 1));
    if (list.empty())
        viewer_full = false;
}

std::vector<std::string> Model::picture_games() const
{
    std::vector<std::string> out;
    for (const auto &file : picture_files)
        if (std::find(out.begin(), out.end(), file.label()) == out.end())
            out.push_back(file.label());
    std::stable_sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) { return folded(a) < folded(b); });
    return out;
}

std::vector<int> Model::picture_group_list(int group) const
{
    std::vector<int> out;
    const auto games = picture_games();
    if (group < 0 || group >= int(games.size()))
        return out;
    for (size_t i = 0; i < picture_files.size(); ++i)
        if (picture_files[i].label() == games[size_t(group)])
            out.push_back(int(i));
    std::stable_sort(out.begin(), out.end(), [this](int a, int b) { return newer(picture_files[size_t(a)], picture_files[size_t(b)]); });
    return out;
}

std::vector<int> Model::picture_list() const
{
    if (picture_order == kPictureGames)
        return picture_group_list(picture_group);
    std::vector<int> out(picture_files.size());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = int(i);
    const auto &files = picture_files;
    if (picture_order == 1)
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return newer(files[size_t(b)], files[size_t(a)]); });
    else if (picture_order == 2)
    {
        // By game, A-Z, and newest first within each.
        std::vector<std::string> keys;
        for (const auto &file : files)
            keys.push_back(folded(file.label()));
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
            return keys[size_t(a)] != keys[size_t(b)] ? keys[size_t(a)] < keys[size_t(b)] : newer(files[size_t(a)], files[size_t(b)]);
        });
    }
    else
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return newer(files[size_t(a)], files[size_t(b)]); });
    return out;
}

const PictureFile *Model::picture() const
{
    const int item = std::max(0, viewer_item);
    if (picture_groups())
    {
        const auto list = picture_group_list(item);
        return list.empty() ? nullptr : &picture_files[size_t(list.front())];
    }
    const auto list = picture_list();
    if (list.empty())
        return nullptr;
    return &picture_files[size_t(list[size_t(std::min(item, int(list.size()) - 1))])];
}

void Model::reset_picture_view()
{
    picture_slideshow = false;
    picture_control = -1;
    picture_rotation = 0;
    picture_visited.clear();
    picture_background_confirm = false;
    picture_background_pending.clear();
    picture_zoom = 0;
    picture_pan_x = picture_pan_y = 0;
}

// pictures/Strings.xus (9199), also used by the later slideshow scenes.
const char *Model::picture_control_help() const
{
    switch (picture_control) {
    case 1: return "Go to Previous Image";
    case 2: return "Stop";
    case 3: return "Go to Next Image";
    case 4: return picture_shuffle ? "Turn Slideshow Shuffle Off" : "Turn Slideshow Shuffle On";
    case 5: return picture_repeat ? "Turn Slideshow Repeat Off" : "Turn Slideshow Repeat On";
    case 6: return "Rotate Image Clockwise";
    case 7: return "Rotate Image Counter-Clockwise";
    default: return picture_slideshow ? "Pause" : "Play";
    }
}

std::vector<std::string> Model::saved_picture_settings() const
{
    return {"picture_order=" + std::to_string(picture_order),
            "picture_repeat=" + std::to_string(picture_repeat ? 1 : 0),
            "picture_shuffle=" + std::to_string(picture_shuffle ? 1 : 0),
            "picture_background=" + std::to_string(picture_background),
            "picture_background_enabled=" + std::to_string(picture_background_enabled)};
}

void Model::load_picture_setting(std::string line)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return;
    const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    if (value.size() != 1 || value[0] < '0' || value[0] > '9') return;
    const int n = value[0] - '0';
    if (key == "picture_order" && n < kPictureOrderCount) picture_order = n;
    if (key == "picture_repeat" && n <= 1) picture_repeat = n != 0;
    if (key == "picture_shuffle" && n <= 1) picture_shuffle = n != 0;
    if (key == "picture_background" && n <= 2) picture_background = n;
    if (key == "picture_background_enabled" && n <= 1) picture_background_enabled = n != 0;
}

bool Model::slideshow_step()
{
    if (viewer != Viewer::Pictures || !viewer_full || !picture_slideshow || guide || info || details || picture_background_confirm)
        return false;
    const int n = viewer_count();
    if (n < 2) { picture_slideshow = false; return false; }
    if (picture_shuffle) {
        if (picture_visited.empty()) picture_visited.push_back(viewer_item);
        if (int(picture_visited.size()) >= n) {
            if (!picture_repeat) { picture_slideshow = false; return false; }
            picture_visited.clear();
        }
        std::vector<int> remaining;
        for (int i = 0; i < n; ++i)
            if (i != viewer_item && std::find(picture_visited.begin(), picture_visited.end(), i) == picture_visited.end())
                remaining.push_back(i);
        if (remaining.empty()) { picture_slideshow = false; return false; }
        picture_random = picture_random * 1664525u + 1013904223u;
        viewer_item = remaining[size_t(picture_random % unsigned(remaining.size()))];
        picture_visited.push_back(viewer_item);
    } else {
        if (viewer_item + 1 >= n && !picture_repeat) { picture_slideshow = false; return false; }
        viewer_item = (std::clamp(viewer_item, 0, n - 1) + 1) % n;
    }
    picture_rotation = picture_zoom = 0;
    picture_pan_x = picture_pan_y = 0;
    return true;
}

Effect Model::press_picture_full(xb::Button button)
{
    if (picture_background_confirm) {
        if (button == xb::B) { picture_background_confirm = false; picture_background_pending.clear(); }
        if (button == xb::Up || button == xb::Left) picture_background_yes = true;
        if (button == xb::Down || button == xb::Right) picture_background_yes = false;
        if (button == xb::A) {
            picture_background_confirm = false;
            if (picture_background_yes && picture_background_available() && !picture_background_pending.empty())
                return {Command::ApplyPictureBackground, -1};
            picture_background_pending.clear();
        }
        return {};
    }
    const int last = std::max(0, viewer_count() - 1);
    const bool zoomed = picture_zoom > 0;
    auto step_to = [this](int to) {
        if (to == viewer_item)
            return Effect{};
        viewer_item = to;
        picture_rotation = 0;
        picture_visited.clear();
        picture_zoom = 0; // each picture opens whole
        picture_pan_x = picture_pan_y = 0;
        return Effect{};
    };
    if (picture_control >= 0) {
        if (button == xb::Left || button == xb::Right) {
            picture_control = (picture_control + (button == xb::Right ? 1 : 7)) % 8;
            return {};
        }
        if (button == xb::Up || button == xb::B) { picture_control = -1; return {}; }
        if (button == xb::A) {
            switch (picture_control) {
            case 1: return step_to(std::max(0, viewer_item - 1));
            case 2: picture_slideshow = false; picture_visited.clear(); return {};
            case 3: return step_to(std::min(last, viewer_item + 1));
            case 4: picture_shuffle = !picture_shuffle; picture_visited.clear(); return {Command::SaveSettings, -1};
            case 5: picture_repeat = !picture_repeat; return {Command::SaveSettings, -1};
            case 6:
            case 7:
                picture_rotation = (picture_rotation + (picture_control == 6 ? 1 : 3)) % 4;
                picture_slideshow = false;
                picture_zoom = picture_pan_x = picture_pan_y = 0;
                return {};
            default: break;
            }
        }
    }
    // Each press moves a zoomed picture about a tenth of the screen.
    const int pan = zoomed ? std::max(1, int(200.0f / (kPictureZooms[picture_zoom] - 1.0f))) : 0;
    switch (button)
    {
    case xb::B:
        viewer_full = false;
        picture_info = false;
        reset_picture_view();
        return {};
    case xb::LB: return step_to(std::max(0, viewer_item - 1));
    case xb::RB: return step_to(std::min(last, viewer_item + 1));
    case xb::Left:
    case xb::Right:
        if (!zoomed)
            return step_to(std::clamp(viewer_item + (button == xb::Right ? 1 : -1), 0, last));
        picture_pan_x = std::clamp(picture_pan_x + (button == xb::Right ? pan : -pan), -kPicturePan, kPicturePan);
        return {};
    case xb::Up:
    case xb::Down:
        if (!zoomed && button == xb::Down) picture_control = 0;
        if (zoomed)
            picture_pan_y = std::clamp(picture_pan_y + (button == xb::Down ? pan : -pan), -kPicturePan, kPicturePan);
        return {};
    case xb::RT:
    case xb::LT:
        picture_zoom = std::clamp(picture_zoom + (button == xb::RT ? 1 : -1), 0, kPictureZoomCount - 1);
        if (picture_zoom == 0)
            picture_pan_x = picture_pan_y = 0;
        else
            picture_slideshow = false; // a closer look stops the show
        return {};
    case xb::A:
    case xb::X:
        picture_slideshow = !picture_slideshow && last > 0;
        if (picture_slideshow) {
            if (!picture_repeat && !picture_shuffle && viewer_item == last) {
                viewer_item = 0;
                picture_rotation = 0;
            }
            if (int(picture_visited.size()) >= viewer_count()) picture_visited.clear();
            picture_zoom = picture_pan_x = picture_pan_y = 0;
        }
        return {};
    case xb::RS: picture_info = !picture_info; return {}; // Ex-Box addition: no 360 original
    case xb::Y:
        if (!picture_background_available()) { show("Apply as Background", "Not available on PS5 yet."); return {}; }
        if (const PictureFile *pic = picture()) {
            picture_background_pending = pic->path;
            picture_background_confirm = true;
            picture_background_yes = false;
        }
        return {};
    default: return {};
    }
}

std::vector<std::string> Model::video_list() const
{
    std::vector<std::string> out;
    for (const auto &key : trailers)
    {
        const auto it = video_info.find(key);
        if (it == video_info.end() || it->second.playable)
            out.push_back(key); // a missing clip would only spin its disc
    }
    // All current clips are Game Videos (trailers); Type ties sort by Title.
    // There is no movie/TV/season metadata in the trailer manifest.
    if (video_order == 1 || video_order == 2)
    {
        auto folded = [this](const std::string &key) {
            const auto it = trailer_names.find(key);
            std::string name = it != trailer_names.end() && !it->second.empty() ? it->second : key;
            for (char &ch : name)
                ch = char(std::tolower(static_cast<unsigned char>(ch)));
            return name;
        };
        std::stable_sort(out.begin(), out.end(), [&](const std::string &a, const std::string &b) { return folded(a) < folded(b); });
    }
    else
    {
        auto date = [this](const std::string &key) {
            const auto it = video_info.find(key);
            return it == video_info.end() ? 0ll : it->second.downloaded;
        };
        std::stable_sort(out.begin(), out.end(), [&](const std::string &a, const std::string &b) { return date(a) > date(b); });
    }
    return out;
}

std::string Model::video_key() const
{
    if (viewer != Viewer::Videos)
        return {};
    const auto list = video_list();
    return list.empty() ? std::string() : list[size_t(std::clamp(viewer_item, 0, int(list.size()) - 1))];
}

int Model::video_length() const
{
    const auto it = video_info.find(video_key());
    return it == video_info.end() ? 0 : it->second.ms;
}

int Model::video_hidden() const
{
    int hidden = 0;
    for (const auto &key : trailers)
    {
        const auto it = video_info.find(key);
        hidden += it != video_info.end() && !it->second.playable;
    }
    return hidden;
}

void Model::load_videos(const std::vector<std::string> &lines)
{
    video_recent.clear();
    video_resume.clear();
    for (const auto &line : lines)
    {
        const size_t tab = line.find('\t');
        const std::string key = line.substr(0, tab);
        // Only clips still on the console; a clip renamed away is forgotten.
        if (key.empty() || std::find(trailers.begin(), trailers.end(), key) == trailers.end() ||
            std::find(video_recent.begin(), video_recent.end(), key) != video_recent.end())
            continue;
        video_recent.push_back(key);
        const int ms = tab == std::string::npos ? 0 : std::atoi(line.c_str() + tab + 1);
        if (ms >= kResumeMin)
            video_resume[key] = ms;
    }
}

std::vector<std::string> Model::saved_videos() const
{
    std::vector<std::string> out;
    for (const auto &key : video_recent)
    {
        const auto it = video_resume.find(key);
        out.push_back(key + "\t" + std::to_string(it == video_resume.end() ? 0 : it->second));
    }
    return out;
}

// Old Added/Name/Recently Watched settings map to Date/Title/Date.
void Model::load_video_order(int value, bool legacy)
{
    video_order = legacy ? (value == 1 ? 1 : 0) : std::clamp(value, 0, kVideoOrderCount - 1);
}

// videos/DetailsActions.xur scnBookmark (9199): Resume / Restart.
Effect Model::request_video()
{
    const std::string key = video_key();
    if (key.empty()) return {};
    const auto it = video_resume.find(key);
    const int length = video_length();
    if (it != video_resume.end() && it->second >= kResumeMin &&
        (length <= 0 || it->second < length - kResumeMin))
    {
        video_pending = key;
        video_start_item = 0;
        return {};
    }
    enter_video(false);
    return {Command::SaveVideos, -1};
}

// Full screen counts as watched only after the bookmark choice is accepted.
void Model::enter_video(bool resume)
{
    const std::string key = video_key();
    viewer_full = true;
    video_paused = false;
    const auto it = video_resume.find(key);
    const int length = video_length();
    video_elapsed = resume && it != video_resume.end() && it->second >= kResumeMin &&
                    (length <= 0 || it->second < length - kResumeMin) ? it->second : 0;
    video_pending.clear();
    if (video_elapsed == 0) video_resume.erase(key);
    if (key.empty())
        return;
    video_recent.erase(std::remove(video_recent.begin(), video_recent.end(), key), video_recent.end());
    video_recent.insert(video_recent.begin(), key);
    if (video_recent.size() > kMaxVideoRecent)
        video_recent.resize(kMaxVideoRecent);
}

// Leaving a video keeps its place, unless it barely started or was nearly done.
void Model::leave_video()
{
    const std::string key = video_key();
    const int length = video_length();
    if (key.empty())
        return;
    if (video_elapsed >= kResumeMin && (length <= 0 || video_elapsed < length - kResumeMin))
        video_resume[key] = video_elapsed;
    else
        video_resume.erase(key);
}

Effect Model::press_video_full(xb::Button button)
{
    const int last = std::max(0, viewer_count() - 1);
    const int length = video_length();
    switch (button)
    {
    case xb::B:
        leave_video();
        viewer_full = false;
        return {Command::SaveVideos, -1};
    case xb::LB:
    case xb::RB:
    {
        const int to = std::clamp(viewer_item + (button == xb::RB ? 1 : -1), 0, last);
        if (to == viewer_item)
            return {};
        leave_video();
        viewer_item = to;
        enter_video();
        return {Command::SaveVideos, -1};
    }
    case xb::Left:
    case xb::Right:
    case xb::LT:
    case xb::RT:
    {
        const int step = button == xb::Left ? -kVideoStep : button == xb::Right ? kVideoStep
                         : button == xb::LT ? -kVideoJump : kVideoJump;
        int to = std::max(0, video_elapsed + step);
        if (length > 0)
            to = std::min(to, length);
        if (to == video_elapsed)
            return {};
        video_elapsed = to;
        return {Command::VideoSeek, to};
    }
    case xb::A:
        // At the end, play again from the start.
        if (video_paused && length > 0 && video_elapsed >= length)
        {
            video_paused = false;
            video_elapsed = 0;
            return {Command::VideoSeek, 0};
        }
        video_paused = !video_paused;
        return {Command::VideoPause, -1};
    case xb::Up:
    case xb::Down:
        if (!video_theater) return {};
        video_theater_reaction = (video_theater_reaction + (button == xb::Down ? 1 : kTheaterReactionCount - 1)) % kTheaterReactionCount;
        return {};
    case xb::X:
        if (video_theater) {
            ++video_theater_reaction_serial;
            return {};
        }
        video_paused = !video_paused;
        return {Command::VideoPause, -1};
    case xb::Y:
    {
        const auto it = video_info.find(video_key());
        if (it == video_info.end() || !it->second.subtitles)
            return {};
        video_subtitles = !video_subtitles;
        return {Command::SaveSettings, -1};
    }
    case xb::RS:
        // Ex-Box Theater Party is part of the existing local player.  It
        // changes only the presentation; playback, Guide suspension, resume
        // state and the GPU video image remain the same objects.
        video_theater = !video_theater;
        return {};
    default: return {};
    }
}

// memory/Strings.xus and DeviceSelector/DeviceDetails/Categories/ItemsGrid/ItemDetails.xur,
// retail 9199, 13604, 15574 and 17559. The latter three scenes use UTF-8 STRN.
std::string storage_size(long long bytes)
{
    if (bytes < 0) return "Unknown"; // Ex-Box addition: no 360 original; no invented sizes
    // The 360 printed sizes in binary units ("232.9 GB", "45 MB", "128 KB");
    // memory/Strings.xus has no unit strings, the console formatted them in code.
    char out[32];
    const double b = double(bytes);
    if (b >= 1024.0 * 1024 * 1024) snprintf(out, sizeof out, "%.1f GB", b / (1024.0 * 1024 * 1024));
    else if (b >= 1024.0 * 1024) snprintf(out, sizeof out, "%.0f MB", b / (1024.0 * 1024));
    else if (bytes > 0) snprintf(out, sizeof out, "%lld KB", (bytes + 1023) / 1024);
    else snprintf(out, sizeof out, "0 KB");
    return out;
}
std::string Model::storage_category_name(int category) const
{
    constexpr const char *names[] = {"Games","Gamer Profiles","Demos","Videos","Themes",
        "Gamer Pictures","Avatar Items","System Items","Music"};
    if (category < 0 || category >= StorageCategoryCount) return "";
    if (category == StorageGames && era == 3) return "Games and Apps";
    if (category == StorageProfiles && era == 3 && !metro_v1()) return "Profiles";
    return names[category];
}
std::vector<int> Model::storage_item_list() const
{
    std::vector<int> out;
    for (size_t i=0;i<storage.items.size();++i) {
        const auto &entry=storage.items[i];
        if (entry.category==storage_category && (storage_device<0 || entry.device==storage_device))
            out.push_back(int(i));
    }
    return out;
}
std::vector<GuideEntry> Model::storage_entries() const
{
    using A=GuideAction;
    std::vector<GuideEntry> out;
    if (system_page==StorageDevices) {
        if (!storage.devices.empty()) out.push_back({"All Devices",A::ConsoleSettings,-1});
        for (size_t i=0;i<storage.devices.size();++i)
            out.push_back({storage.devices[i].name,A::ConsoleSettings,int(i)});
    } else if (system_page==StorageSummary) {
        out={{"Select Item",A::ConsoleSettings,StorageCategories},{"Device Options",A::ConsoleSettings,StorageOptions}};
    } else if (system_page==StorageCategories) {
        for (int i=0;i<StorageCategoryCount;++i) out.push_back({storage_category_name(i),A::ConsoleSettings,i});
    } else if (system_page==StorageItems) {
        for (int i:storage_item_list()) out.push_back({storage.items[size_t(i)].name,A::ConsoleSettings,i});
    } else if (system_page==StorageDetails) {
        out={{"Copy",A::Unavailable},{"Move",A::Unavailable},{"Delete",A::Unavailable}};
    } else if (system_page==StorageOptions) {
        out={{"Rename",A::Unavailable},{"Format",A::Unavailable},{"Clear System Cache",A::Unavailable},
             {"Transfer Content",A::Unavailable},{"Delete System Update",A::Unavailable}};
    }
    return out;
}
std::string Model::storage_body() const
{
    const StorageDevice *device=storage_device>=0 && storage_device<int(storage.devices.size())
        ? &storage.devices[size_t(storage_device)] : nullptr;
    const std::string name=device?device->name:"All Devices";
    if (system_page==StorageDevices)
        return storage.devices.empty()?"No storage devices found.":"Select a storage device.";
    if (system_page==StorageSummary || system_page==StorageOptions)
        return "Device Summary\n"+name+"\nCapacity: "+storage_size(device?device->capacity:-1)+
            "\nFree Space: "+storage_size(device?device->free:-1)+(device?"\n"+device->path:"");
    const StorageItem *item=nullptr;
    if (system_page==StorageDetails && storage_item>=0 && storage_item<int(storage.items.size()))
        item=&storage.items[size_t(storage_item)];
    if (system_page==StorageItems) {
        const auto list=storage_item_list();
        if (list.empty()) return "No items found.";
        if (viewer_item>=0 && viewer_item<int(list.size())) item=&storage.items[size_t(list[size_t(viewer_item)])];
    }
    if (item) {
        // Ex-Box addition: unknown metadata stays explicit. stat mtime is not creation time.
        return item->name+"\nOwner: "+(item->owner.empty()?"Unknown":item->owner)+
            "\nCreated: "+(item->created.empty()?"Unknown":item->created)+
            "\nFile Size: "+storage_size(item->bytes)+"\nLocation: "+(item->path.empty()?"Unknown":item->path);
    }
    return name+"\nFree Space: "+storage_size(device?device->free:-1);
}
std::string Model::storage_row_size(int row) const
{
    if (system_page==StorageDevices) {
        if (row>0 && row<=int(storage.devices.size())) return "Free Space: "+storage_size(storage.devices[size_t(row-1)].free);
    } else if (system_page==StorageItems) {
        const auto list=storage_item_list();
        if (row>=0 && row<int(list.size())) return storage_size(storage.items[size_t(list[size_t(row)])].bytes);
    }
    return "";
}
int Model::storage_row_icon(int row) const
{
    if (system_page==StorageDevices) {
        if (row>0 && row<=int(storage.devices.size())) return storage.devices[size_t(row-1)].external?1:0;
        return 11;
    }
    if (system_page==StorageCategories) return row>=0 && row<StorageCategoryCount?row+2:-1;
    if (system_page==StorageItems || system_page==StorageDetails) return storage_category+2;
    return -1;
}
Effect Model::press_storage(xb::Button button)
{
    const auto entries=storage_entries();
    const int last=std::max(0,int(entries.size())-1);
    viewer_item=std::clamp(viewer_item,0,last);
    const bool grid=system_page==StorageItems;
    switch (button) {
    case xb::Up:
        if (!grid) viewer_item=std::max(0,viewer_item-1);
        else if (viewer_item>=2) viewer_item-=2;
        return {};
    case xb::Down:
        if (!grid) viewer_item=std::min(last,viewer_item+1);
        else if (viewer_item/2<last/2) viewer_item=std::min(last,viewer_item+2);
        return {};
    case xb::Left: if (grid && viewer_item%2) --viewer_item; return {};
    case xb::Right: if (grid && viewer_item%2==0) viewer_item=std::min(last,viewer_item+1); return {};
    case xb::B:
        if (!system_history.empty()) {
            const auto previous=system_history.back();system_history.pop_back();
            system_page=previous.page;viewer_item=previous.item;
        } else {system_page=SystemRoot;viewer_item=2;}
        return {};
    case xb::X:
        if (grid && !entries.empty()) show("Delete","Not available on PS5 yet.");
        return {};
    case xb::Y:
        if (system_page==StorageDevices && !entries.empty()) {
            storage_device=entries[size_t(viewer_item)].app;open_system_page(StorageOptions);
        } else if (system_page==StorageCategories) show("Transfer Content","Not available on PS5 yet.");
        return {};
    case xb::A:
        if (entries.empty()) return {};
        if (system_page==StorageOptions || system_page==StorageDetails) {
            show(entries[size_t(viewer_item)].label,"Not available on PS5 yet.");return {};
        }
        if (system_page==StorageDevices) {
            storage_device=entries[size_t(viewer_item)].app;open_system_page(StorageSummary);
        } else if (system_page==StorageSummary) open_system_page(entries[size_t(viewer_item)].app);
        else if (system_page==StorageCategories) {
            storage_category=viewer_item;storage_item=-1;open_system_page(StorageItems);
        } else if (grid) {storage_item=entries[size_t(viewer_item)].app;open_system_page(StorageDetails);}
        return {};
    default: return {};
    }
}

// network/2004, 2018, 2033, 2038 and NetworkMain/TestingScene (6770-17559).
std::vector<GuideEntry> Model::network_entries() const
{
    using A=GuideAction;
    const bool launch=era==0 && dashboard_build<5759;
    if (system_page==NetworkMain) return {
        {launch || (era==3 && !metro_v1())?"Test Xbox Live Connection":"Test Xbox LIVE Connection",A::NetworkSettings,NetworkTesting},
        {era==0?"Edit Settings":"Configure Network",A::Unavailable},
        {"Current Settings",A::NetworkSettings,NetworkDetails},
        {"Advanced Settings",A::NetworkSettings,NetworkConsole},
        {era>=2?"Available Networks":"Wireless Network",A::Unavailable}};
    // 1888 has no Restart label: repeat its original test action.
    if (system_page==NetworkTesting) return {{launch?"Test Xbox Live Connection":era==0?"Restart Tests":"Restart Test",A::NetworkSettings,NetworkTesting},
        {"More Info",A::Unavailable}};
    if (system_page==NetworkDetails) return {{"IP Settings",A::NetworkSettings,NetworkIP},
        {"DNS Settings",A::NetworkSettings,NetworkDNS},{"Advanced Settings",A::NetworkSettings,NetworkConsole}};
    if (system_page==NetworkIP) return {{"IP Address",A::Unavailable},{"Subnet Mask",A::Unavailable},{"Gateway",A::Unavailable}};
    if (system_page==NetworkDNS) return {{"Primary DNS Server",A::Unavailable},{"Secondary DNS Server",A::Unavailable}};
    return {{"Host Name",A::Unavailable},{era<2 && network.medium==NetworkMedium::Wired?"Wired MAC Address":"MAC Address",A::Unavailable},
        {"Wireless Information",A::Unavailable}};
}

std::string Model::network_value(const std::string &label) const
{
    if (label=="IP Address") return network.address;
    if (label=="Subnet Mask") return network.mask;
    if (label=="Gateway") return network.gateway;
    if (label=="Primary DNS Server") return network.dns;
    if (label=="Secondary DNS Server") return network.dns_secondary;
    if (label=="Host Name") return network.hostname;
    if (label=="MAC Address" || label=="Wired MAC Address") return network.mac;
    // Strings.xus: Network Adapter / Wired / Wireless. Unknown stays blank.
    if (label=="Network Adapter") return network.medium==NetworkMedium::Wired?"Wired":
        network.medium==NetworkMedium::Wireless?"Wireless":"";
    return "";
}

Effect Model::press_network(xb::Button button)
{
    const auto entries=network_entries();
    viewer_item=std::clamp(viewer_item,0,int(entries.size())-1);
    if (button==xb::Up) viewer_item=std::max(0,viewer_item-1);
    if (button==xb::Down) viewer_item=std::min(int(entries.size())-1,viewer_item+1);
    if (button==xb::B) {
        if (system_page==NetworkTesting || system_page==NetworkMain) {
            ++network_generation;network_test.running=false;
            for (auto &step:network_test.steps) if (step==NetworkResult::Running) step=NetworkResult::Idle;
        }
        if (system_page==NetworkMain && network_from_blades) {
            viewer=Viewer::None;system_page=SystemRoot;system_history.clear();network_from_blades=false;
        } else if (!system_history.empty()) {
            const auto previous=system_history.back();system_history.pop_back();
            system_page=previous.page;viewer_item=previous.item;
        } else {system_page=SystemRoot;viewer_item=0;}
    }
    if (button==xb::A) {
        const auto &entry=entries[size_t(viewer_item)];
        if (entry.action==GuideAction::Unavailable) {
            // Ex-Box addition: no 360 original. Read-only PS5 pages.
            if (system_page==NetworkTesting) show(entry.label,
                "Console: an interface is up.\nNetwork: gateway or local DNS server, TCP port 53 or 80.\n"
                "Internet: DNS lookup and TCP to example.com:443.\n"
                "A failed TCP check can mean the host blocks that port.\nXbox LIVE is not available on PS5.");
            else show(entry.label,"Not available on PS5 yet.");
        } else {
            if (entry.app!=system_page) open_system_page(entry.app);
            if (entry.app==NetworkTesting) {
                network_test.start();++network_generation;
                return {Command::TestNetwork,-1};
            }
        }
    }
    return {};
}

std::vector<GuideEntry> Model::system_entries() const
{
    using A = GuideAction;
    if (system_page == SystemDiagnostics) return {};
    if (system_page == SystemHealth) {std::vector<GuideEntry> out;for(const auto&row:compat_health_rows())out.push_back({row.label,A::Unavailable});return out;}
    if (network_page()) return network_entries();
    if (storage_page()) return storage_entries();
    if (system_page == SystemFamily)
        return {{"PS5 Parental Controls",A::Unavailable},{"Local Game Library",A::AllGames},
                {"Communication and Online Safety",A::Unavailable},{"Purchases",A::Unavailable},
                {"Xbox Live Family Settings",A::Unavailable}};
    if (system_page == SystemAccount) {
        std::vector<GuideEntry> out{{"Local Profile",A::Profile},{"Change Gamer Picture",A::ChangeGamerpic},
                                    {"My Avatar",A::Avatar}};
        if(avatar_editable||external_avatar_editor)out.push_back({"Avatar Editor",A::AvatarEdit});
        out.push_back({"Xbox Live Account",A::Unavailable});out.push_back({"PS5 Account",A::Unavailable});
        return out;
    }
    if (system_page == SystemKinect)
        return {{"Kinect Sensor",A::Unavailable},{"Controller Layout",A::ControllerMap},
                {"Camera and Microphone",A::Unavailable},{"Kinect Tuner",A::Unavailable},
                {"Sign-In Recognition",A::Unavailable}};
    if (system_page == SystemComputers)
        return {{"Music Library",A::Music},{"Pictures",A::Pictures},{"Video Library",A::Videos},
                {kSystemApps[3].title,A::LaunchSystem,3},{"Windows Media Center PC",A::Unavailable},
                {"Network Media Sharing",A::Unavailable}};
    if (system_page == SystemTV)
        return {{"Display Settings",A::ConsoleSettings,SystemDisplay},{"Video Library",A::Videos},
                {kSystemApps[2].title,A::LaunchSystem,2},{"TV Provider",A::Unavailable},
                {"Live TV",A::Unavailable}};
    if (system_page == SystemCamera)
        return {{"PS5 Camera Access",A::Unavailable},{"Gamer Picture Photo Booth",A::Unavailable},
                {"Video Chat",A::Unavailable},{"Camera Privacy",A::Unavailable}};
    if (system_page == SystemLanguage)
        return {{"Dashboard Language: English",A::Unavailable},{"Locale: PS5 System",A::Unavailable},
                {"Keyboard and Text Input",A::Unavailable}};
    if (system_page == SystemAutoPlay)
        return {{"Game Launch: Ask First",A::Unavailable},{"Video: Resume or Restart",A::Unavailable},
                {"Music: Keep Playing",A::Unavailable},{"PS5 Disc Auto-Play",A::Unavailable}};
    if (system_page == SystemConnectedDevices)
        return {{"DualSense Controller",A::ControllerMap},{"USB Storage",A::ConsoleSettings,StorageDevices},
                {"Kinect Sensor",A::Unavailable},{"Xbox 360 Accessories",A::Unavailable}};
    if (system_page == SystemMusicVolume) {
        std::vector<GuideEntry> out;for(int volume=0;volume<=10;++volume)out.push_back({"Music Volume "+std::to_string(volume*10)+"%",A::Unavailable});return out;
    }
    const bool v2 = era == 3 && !metro_v1();
    // consoles/dashSysCslSet.xur button order, all four builds. btnPolicy has
    // no known PS5 equivalent and remains hidden.
    // Labels/current values: each build's consoles/dashCSettingsStrings.xus.
    if (system_page == SystemConsole) {
        std::vector<GuideEntry> out{{"Display",A::ConsoleSettings,SystemDisplay},
            {"Audio",A::ConsoleSettings,SystemAudio},{"Language and Locale",A::ConsoleSettings,SystemLanguage},
            {"Clock",A::ConsoleSettings,SystemClock},{"Startup and Shutdown",A::ConsoleSettings,SystemStartup},
            {"Auto-Play",A::ConsoleSettings,SystemAutoPlay}};
        if (v2) out.push_back({"Auto-Downloads",A::ConsoleSettings,SystemGameMusic});
        out.push_back({"Remote Control",A::ControllerMap});
        // 17559's dynamically supplied pages (ConsoleName/ConsoleDiscoverable).
        if (v2) {
            out.push_back({"Console Name",A::Unavailable});
            out.push_back({"Connected Devices",A::ConsoleSettings,SystemConnectedDevices});
        }
        out.push_back({"System Info",A::ConsoleSettings,SystemInfo});
        return out;
    }
    if (system_page == SystemDisplay) {
        // dashSysCslSetDisplay and its Format/OutputLevels/ColorSpace/Discovery
        // scenes; 3D and TVScreenSize first appear in the 13604 package.
        std::vector<GuideEntry> out{{"Screen Resolution",A::ConsoleSettings,SystemResolution},
            {"Screen Format",A::ConsoleSettings,SystemScreenFormat},{"Reference Levels",A::Unavailable},
            {"HDMI Color Space",A::Unavailable},{"Display Discovery",A::Unavailable},
            {"Screen Saver",A::ConsoleSettings,SystemScreenSaver}};
        if (era >= 2) {
            out.push_back({"3D TV",A::Unavailable});
            out.push_back({"TV Size",A::Unavailable});
        }
        if (v2) out.push_back({"Closed Captioning",A::Unavailable});
        out.push_back({"HDR Output",A::Unavailable});
        return out;
    }
    if (system_page == SystemAudio) {
        // dashSysCslSetAudio.xur; 17559 adds AudioNielsen.
        std::vector<GuideEntry> out{{"Digital Output",A::Unavailable},
                                   {"Sound Effects",A::ConsoleSettings,SystemSoundEffects},
                                   {"Ex-Box Music Volume",A::ConsoleSettings,SystemMusicVolume}};
        if (v2) out.push_back({"Ratings Collection Sound",A::Unavailable});
        return out;
    }
    if (system_page == SystemClock)
        return {{"Date and Time",A::ConsoleSettings,SystemDateTime},
                {"Time Format",A::ConsoleSettings,SystemTimeFormat},
                {"Time Zone",A::Unavailable},{"Daylight Saving",A::Unavailable}};
    if (system_page == SystemWelcome) return {{"Show",A::ConsoleSettings},{"Hide",A::ConsoleSettings}};
    if (system_page == SystemStartup) {
        // dashSysCslSetStartupShutdown.xur: Metro removes Welcome Channel.
        std::vector<GuideEntry> out{{"Startup",A::Unavailable}};
        if (era==1 || era==2) out.push_back({"Welcome Channel",A::ConsoleSettings,SystemWelcome});
        // Ex-Box addition: no 360 original. Auto-Off opens the real Screen
        // Saver control; never claim that dimming shuts down the PS5.
        out.push_back({"Auto-Off",A::ConsoleSettings,SystemScreenSaver});
        out.push_back({"Background Downloads",A::Unavailable});
        return out;
    }
    // dashSysCslSetAudioSoundEffects, ClockFormat, Screensaver, DisplayFormat.
    // Their choices are handled locally, never dispatched as Guide actions.
    if (system_page == SystemSoundEffects)
        return {{"Enable Sound Effects",A::Unavailable},{"Disable Sound Effects",A::Unavailable}};
    if (system_page == SystemTimeFormat)
        return {{"12-Hour Clock",A::Unavailable},{"24-Hour Clock",A::Unavailable}};
    if (system_page == SystemScreenSaver)
        return {{"Enable",A::Unavailable},{"Disable",A::Unavailable}};
    if (system_page == SystemResolution) return {{"1080p Original",A::Unavailable},{"4K Sharp",A::Unavailable}}; // Ex-Box rendering option
    if (system_page == SystemScreenFormat) return {{"Widescreen",A::Unavailable}};
    if (system_page == SystemGameMusic) return {{"Scan Installed Game Folders",A::Unavailable},{"Do Not Scan Game Folders",A::Unavailable}};
    if (system_page == SystemLocalFriends) return {{"Show Local Offline Friends",A::Unavailable},{"Hide Local Offline Friends",A::Unavailable}};
    if (system_page == SystemLibraryStatus)
        return {{"PS4 / PS5 Games",A::Unavailable},{"Homebrew Apps",A::Unavailable},
                {"Xbox 360 Frontend",A::Unavailable},{"Discovery Source",A::Unavailable},
                {"Pins",A::Unavailable},{"Recently Played",A::Unavailable},
                {"Installed Themes",A::Unavailable},{"Storage Inventory",A::Unavailable}};
    if (system_page == SystemExBox)
        return {{"Dashboard Version",A::CycleEra},{"Themes",A::SelectTheme},{"Promotions",A::AdsSettings},
                {"Game Library",A::AllGames},{"Library Status",A::ConsoleSettings,SystemLibraryStatus},
                {"Storage & Memory",A::ConsoleSettings,StorageDevices},
                {"Game Soundtracks",A::ConsoleSettings,SystemGameMusic},{"Local Offline Friends",A::ConsoleSettings,SystemLocalFriends},
                {"Refresh Libraries",A::Refresh},{"Controller Layout",A::ControllerMap},
                {"Compatibility & Health",A::ConsoleSettings,SystemHealth}};
    if (system_page == SystemInfo)
        return {{"Ex-Box Version",A::Unavailable},{"Dashboard Build",A::Unavailable},
                {"Rendering Mode",A::Unavailable},{"Installed Games",A::Unavailable},
                {"Homebrew Apps",A::Unavailable},{"Installed Themes",A::Unavailable},
                {"Storage Devices",A::Unavailable},{"System Software",A::Unavailable}};
    if (system_page == SystemDateTime)
        return {{"Clock Source",A::Unavailable},{"Time Format",A::ConsoleSettings,SystemTimeFormat},
                {"Time Zone",A::Unavailable},{"Daylight Saving",A::Unavailable}};
    // consoles/SystemScene: NXE lists Family Settings second; Kinect and Metro
    // Kinect Settings; Metro renames Memory to Storage and adds TV.
    std::vector<GuideEntry> out{{"Console Settings",A::ConsoleSettings}};
    if (era == 1) out.push_back({"Family Settings",A::ConsoleSettings,SystemFamily});
    else out.push_back({"Kinect Settings",A::ConsoleSettings,SystemKinect});
    out.push_back({era == 3 ? "Storage" : "Memory",A::ConsoleSettings,StorageDevices});
    out.push_back({"Network Settings",A::NetworkSettings});
    out.push_back({"Computers",A::ConsoleSettings,SystemComputers});
    if (era == 3) out.push_back({"TV",A::ConsoleSettings,SystemTV});
    out.push_back({era == 3 && !metro_v1() ? "Xbox Live Vision" : "Xbox LIVE Vision",A::ConsoleSettings,SystemCamera});
    out.push_back({"Initial Setup",A::InitialSetup});
    out.push_back({"Ex-Box Settings",A::ConsoleSettings,SystemExBox});
    out.push_back({"Compatibility & Health",A::ConsoleSettings,SystemHealth});
    // Additional Ex-Box account tools append after every authored retail route,
    // preserving all original D-pad positions and learned controller paths.
    out.push_back({"Account Management",A::ConsoleSettings,SystemAccount});
    if (developer_diagnostics_available)
        out.push_back({"Developer Diagnostics",A::ConsoleSettings,SystemDiagnostics});
    return out;
}

void Model::open_system_page(int page, int item)
{
    if (system_page == SystemRoot) system_history.clear();
    system_history.push_back({system_page,viewer_item});
    system_page = page;
    viewer_item = item;
}

std::string Model::system_page_title() const
{
    if (system_page==SystemWelcome) return "Welcome Channel";
    if (system_page==SystemHealth) return "Compatibility & Health";
    if (system_page==SystemDiagnostics) return "Developer Diagnostics";
    if (system_page==SystemExBox) return "Ex-Box Settings";
    if (system_page==SystemGameMusic) return "Game Soundtracks";
    if (system_page==SystemLocalFriends) return "Local Offline Friends";
    if (system_page==SystemLibraryStatus) return "Library Status";
    if (system_page==SystemFamily) return "Family Settings";
    if (system_page==SystemAccount) return "Account Management";
    if (system_page==SystemKinect) return "Kinect Settings";
    if (system_page==SystemComputers) return "Computers and Media";
    if (system_page==SystemTV) return "TV and Video";
    if (system_page==SystemCamera) return "Camera";
    if (system_page==SystemLanguage) return "Language and Locale";
    if (system_page==SystemAutoPlay) return "Auto-Play";
    if (system_page==SystemConnectedDevices) return "Connected Devices";
    if (system_page==SystemMusicVolume) return "Ex-Box Music Volume";
    if (network_page()) {
        switch (system_page) {
        case NetworkTesting: return era==0?(dashboard_build<5759?"Test Xbox Live Connection":"Test Xbox LIVE Connection"):"Testing Connection";
        case NetworkDetails: return "Current Settings";
        case NetworkIP: return "IP Settings";
        case NetworkDNS: return "DNS Settings";
        case NetworkConsole: return "Advanced Settings";
        default: return "Network Settings";
        }
    }
    if (storage_page()) {
        if (system_page==StorageDevices) return "Storage Devices";
        if (system_page==StorageSummary) return "Device Summary";
        if (system_page==StorageOptions) return "Device Options";
        if (system_page==StorageItems) return "Select Item";
        if (system_page==StorageDetails) return "Item Options";
        return storage_device>=0 && storage_device<int(storage.devices.size())
            ? storage.devices[size_t(storage_device)].name : "All Devices";
    }
    // Headers of consoles/dashSysCslSet*.xur, 9199 through 17559.
    constexpr const char *titles[] = {"System Settings","Console Settings","Display","Audio","Clock",
        "Startup and Shutdown","System Info","Sound Effects","Time Format","Screen Saver",
        "Date and Time","Screen Resolution","Screen Format"};
    return titles[std::clamp(system_page, int(SystemRoot), int(SystemScreenFormat))];
}

std::string Model::system_value(const std::string &label) const
{
    if(system_page==SystemHealth)for(const auto&row:compat_health_rows())if(row.label==label)return row.value;
    // consoles/dashCSettingsStrings.xus (9199/13604/15574/17559).
    if (label == "Display" || label == "Screen Resolution") {
        const std::string active=active_render_scale==2?"4K Sharp":"1080p Original";
        if (render_scale==active_render_scale) return active;
        return active + " (restart required: " + (render_scale==2?"4K Sharp":"1080p Original") + ")";
    }
    if (label == "Screen Format") return "Widescreen";
    if (label == "Audio" || label == "Sound Effects") return sounds ? "Sound Effects Enabled" : "Sound Effects Disabled";
    if (label == "Welcome Channel") return welcome_visible?"Show":"Hide";
    if (label == "Time Format") return clock_24_hour ? "24-Hour Clock" : "12-Hour Clock";
    if (label == "Screen Saver") return dimming ? "Screen Saver Enabled" : "Screen Saver Disabled";
    if (label == "Dashboard Version") return kDashboardFamilies[dashboard_family(era,dashboard_build)].name;
    if (label == "Themes") return installed_theme_id.empty()?"Xbox 360":installed_theme_id;
    if (label == "Promotions") return promotion_mode==0?"Authentic Period":promotion_mode==1?"Local Personalized":"Off";
    if (label == "Game Soundtracks") return music_game_scan?"Installed Game Scan Enabled":"Installed Game Scan Disabled";
    if (label == "Local Offline Friends") return local_friend_avatars?"Shown":"Hidden";
    if (label == "Game Library") return std::to_string(games.size())+" games";
    if (label == "Library Status") return std::to_string(games.size()+apps.size())+" installed entries";
    if (label == "Storage & Memory") return storage.devices.empty()?"Open to scan":std::to_string(storage.devices.size())+" devices";
    if (system_page==SystemLibraryStatus) {
        if (label=="PS4 / PS5 Games") return std::to_string(games.size());
        if (label=="Homebrew Apps") return std::to_string(apps.size());
        if (label=="Xbox 360 Frontend") return std::to_string(games_frontend.entries.size());
        if (label=="Discovery Source") return game_library_companion?"Companion feed":"Direct filesystem";
        if (label=="Pins") return std::to_string(pins.size());
        if (label=="Recently Played") return std::to_string(recent.size());
        if (label=="Installed Themes") return std::to_string(installed_themes.size());
        if (label=="Storage Inventory") return storage.devices.empty()?"Not scanned":std::to_string(storage.devices.size())+" devices";
    }
    if (label == "Refresh Libraries") return "Games, apps and local media";
    if (label == "Controller Layout") return "Xbox-style DualSense mapping";
    if (label == "System Info") return "Dashboard: 2.0." + std::to_string(dashboard_build) + ".0";
    if (label == "HDR Output") return "Managed by PS5";
    if (label == "PS5 Parental Controls" || label == "Purchases" ||
        label == "Communication and Online Safety" || label == "PS5 Account" ||
        label == "PS5 Camera Access" || label == "Camera Privacy" ||
        label == "PS5 Disc Auto-Play") return "Managed by PS5";
    if (label == "Local Game Library") return std::to_string(games.size()) + " games";
    if (label == "Local Profile") return player.empty() ? "Player" : player;
    if (label == "Xbox Live Account" || label == "Xbox Live Family Settings") return "Not connected";
    if (label == "Kinect Sensor" || label == "Kinect Tuner" || label == "Sign-In Recognition" ||
        label == "Xbox 360 Accessories") return "Not available on PS5";
    if (label == "Camera and Microphone" || label == "Video Chat") return "Unavailable to Ex-Box";
    if (label == "Music Library") return std::to_string(songs) + " tracks";
    if (label == "Pictures") return std::to_string(pictures) + " pictures";
    if (label == "Video Library") return std::to_string(video_list().size()) + " videos";
    if (label == "Windows Media Center PC" || label == "Network Media Sharing" ||
        label == "TV Provider" || label == "Live TV") return "Not connected";
    if (label == "Dashboard Language: English") return "English";
    if (label == "Locale: PS5 System" || label == "Keyboard and Text Input") return "PS5 system";
    if (label == "Game Launch: Ask First") return "Ask First";
    if (label == "Video: Resume or Restart") return "Resume when supported";
    if (label == "Music: Keep Playing") return "Enabled";
    if (label == "DualSense Controller") return "Xbox-style mapping";
    if (label == "USB Storage") return storage.devices.empty() ? "Open to scan" : std::to_string(storage.devices.size()) + " devices";
    if (label.rfind("Music Volume ",0)==0) return music_volume*10==std::atoi(label.c_str()+13) ? "Current" : "";
    if (label == "Ex-Box Music Volume") return std::to_string(music_volume*10) + "%";
    if (system_page == SystemInfo) {
        if (label == "Ex-Box Version") return "2.0." + std::to_string(dashboard_build) + ".0";
        if (label == "Dashboard Build") return std::to_string(dashboard_build);
        if (label == "Rendering Mode") return active_render_scale==2 ? "3840 x 2160" : "1920 x 1080";
        if (label == "Installed Games") return std::to_string(games.size());
        if (label == "Homebrew Apps") return std::to_string(apps.size());
        if (label == "Installed Themes") return std::to_string(installed_themes.size());
        if (label == "Storage Devices") return storage.devices.empty()?"Not scanned":std::to_string(storage.devices.size());
        if (label == "System Software") return "PS5 managed";
    }
    if (system_page == SystemDateTime) {
        if (label == "Clock Source") return "PS5 system clock";
        if (label == "Time Zone" || label == "Daylight Saving") return "PS5 system";
    }
    // No guessed locale, AV pack, remote, startup, or PS5 power policy.
    return "";
}

std::string Model::system_description() const
{
    if(system_page==SystemHealth){const auto rows=compat_health_rows();return rows.empty()?std::string{}:rows[std::size_t(std::clamp(viewer_item,0,int(rows.size())-1))].description;}
    if (system_page==SystemResolution)
        return std::string("Ex-Box rendering resolution. Restart required to apply changes.\n\nActive: ")+
               (active_render_scale==2?"4K Sharp":"1080p Original")+"\nRequested: "+(render_scale==2?"4K Sharp":"1080p Original");
    if (system_page==SystemWelcome) return "Show or hide the Welcome channel."; // dashSysCslSetWelcomeChannel.xur
    if (system_page==SystemExBox) return "Dashboard-specific controls that Ex-Box can safely own. PS5 system settings remain under the PS5 Settings app.";
    if (system_page==SystemGameMusic) return "Scan accessible installed-game folders for playable WAV, MP3, FLAC and OGG files. Ex-Box does not decrypt, mount, extract or bypass protected game data.";
    if (system_page==SystemLocalFriends) return "Show clearly labeled offline friends in Social and Theater. Validated editor friend saves are preferred; your installed avatar kit fills empty slots. They are local characters, not Xbox Live accounts or contacts.";
    if (system_page==SystemFamily) return "Ex-Box exposes its local game library here, but parental controls, purchases and communication safety remain in PS5 Settings. It never imitates or overrides platform account controls.";
    if (system_page==SystemAccount) return "Open dashboard-owned profile, gamer picture and avatar tools. Xbox Live is not connected; PS5 account and privacy controls remain in the PS5 system UI.";
    if (system_page==SystemKinect) return "Kinect hardware and Xbox 360 sign-in recognition are not available on PS5. The controller-layout entry remains useful for the Xbox-style DualSense mapping.";
    if (system_page==SystemComputers) return "Open local music, pictures, videos and the browser. Legacy Windows Media Center pairing and network media sharing are shown honestly as disconnected.";
    if (system_page==SystemTV) return "Open Ex-Box display and local video features. TV-provider setup and live television services are not emulated.";
    if (system_page==SystemCamera) return "Camera permission and privacy are controlled by PS5. Ex-Box does not claim access that the homebrew runtime has not provided.";
    if (system_page==SystemLanguage) return "The dashboard currently uses English and inherits locale and text input from the PS5 system.";
    if (system_page==SystemAutoPlay) return "Review Ex-Box launch and playback behavior. PS5 disc auto-play remains a PS5 system setting.";
    if (system_page==SystemConnectedDevices) return "Open the Xbox-style DualSense map or scan accessible storage. Unsupported Xbox 360 peripherals are reported without pretending they are connected.";
    if (system_page==SystemMusicVolume) return "Set dashboard music volume. This controls Ex-Box music and is saved with the dashboard settings.";
    if (system_page==SystemInfo) return "Live dashboard and library information. PS5 system-software details remain owned by the console.";
    if (system_page==SystemDateTime) return "Ex-Box reads the PS5 system clock and applies your selected 12-hour or 24-hour dashboard display format.";
    if (system_page==SystemScreenFormat) return "Ex-Box uses the widescreen 16:9 layout required by the dashboard and PS5 output.";
    if (system_page==SystemLibraryStatus) {
        const auto rows=system_entries();
        const std::string label=rows.empty()?std::string{}:rows[std::size_t(std::clamp(viewer_item,0,int(rows.size())-1))].label;
        if(label=="PS4 / PS5 Games")return "Installed console titles currently published to My Games. Refresh Libraries asks the active discovery source to rebuild this list.";
        if(label=="Homebrew Apps")return "Dashboard-launchable homebrew entries found in the configured homebrew locations.";
        if(label=="Xbox 360 Frontend")return "Owner-supplied Xbox 360 frontend entries. A listed entry is inventory evidence, not a claim that game dispatch is enabled.";
        if(label=="Discovery Source")return game_library_companion?"The bounded local companion feed answered and is preferred over ShadowMount's potentially empty appmeta view.":"The companion feed did not answer, so Ex-Box is reading the accessible app metadata filesystem directly.";
        if(label=="Storage Inventory")return "Memory/Storage performs a fresh read-only device and content inventory. Ex-Box never formats, moves or deletes PS5 data.";
        return "Dashboard-owned library status. Counts are current runtime records and unknown data is never guessed.";
    }

    // Body text from AudioSoundEffects/ClockFormat/Screensaver.xur, all builds.
    if (system_page == SystemSoundEffects)
        return "Disable sound effects to mute sounds in the Xbox Dashboard, Xbox Guide, and notifications.\n\n"
               "You'll still hear game sounds, music, video, and voice communications.";
    if (system_page == SystemTimeFormat) return "Choose how you want the time to be displayed.";
    if (system_page == SystemScreenSaver) {
        const std::string body = "Enable the screen saver to automatically dim the screen image after 10 minutes of inactivity.";
        if (!system_history.empty() && system_history.back().page == SystemStartup)
            return body + "\n\nEx-Box dims the screen; it does not turn off the PS5."; // Ex-Box addition: no 360 original
        return body;
    }
    return "";
}

std::string Model::system_meta(const std::string &label) const
{
    const bool v2 = era == 3 && !metro_v1();
    const std::string live = v2 ? "Xbox Live" : "Xbox LIVE";
    if (label == "Console Settings") return "Change your Xbox 360 console settings, including display, audio, language, and remote control.";
    if (label == "Account Management") return "Open your local dashboard profile, gamer picture and avatar. PS5 account controls remain in the PS5 system interface.";
    if (label == "Ex-Box Settings") return "Choose dashboard features Ex-Box actually controls: versions, themes, promotions, local game soundtracks, library refresh and offline Social guests.";
    if (label == "Family Settings") return "Protect younger family members by limiting content, communications, and online interactions.\n\nYou can adjust settings for the console or for individual profiles.";
    if (label == "Kinect Settings") return "Change Kinect sensor settings, and access the Kinect Tuner to configure your play space and diagnose audio problems.";
    if (label == "Memory" || label == "Storage") return "Move or delete saved games, profiles, and other items on Xbox 360 storage devices.";
    if (label == "Network Settings") return "Connect to " + live + " or your home network. Set up wireless connections and test network settings.";
    if (label == "Computers")
        return std::string("Manage your connection to a Windows Media Center PC.\n\nTo connect to a PC running media sharing software, ") +
               (era == 3 ? "open the video player, music player, or picture viewer." : "open your video, music, or picture library.");
    if (label == "TV") return "Change your TV provider.";
    if (label == "Xbox LIVE Vision" || label == "Xbox Live Vision") return "Change the settings for your " + live + " Vision camera.";
    if (label == "Initial Setup")
        return std::string("Start the original system setup.\n\n") +
               (era == 1 ? "You can't run initial setup if Family Settings are enabled."
                : era == 2 ? "You can't run initial setup if console safety is on."
                : v2 ? "You can't perform initial setup if content controls are on. If you want to do this, first turn them off in Settings, Family."
                     : "You can't run initial setup if content controls are on.");
    if(label=="Compatibility & Health")return "Review truthful local compatibility, asset, soundtrack, theme, renderer, and companion status. Unknown values remain unpublished instead of being guessed.";
    if (label=="PS5 Parental Controls" || label=="Communication and Online Safety" || label=="Purchases")
        return "This protection is enforced by PS5. Use PS5 Settings > Family and Parental Controls; Ex-Box cannot weaken or replace it.";
    if (label=="Xbox Live Family Settings" || label=="Xbox Live Account")
        return "Ex-Box is an offline dashboard and is not connected to Xbox Live. No account or family state is fabricated.";
    if (label=="PS5 Account") return "Sign-in, privacy and account security are managed by the PS5 system interface.";
    if (label=="Kinect Sensor" || label=="Kinect Tuner" || label=="Sign-In Recognition")
        return "Xbox 360 Kinect hardware and its calibration/sign-in services are not available to this PS5 homebrew app.";
    if (label=="Camera and Microphone" || label=="PS5 Camera Access" || label=="Camera Privacy")
        return "Camera and microphone permission is controlled by PS5. Ex-Box does not currently receive a supported capture interface.";
    if (label=="Video Chat") return "No online video-chat service is configured. Ex-Box will not imply a camera or network connection that is absent.";
    if (label=="Gamer Picture Photo Booth") return "Photo Booth remains unavailable until a supported PS5 camera input can be used with explicit permission.";
    if (label=="Windows Media Center PC" || label=="Network Media Sharing")
        return "No compatible local media server is connected. Use the local Music, Pictures and Video libraries for accessible files.";
    if (label=="TV Provider" || label=="Live TV") return "No TV provider or tuner service is connected. Local video playback remains available.";
    if (label=="HDR Output") return "HDR output is negotiated and controlled by PS5 video settings. Ex-Box does not override the console display pipeline.";
    if (label=="Digital Output") return "Audio routing and HDMI output format are controlled by PS5 Sound settings.";
    if (label=="Reference Levels" || label=="HDMI Color Space" || label=="Display Discovery" || label=="3D TV" || label=="TV Size" || label=="Closed Captioning")
        return "This display option is managed by PS5 or is unsupported by the current Ex-Box presenter.";
    if (label=="Dashboard Language: English") return "English is the installed dashboard language in this build.";
    if (label=="Locale: PS5 System" || label=="Keyboard and Text Input") return "Ex-Box inherits this behavior from the PS5 system.";
    if (label=="PS5 Disc Auto-Play") return "Disc auto-play is controlled by PS5 system settings.";
    if (label=="Xbox 360 Accessories") return "Xbox 360 accessories are not exposed as supported devices to this PS5 homebrew app.";
    if (label=="Startup" || label=="Background Downloads") return "This power or download policy is owned by PS5; Ex-Box does not change it.";
    if (label=="Console Name") return "The console name is managed by the PS5 system and is not rewritten by Ex-Box.";
    if (label=="System Software") return "PS5 system software is managed and updated outside Ex-Box.";
    if (label=="Clock Source" || label=="Time Zone" || label=="Daylight Saving") return "This value comes from the PS5 system clock settings.";
    return "";
}

exbox_services::health::Snapshot Model::compat_health_snapshot() const
{
    auto out=health_runtime;
    out.ps5x360_installed=std::any_of(games.begin(),games.end(),[](const App&game){return game.title_id==exbox_games::player_title;});
    const auto caps=rom_game_guide_preview.capabilities();out.companion_advertised=caps.cooperative_overlay||caps.pause_resume||caps.return_dashboard||caps.controller_map||caps.music||caps.settings;
    out.xbox_titles=0;out.artwork_mapped=0;out.artwork_unmapped=0;
    for(const auto&entry:games_frontend.entries)if(entry.platform==exbox_games::Platform::Xbox360){++out.xbox_titles;if(entry.owner_art_key.empty())++out.artwork_unmapped;else ++out.artwork_mapped;}
    out.continue_playing=recent.size();out.xbox_inventory_scanned=games_frontend_inventory_published;out.xbox_inventory_limited=games_frontend_limited;out.xbox_rejected=games_frontend_rejected;out.diagnostics_available=developer_diagnostics_available;
    return out;
}

std::vector<exbox_services::health::Row> Model::compat_health_rows() const{return exbox_services::health::rows(compat_health_snapshot());}

void Model::load_theme(int legacy_value)
{
    // All five former hue transforms migrate to the installed Xbox 360 default.
    // Decoded floor tints do not establish standalone retail colour themes.
    (void)legacy_value;
    theme = 0;
}
bool Model::load_promotion_mode_setting(const char* line)
{
    if(!line||std::strncmp(line,"promotion_mode=",15)!=0)return false;
    const char value=line[15];
    const char*tail=line+16;
    const bool terminated=!tail[0] || (tail[0]=='\n'&&!tail[1]) || (tail[0]=='\r'&&tail[1]=='\n'&&!tail[2]);
    if(value<'0'||value>'2'||!terminated)return false;
    promotion_mode=value-'0';promotions=promotion_mode!=2;return true;
}
std::string Model::saved_promotion_mode_setting()const
{
    return "promotion_mode="+std::to_string(std::clamp(promotion_mode,0,2));
}
void Model::set_installed_themes(std::vector<InstalledTheme> entries)
{
    installed_themes=std::move(entries);
    std::sort(installed_themes.begin(),installed_themes.end(),[](const auto&a,const auto&b){return a.display_name==b.display_name?a.stable_id<b.stable_id:a.display_name<b.display_name;});
}
void Model::load_installed_theme_setting(const std::string &line)
{
    const auto read=[&](const char *prefix,std::string &value){
        const std::size_t length=std::strlen(prefix);if(line.rfind(prefix,0)!=0)return false;
        value=line.substr(length);while(!value.empty()&&(value.back()=='\r'||value.back()=='\n'))value.pop_back();return true;
    };
    const auto valid=[](const std::string &value){
        if(value.size()!=22||value.rfind("theme-",0)!=0)return false;
        for(std::size_t i=6;i<value.size();++i)if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')))return false;
        return true;
    };
    std::string value;
    if(read("installed_theme=",value)){if(value.empty()||valid(value))installed_theme_id=std::move(value);return;}
    auto append=[&](std::vector<std::string>&list,std::size_t limit){if(valid(value)&&std::find(list.begin(),list.end(),value)==list.end()&&list.size()<limit)list.push_back(value);};
    if(read("theme_favorite=",value)){append(theme_favorites,64);return;}
    if(read("theme_recent=",value))append(theme_recent,16);
}
std::string Model::saved_installed_theme_setting() const {return "installed_theme="+installed_theme_id;}
std::vector<std::string> Model::saved_theme_library_settings() const
{
    std::vector<std::string> lines;lines.reserve(1+theme_favorites.size()+theme_recent.size());
    lines.push_back(saved_installed_theme_setting());
    for(const auto&id:theme_favorites)lines.push_back("theme_favorite="+id);
    for(const auto&id:theme_recent)lines.push_back("theme_recent="+id);
    return lines;
}
std::vector<int> Model::theme_visible_indices() const
{
    std::vector<int> out;
    if(theme_filter==0){out.reserve(installed_themes.size());for(std::size_t i=0;i<installed_themes.size();++i)out.push_back(int(i));return out;}
    const auto&ids=theme_filter==1?theme_favorites:theme_recent;out.reserve(ids.size());
    for(const auto&id:ids)for(std::size_t i=0;i<installed_themes.size();++i)if(installed_themes[i].stable_id==id){out.push_back(int(i));break;}
    return out;
}
int Model::theme_catalog_index_for_row(int row) const
{
    if(row<=0)return -1;const auto visible=theme_visible_indices();
    return row<=int(visible.size())?visible[std::size_t(row-1)]:-2;
}
int Model::theme_row_for_catalog_index(int index) const
{
    const auto visible=theme_visible_indices();for(std::size_t i=0;i<visible.size();++i)if(visible[i]==index)return int(i)+1;return 0;
}
int Model::theme_count() const
{
    return 1+int(theme_visible_indices().size())+
           (picture_background_available()&&picture_background>0?1:0);
}
std::string Model::theme_entry(int index) const
{
    if(index==0)return kThemes[0];
    const auto visible=theme_visible_indices();
    if(index>0&&index<=int(visible.size())){
        const auto&entry=installed_themes[std::size_t(visible[std::size_t(index-1)])];
        const unsigned count=entry.wallpapers?entry.wallpapers:(entry.complete?4u:1u);
        return entry.display_name+(entry.complete?" [Compatibility ":" [Partial compatibility ")+std::to_string(count)+"/4]";
    }
    if(index==int(visible.size())+1&&picture_background_available()&&picture_background>0)
        return "Picture Background";
    return {};
}
std::vector<std::string> Model::theme_entries() const
{
    std::vector<std::string> rows;rows.reserve(std::size_t(theme_count()));
    for(int i=0;i<theme_count();++i)rows.push_back(theme_entry(i));
    return rows;
}
std::vector<GuideEntry> Model::profile_entries() const
{
    // gamer/Strings.xus (9199/13604), socxzp/strings.xus and MeViewScene (Metro).
    return {{"Achievements",GuideAction::Achievements},
            {"Customize Avatar",(avatar_editable || external_avatar_editor) ? GuideAction::AvatarEdit : GuideAction::Avatar},
            {"Change Theme",GuideAction::SelectTheme},{"Switch Profile",GuideAction::Unavailable}};
}
Effect Model::open_themes()
{
    if (viewer == Viewer::Themes) { guide = false; return {}; }
    theme_previous_viewer=viewer;theme_previous_item=viewer_item;
    theme_previous_full=viewer_full;theme_previous_guide=guide;
    guide=false;
    open_viewer(Viewer::Themes);
    viewer_item=0;
    if(!installed_theme_id.empty())for(std::size_t i=0;i<installed_themes.size();++i)if(installed_themes[i].stable_id==installed_theme_id){viewer_item=theme_row_for_catalog_index(int(i));break;}
    if(picture_background_enabled&&picture_background_available()&&picture_background>0)viewer_item=theme_count()-1;
    return {};
}
#include "welcome_model.inc.hpp"
#include "achievements_model.inc.hpp"

Effect Model::press_viewer(xb::Button button)
{
    if (viewer==Viewer::RomGameGuidePreview) {
        const exbox_rom_guide::Result result=exbox_rom_guide::press(rom_game_guide_preview,button);
        if (result.action==exbox_rom_guide::Action::Close) {
            rom_game_guide_preview.session_lost();
            viewer=Viewer::None;viewer_item=0;
        } else viewer_item=int(rom_game_guide_preview.selected());
        // Preview actions never dispatch transport. Default capabilities make
        // A inert while the corresponding unsupported reason remains visible.
        return {};
    }
    if (viewer==Viewer::Achievements) return press_achievements(button);
    if (viewer==Viewer::WelcomePages || viewer==Viewer::InitialSetup) return press_welcome(button);
    if (viewer == Viewer::Themes || viewer == Viewer::Profile) {
        // The Guide opens over these pages as over every other viewer.
        if (button == xb::Guide || button == xb::Start) { open_guide(); return {}; }
        viewer_item=std::clamp(viewer_item,0,std::max(0,viewer_count()-1));
        // ThemePicker/lstThemes Wrap=true (9199, 13604, 15574, 17559).
        if (viewer==Viewer::Themes && (button==xb::Up || button==xb::Down))
            viewer_item=(viewer_item+(button==xb::Down?1:viewer_count()-1))%viewer_count();
        else if(viewer==Viewer::Themes&&(button==xb::Left||button==xb::Right)){
            constexpr int page=6;
            const int n=viewer_count();
            viewer_item=std::clamp(viewer_item+(button==xb::Right?page:-page),0,n-1);
        }
        else if(viewer==Viewer::Themes&&(button==xb::LB||button==xb::RB)){
            const auto initial=[&](int row){
                const int index=theme_catalog_index_for_row(row);if(index<0)return row==0?'#':'~';
                const auto&name=installed_themes[std::size_t(index)].display_name;
                if(name.empty())return'#';const char c=name.front();return c>='a'&&c<='z'?char(c-'a'+'A'):c;
            };
            const int n=viewer_count();
            if(button==xb::RB){const char from=initial(viewer_item);while(viewer_item+1<n&&initial(viewer_item)==from)++viewer_item;}
            else if(viewer_item>0){--viewer_item;const char group=initial(viewer_item);while(viewer_item>0&&initial(viewer_item-1)==group)--viewer_item;}
        }
        else {
            if (button==xb::Up) viewer_item=std::max(0,viewer_item-1);
            if (button==xb::Down) viewer_item=std::min(viewer_count()-1,viewer_item+1);
        }
        if (button==xb::B) {
            if (viewer==Viewer::Themes) {
                viewer=theme_previous_viewer;viewer_item=theme_previous_item;
                viewer_full=theme_previous_full;guide=theme_previous_guide;
            } else { viewer=Viewer::None;viewer_item=0; }
        }
        if (button==xb::A) {
            if (viewer==Viewer::Themes) {
                theme=0;
                if(viewer_item==0){installed_theme_id.clear();picture_background_enabled=false;return {Command::ApplyInstalledTheme,-1};}
                const int index=theme_catalog_index_for_row(viewer_item);
                if(index>=0){const auto&id=installed_themes[std::size_t(index)].stable_id;theme_recent.erase(std::remove(theme_recent.begin(),theme_recent.end(),id),theme_recent.end());theme_recent.insert(theme_recent.begin(),id);if(theme_recent.size()>16)theme_recent.resize(16);return {Command::ApplyInstalledTheme,index};}
                installed_theme_id.clear();picture_background_enabled=true;return {Command::ApplyInstalledTheme,-2};
            }
            return perform_guide_entry(profile_entries()[std::size_t(viewer_item)]);
        }
        if(viewer==Viewer::Themes&&button==xb::X){
            const int index=theme_catalog_index_for_row(viewer_item);if(index<0)return {};
            const auto&id=installed_themes[std::size_t(index)].stable_id;const auto found=std::find(theme_favorites.begin(),theme_favorites.end(),id);
            if(found==theme_favorites.end()){if(theme_favorites.size()<64)theme_favorites.push_back(id);}else theme_favorites.erase(found);
            if(theme_filter==1)viewer_item=std::clamp(viewer_item,0,std::max(0,theme_count()-1));return {Command::SaveSettings,-1};
        }
        if(viewer==Viewer::Themes&&button==xb::Y){theme_filter=(theme_filter+1)%3;viewer_item=0;return {};}
        return {};
    }
    if (viewer == Viewer::Pictures && picture_background_confirm) return press_picture_full(button);
    if (viewer == Viewer::SystemSettings)
    {
        if (network_page()) return press_network(button);
        if (storage_page()) return press_storage(button);
        const auto entries = system_entries();
        const int last = std::max(0, int(entries.size()) - 1);
        viewer_item = std::clamp(viewer_item, 0, last);
        switch (button)
        {
        case xb::Up: viewer_item = std::max(0, viewer_item - 1); return {};
        case xb::Down: viewer_item = std::min(last, viewer_item + 1); return {};
        case xb::B:
            if (system_page != SystemRoot) {
                if (!system_history.empty()) {
                    const auto previous = system_history.back();
                    system_history.pop_back();
                    system_page = previous.page;
                    viewer_item = previous.item;
                } else { system_page = SystemRoot; viewer_item = 0; }
                return {};
            }
            system_history.clear();
            viewer = Viewer::None;
            return {};
        case xb::A:
        {
            if (entries.empty()) return {};
            if (system_page == SystemHealth || system_page == SystemLibraryStatus ||
                system_page == SystemInfo || system_page == SystemDateTime ||
                system_page == SystemScreenFormat) {
                show(entries[std::size_t(viewer_item)].label,system_description());
                return {};
            }
            if (system_page == SystemGameMusic) {
                const bool enabled=viewer_item==0;
                if(music_game_scan==enabled)return {};
                music_game_scan=enabled;
                return enabled?Effect{Command::ScanGameMusic,-1}:Effect{Command::SaveSettings,-1};
            }
            if (system_page == SystemLocalFriends) {
                const bool enabled=viewer_item==0;
                if(local_friend_avatars==enabled)return {};
                local_friend_avatars=enabled;
                return {Command::SaveSettings,-1};
            }
            if (system_page == SystemMusicVolume) {
                music_volume=std::clamp(viewer_item,0,10);
                if(music_volume>0)music_volume_restore=music_volume;
                return {Command::MusicVolume,music_volume};
            }
            if (system_page == SystemResolution) {
                const int requested=viewer_item==1?2:1;
                if (render_scale==requested) return {};
                render_scale=requested;
                return {Command::SaveRenderScale,requested};
            }
            if (system_page == SystemWelcome) {
                welcome_visible=viewer_item==0;
                if (!welcome_visible && channel==Welcome) channel=MyXbox;
                return {Command::SaveSettings,-1};
            }
            if (system_page == SystemSoundEffects) {
                sounds = viewer_item == 0;
                return {Command::SaveSettings,-1};
            }
            if (system_page == SystemTimeFormat) {
                clock_24_hour = viewer_item == 1;
                return {Command::SaveSettings,-1};
            }
            if (system_page == SystemScreenSaver) {
                dimming = viewer_item == 0;
                return {Command::SaveSettings,-1};
            }
            const GuideEntry entry = entries[std::size_t(viewer_item)];
            if (entry.action == GuideAction::ConsoleSettings) {
                const int page = entry.app < 0 ? int(SystemConsole) : entry.app;
                const int item = page == SystemResolution ? (render_scale==2?1:0)
                               : page == SystemWelcome ? (welcome_visible?0:1)
                               : page == SystemSoundEffects ? (sounds ? 0 : 1)
                               : page == SystemTimeFormat ? (clock_24_hour ? 1 : 0)
                               : page == SystemScreenSaver ? (dimming ? 0 : 1)
                               : page == SystemGameMusic ? (music_game_scan ? 0 : 1)
                               : page == SystemLocalFriends ? (local_friend_avatars ? 0 : 1)
                               : page == SystemMusicVolume ? std::clamp(music_volume,0,10) : 0;
                open_system_page(page,item);
                if (page==StorageDevices) {storage_device=-1;storage_item=-1;return {Command::ScanStorage,-1};}
                return {};
            }
            if (entry.action == GuideAction::Unavailable) {
                std::string detail=system_meta(entry.label);
                if(detail.empty()) detail=system_description();
                if(detail.empty()) detail="This setting is managed by PS5 or unavailable to the current Ex-Box runtime.";
                show(entry.label,detail);
                return {};
            }
            // The rest do what their Guide entries do, from here.
            const bool was_open = guide;
            guide = true;
            const int saved_blade = guide_blade;
            Effect e = perform_guide_entry(entry);
            if (!was_open && guide) guide = false;
            guide_blade = saved_blade;
            return e;
        }
        default: return {};
        }
    }
    const int n = viewer_count();
    viewer_item = std::clamp(viewer_item, 0, std::max(0, n - 1));
    const int last = std::max(0, n - 1);
    if (viewer==Viewer::AdsSettings)
    {
        const int profile=current_ad_profile();
        auto &mode=ad_mode[profile];
        auto &selected=ad_selected[profile];
        switch (button)
        {
        case xb::Up: viewer_item=std::max(0,viewer_item-1);return {};
        case xb::Down: viewer_item=std::min(last,viewer_item+1);return {};
        case xb::LB: viewer_item=std::max(0,viewer_item-10);return {};
        case xb::RB: viewer_item=std::min(last,viewer_item+10);return {};
        case xb::LT: viewer_item=0;return {};
        case xb::RT: viewer_item=last;return {};
        case xb::X:
            ad_show_all=!ad_show_all;
            viewer_item=0;
            return {};
        case xb::B:
            viewer=ad_previous_viewer;viewer_item=ad_previous_item;viewer_full=ad_previous_full;
            open_guide();guide_host=ad_return_host;guide_blade=BladeSettings;guide_item[BladeSettings]=ad_return_item;
            return {};
        case xb::Left:
        case xb::Right:
            if (viewer_item!=0) return {};
            mode=(mode+(button==xb::Right?1:3))%4;break;
        case xb::A:
            if (viewer_item==0) mode=(mode+1)%4;
            else if (viewer_item==1) {mode=2;++ad_shuffle_seed[profile];}
            else if (viewer_item==2) {
                // Batch picks exclude known fragments without removing any
                // resource the owner deliberately selected in All resources.
                for (const auto &entry:ad_catalog) {
                    if (entry.auto_eligible && std::find(selected.begin(),selected.end(),entry.id)==selected.end()) {
                        selected.push_back(entry.id);
                    }
                }
                if (mode==0 || mode==3) mode=1;
            }
            else if (viewer_item==3) {selected.clear();if (mode==0 || mode==3) mode=1;}
            else if (viewer_item==4) {mode=0;selected.clear();ad_shuffle_seed[profile]=0;}
            else {
                const int index=ad_catalog_index(viewer_item-5);
                if (index<0) return {};
                const auto &id=ad_catalog[size_t(index)].id;
                const auto found=std::find(selected.begin(),selected.end(),id);
                if (found==selected.end()) selected.push_back(id);else selected.erase(found);
                if (mode==0 || mode==3) mode=1;
            }
            break;
        case xb::Guide:
        case xb::Start: open_guide();return {};
        default:return {};
        }
        promotions=true;clamp_all();return {Command::SaveAdProfiles,-1};
    }
    if (viewer==Viewer::DashboardEras || viewer==Viewer::DashboardUpdates)
    {
        switch (button)
        {
        case xb::Up: viewer_item=std::max(0,viewer_item-1); return {};
        case xb::Down: viewer_item=std::min(last,viewer_item+1); return {};
        case xb::LB: viewer_item=std::max(0,viewer_item-8); return {};
        case xb::RB: viewer_item=std::min(last,viewer_item+8); return {};
        case xb::LT: viewer_item=0; return {};
        case xb::RT: viewer_item=last; return {};
        case xb::X: picker_previews=!picker_previews;viewer_item=viewer==Viewer::DashboardEras?viewer_item:0;return {};
        case xb::B:
            if (viewer==Viewer::DashboardUpdates) {viewer=Viewer::DashboardEras;viewer_item=picker_family;}
            else {viewer=picker_previous_viewer;viewer_item=picker_previous_item;viewer_full=picker_previous_full;open_guide();guide_host=picker_return_host;guide_blade=BladeSettings;guide_item[BladeSettings]=picker_return_item;}
            return {};
        case xb::A:
            if (viewer==Viewer::DashboardEras)
            {
                picker_family=viewer_item;
                const auto updates=updates_for(picker_family,picker_previews);
                viewer=Viewer::DashboardUpdates;viewer_item=0;
                const int current=dashboard_family(era,dashboard_build)==picker_family?dashboard_build:kDashboardFamilies[picker_family].preferred;
                for (size_t i=0;i<updates.size();i++) if (updates[i].build==current) viewer_item=int(i);
                return {};
            }
            {
                const auto updates=updates_for(picker_family,picker_previews);
                if (viewer_item>=int(updates.size())) return {};
                era=kDashboardFamilies[picker_family].era;
                dashboard_build=updates[size_t(viewer_item)].build;
                viewer=Viewer::None;guide=false;
                go(MyXbox);
                return {Command::SaveSettings,-1};
            }
        default: return {};
        }
    }
    if (button == xb::Guide || button == xb::Start)
    {
        open_guide(); // over the viewer; B in the Guide comes back to it
        return {};
    }
    if (viewer == Viewer::Library)
    {
        switch (button)
        {
        case xb::B: viewer = Viewer::None; return {};
        case xb::Up: viewer_item = std::max(0, viewer_item - 1); return {};
        case xb::Down: viewer_item = std::min(last, viewer_item + 1); return {};
        case xb::LB: viewer_item = std::max(0, viewer_item - 8); return {};
        case xb::RB: viewer_item = std::min(last, viewer_item + 8); return {};
        case xb::LT: viewer_item = 0; return {};
        case xb::RT: viewer_item = last; return {};
        case xb::Left:
        case xb::Right:
            if (collection == Collection::All)
            {
                library_filter = (library_filter + (button == xb::Right ? 1 : 27)) % 28;
                viewer_item = 0;
            }
            return {};
        case xb::X: return {Command::Refresh, -1};
        case xb::Y:
            if (n > 0)
            {
                const Card c = library_cards()[std::size_t(viewer_item)];
                // Supplied Xbox 360 recents are deliberately inert until the
                // cooperative launch/return contract is accepted. They are
                // not missing pin attachments, so never route them through
                // open_info()'s pin-only missing-card fallback.
                if (c.game >= 0 || c.app >= 0) open_info(c);
            }
            return {};
        case xb::A:
            if (n > 0)
            {
                const Card c = library_cards()[std::size_t(viewer_item)];
                if (c.game >= 0) return {Command::LaunchTitle, c.game};
                if (c.app >= 0) return {Command::Launch, c.app};
            }
            return {};
        default: return {};
        }
    }
    if (viewer == Viewer::AvatarEdit)
    {
        // The editor: Up / Down choose a category, Left / Right its choice,
        // LB / RB turn the avatar, X surprises, Y emotes, A keeps, B puts it back.
        const int step = std::max(1, n / 8);
        switch (button)
        {
        case xb::Up: avatar_category = std::max(0, avatar_category - 1); return {};
        case xb::Down: avatar_category = std::min(std::max(0, avatar_categories - 1), avatar_category + 1); return {};
        case xb::Left: return {Command::AvatarChange, -1};
        case xb::Right: return {Command::AvatarChange, 1};
        case xb::LB: if (n > 0) viewer_item = (viewer_item + n - step) % n; return {};
        case xb::RB: if (n > 0) viewer_item = (viewer_item + step) % n; return {};
        case xb::LT:
        case xb::RT: viewer_item = 0; return {};
        case xb::X: return {Command::AvatarRandom, -1};
        case xb::Y: return {Command::AvatarEmote, -1};
        case xb::A: viewer = Viewer::Avatar; return {Command::AvatarSave, -1};
        case xb::B: viewer = Viewer::Avatar; return {Command::AvatarCancel, -1};
        default: return {};
        }
    }
    if (viewer == Viewer::Avatar)
    {
        // The stage: Left / Right turn the avatar all the way round, A waves.
        const int step = std::max(1, n / 8);
        switch (button)
        {
        case xb::B: viewer = Viewer::None; return {};
        case xb::Y:
            if (external_avatar_editor) return {Command::OpenAvatarEditor, -1};
            if (!avatar_editable)
                return {};
            viewer = Viewer::AvatarEdit;
            avatar_category = 0;
            return {Command::AvatarEditStart, -1};
        case xb::A: return n > 0 ? Effect{Command::AvatarWave, -1} : Effect{};
        case xb::Left: if (n > 0) viewer_item = (viewer_item + n - 1) % n; return {};
        case xb::Right: if (n > 0) viewer_item = (viewer_item + 1) % n; return {};
        case xb::LB: if (n > 0) viewer_item = (viewer_item + n - step) % n; return {};
        case xb::RB: if (n > 0) viewer_item = (viewer_item + step) % n; return {};
        case xb::LT:
        case xb::RT: viewer_item = 0; return {}; // face front again
        default: return {};
        }
    }
    if (viewer == Viewer::Music && music_page != MusicLibrary)
        return press_music_page(button);
    if (viewer == Viewer::Music)
    {
        // A list: Up / Down choose, A opens a song's page (or an album ...),
        // Left / Right skip, X pauses, Y shows Now Playing.
        const bool playing = now_playing >= 0;
        const int actions = int(music_group_actions().size());
        // The view selector is reachable even when its list is empty. Landing
        // on a view activates it, so Down always enters the displayed list.
        if (music_tabs_focused) {
            if (music_random_focused && button == xb::A)
                return {Command::MusicEdit, int(MusicOp::RandomAlbum)};
            if (music_game_scan_focused && button == xb::A) {
                music_game_scan=true;
                return {Command::ScanGameMusic,-1};
            }
            if (button == xb::Left || button == xb::Right) {
                const int focus=music_game_scan_focused?kMusicViews+1:music_random_focused?kMusicViews:music_view;
                const int next=(focus+(button==xb::Right?1:kMusicViews+1))%(kMusicViews+2);
                music_random_focused=next==kMusicViews;
                music_game_scan_focused=next==kMusicViews+1;
                if(music_random_focused||music_game_scan_focused)return {};
                music_view=next;
                music_group = -1;
                viewer_item = 0;
                return {Command::MusicBrowse, -1};
            }
            if (button == xb::Down || button == xb::A) {
                music_tabs_focused = false;
                viewer_item = 0;
                return {};
            }
            if (button == xb::Up) return {};
            if (button == xb::B) music_tabs_focused = false;
        }
        switch (button)
        {
        case xb::B:
            if (music_view > 0 && music_group >= 0)
            {
                viewer_item = music_group; // back on the album / artist just left
                music_group = -1;
                return {Command::MusicBrowse, -1};
            }
            viewer = Viewer::None;
            return {};
        case xb::LT:
        case xb::RT:
            // The PS5 keeps Create (Back), so the triggers turn the view:
            // Songs, Albums, Artists, Genres.
            music_random_focused=false;music_game_scan_focused=false;
            music_view = (music_view + (button == xb::RT ? 1 : kMusicViews - 1)) % kMusicViews;
            music_group = -1;
            viewer_item = 0;
            return {Command::MusicBrowse, -1};
        case xb::Up:
            if (viewer_item == 0) {music_tabs_focused = true;music_random_focused=false;music_game_scan_focused=false;}
            else viewer_item = std::max(0, viewer_item - 1);
            return {};
        case xb::Down: viewer_item = std::min(last, viewer_item + 1); return {};
        case xb::LB: viewer_item = std::max(0, viewer_item - kMusicRows); return {};
        case xb::RB: viewer_item = std::min(last, viewer_item + kMusicRows); return {};
        case xb::A:
            if (n == 0)
                return {};
            if (music_in_groups())
            {
                music_group = viewer_item;
                viewer_item = 0;
                return {Command::MusicBrowse, -1};
            }
            if (viewer_item < actions)
            {
                // The opened group's options: Play, Add to Current Playlist,
                // then Choose Cover (an album) or Delete (a saved playlist).
                music_target = music_group;
                if (viewer_item == 0)
                {
                    music_paused = false;
                    return {Command::MusicEdit, int(MusicOp::PlayGroup)};
                }
                if (viewer_item == 1)
                    return {Command::MusicEdit, int(MusicOp::AddGroup)};
                music_library_item = viewer_item;
                viewer_item = 0;
                if (music_view==1 && music_library_item==3) {
                    music_page=MusicAlbumInfo;music_album_field=-1;
                    return {Command::MusicEdit,int(MusicOp::OpenAlbumInfo)};
                }
                if (music_view == 4)
                {
                    music_page = MusicConfirmDelete;
                    return {Command::MusicBrowse, -1};
                }
                music_cover_from = MusicLibrary;
                music_page = MusicChooseCover;
                return {Command::MusicEdit, int(MusicOp::OpenCover)};
            }
            // A song: its page (Play Song, Add to Current Playlist, Choose Cover).
            music_target = viewer_item;
            music_library_item = viewer_item;
            music_page = MusicSong;
            viewer_item = 0;
            return {Command::MusicEdit, int(MusicOp::OpenSong)};
        case xb::X:
            if (!playing)
                return {};
            music_paused = !music_paused;
            return {Command::MusicPause, -1};
        case xb::Y: // Now Playing (music/1003_IndividualDevice.xur legend_Y)
            music_tabs_focused = false;
            music_library_item = viewer_item;
            music_page = MusicNowPlaying;
            viewer_item = 0;
            return {Command::MusicBrowse, -1};
        case xb::Left: return playing ? Effect{Command::MusicPrevious, -1} : Effect{};
        case xb::Right: return playing ? Effect{Command::MusicNext, -1} : Effect{};
        // The face buttons and triggers are taken: the sticks' clicks set
        // the order (both at once still leave for PS5 Home).
        case xb::LS:
            if (n == 0)
                return {};
            music_shuffle = !music_shuffle;
            return {Command::MusicShuffle, -1};
        case xb::RS:
            if (n == 0)
                return {};
            music_repeat = (music_repeat + 1) % 3;
            return {Command::MusicRepeat, -1};
        default: return {};
        }
    }
    if (viewer == Viewer::Videos)
    {
        if (viewer_full)
            return press_video_full(button);
        if (!video_pending.empty())
        {
            // Freeze the grid while choosing; B leaves the bookmark untouched.
            if (video_key() != video_pending || button == xb::B)
                video_pending.clear();
            else if (button == xb::Up || button == xb::Down)
                video_start_item = button == xb::Up ? 0 : 1;
            else if (button == xb::A)
            {
                enter_video(video_start_item == 0);
                return {Command::SaveVideos, -1};
            }
            return {};
        }
        if (button == xb::A && n > 0) {
            video_theater=false;
            return request_video();
        }
        if (button == xb::RS && n > 0) {
            video_theater=true;
            return request_video();
        }
        if (button == xb::X)
        {
            // Sort, keeping the same video selected.
            const std::string key = video_key();
            video_order = (video_order + 1) % kVideoOrderCount;
            const auto list = video_list();
            const auto it = std::find(list.begin(), list.end(), key);
            viewer_item = it == list.end() ? 0 : int(it - list.begin());
            return {Command::SaveSettings, -1};
        }
        if (button == xb::Y)
            return {Command::RefreshMedia, -1};
    }
    if (viewer == Viewer::Pictures)
    {
        if (viewer_full)
            return press_picture_full(button);
        switch (button)
        {
        case xb::A:
            if (n == 0)
                return {};
            if (picture_groups())
            {
                picture_group = viewer_item; // open the game
                viewer_item = 0;
                return {};
            }
            viewer_full = true;
            reset_picture_view();
            return {};
        case xb::B:
            if (picture_group < 0)
                break;
            viewer_item = picture_group; // back to the list of games, on this one
            picture_group = -1;
            return {};
        case xb::X:
        {
            // Sort, keeping the same picture (or its game) selected.
            const PictureFile *was = picture();
            const std::string path = was ? was->path : std::string(), game = was ? was->label() : std::string();
            picture_order = (picture_order + 1) % kPictureOrderCount;
            picture_group = -1;
            viewer_item = 0;
            if (picture_groups())
            {
                const auto games = picture_games();
                const auto it = std::find(games.begin(), games.end(), game);
                viewer_item = it == games.end() ? 0 : int(it - games.begin());
            }
            else
            {
                const auto list = picture_list();
                for (size_t i = 0; i < list.size(); ++i)
                    if (picture_files[size_t(list[i])].path == path)
                        viewer_item = int(i);
            }
            return {Command::SaveSettings, -1};
        }
        case xb::Y:
            return n > 0 && !picture_groups() ? press_picture_full(button) : Effect{};
        case xb::RS: return {Command::ScanPictures, -1}; // Ex-Box addition: no 360 original; refresh captures
        default: break;
        }
    }
    if (viewer_full)
    {
        switch (button)
        {
        case xb::B: viewer_full = false; return {};
        case xb::Left:
        case xb::LB: viewer_item = std::max(0, viewer_item - 1); return {};
        case xb::Right:
        case xb::RB: viewer_item = std::min(last, viewer_item + 1); return {};
        default: return {};
        }
    }
    switch (button)
    {
    case xb::B:
        viewer = Viewer::None;
        return {};
    case xb::A:
        if (n > 0)
            viewer_full = true;
        return {};
    case xb::Left:
        // Picture/video tiles are a four-column grid. Do not jump diagonally
        // from the first tile of one row to the last tile of the row above.
        if (viewer_item % kViewerColumns) --viewer_item;
        return {};
    case xb::Right:
        // Likewise, the right edge stays put; Down is the spatial route into
        // the next row. Shoulder paging remains only an optional shortcut.
        if (viewer_item % kViewerColumns + 1 < kViewerColumns && viewer_item < last) ++viewer_item;
        return {};
    case xb::Up:
        if (viewer_item >= kViewerColumns)
            viewer_item -= kViewerColumns;
        return {};
    case xb::Down:
        if (viewer_item + kViewerColumns <= last)
            viewer_item += kViewerColumns;
        else if (viewer_item / kViewerColumns < last / kViewerColumns)
            viewer_item = last; // a short last row
        return {};
    case xb::LB: viewer_item = std::max(0, viewer_item - kViewerColumns * kViewerRows); return {};
    case xb::RB: viewer_item = std::min(last, viewer_item + kViewerColumns * kViewerRows); return {};
    case xb::LT: viewer_item = 0; return {};
    case xb::RT: viewer_item = last; return {};
    default: return {};
    }
}

Effect Model::press_guide(xb::Button button)
{
    // The original Guide exposes a modal music-volume slider. It controls only
    // Ex-Box's music bus; games and the PS5 system mixer are never touched.
    if (guide_volume_open)
    {
        if (button == xb::Guide || button == xb::Start) { guide_volume_open=false; guide=false; return {}; }
        if (button == xb::B || button == xb::Up || button == xb::Down) { guide_volume_open=false; return {}; }
        if (button == xb::Left || button == xb::Right)
        {
            music_volume=std::clamp(music_volume+(button==xb::Right?1:-1),0,10);
            if (music_volume>0) music_volume_restore=music_volume;
            return {Command::MusicVolume,music_volume};
        }
        if (button == xb::A)
        {
            if (music_volume>0) { music_volume_restore=music_volume; music_volume=0; }
            else music_volume=std::clamp(music_volume_restore,1,10);
            return {Command::MusicVolume,music_volume};
        }
        return {};
    }
    if (guide_music_page==4 && guide_music_search_edit)
    {
        if (button==xb::Left || button==xb::Right) {guide_music_key=(guide_music_key+(button==xb::Right?1:26))%27;return {};}
        if (button==xb::Up || button==xb::Down) {guide_music_key=(guide_music_key+(button==xb::Down?9:18))%27;return {};}
        if (button==xb::A) {if(guide_music_query.size()<32)guide_music_query+=guide_music_key<26?char('A'+guide_music_key):' ';return {Command::GuideMusicRefresh,guide_music_page};}
        if (button==xb::X) {if(!guide_music_query.empty())guide_music_query.pop_back();return {Command::GuideMusicRefresh,guide_music_page};}
        if (button==xb::Y || button==xb::B) {guide_music_search_edit=false;return {};}
        if (button==xb::Guide || button==xb::Start) {guide_music_search_edit=false;guide=false;return {};}
        return {};
    }
    // The retired R3 Utilities overlay no longer exists. RS is intentionally
    // inert in the Guide; all features are normal blade rows.
    if(button==xb::RS)return {};
    if(guide_music_page>0&&button==xb::B){guide_music_page=0;guide_music_search_edit=false;guide_item[BladeMedia]=0;return {};}
    if(guide_exbox_page&&button==xb::B){
        guide_exbox_page=false;
        const auto home=blade_entries(BladeHome);
        const auto row=std::find_if(home.begin(),home.end(),[](const GuideEntry &entry){return entry.action==GuideAction::OpenExBoxMenu;});
        guide_item[BladeHome]=row==home.end()?0:int(row-home.begin());
        return {};
    }
    const std::vector<GuideEntry> entries = blade_entries(guide_blade);
    const int items = std::max(1, int(entries.size()));
    int &item = guide_item[guide_blade];
    item = std::clamp(item, 0, items - 1);
    const auto record_at=std::find_if(entries.begin(),entries.end(),[](const GuideEntry&entry){return entry.action==GuideAction::RecordScreen;});
    const int record_index=record_at==entries.end()?-1:int(record_at-entries.begin());
    // Recording is a normal Media row. Left/Right stays inside its horizontal
    // transport controls; Up/Down continues through the ordinary row list.
    if(record_index>=0&&item==record_index&&recorder_available&&
       (button==xb::Left||button==xb::Right)){
        std::vector<int> controls;
        if(!recorder_recording||recorder_paused)controls.push_back(0);
        if(recorder_recording&&!recorder_paused&&recorder_pause_supported)controls.push_back(1);
        if(recorder_recording)controls.push_back(2);
        if(!controls.empty()){
            auto at=std::find(controls.begin(),controls.end(),recorder_control);
            int index=at==controls.end()?0:int(at-controls.begin());
            index=std::clamp(index+(button==xb::Right?1:-1),0,int(controls.size())-1);
            recorder_control=controls[std::size_t(index)];
        }
        return {};
    }
    // The authored Media mini-player is one horizontal icon row. Its entries
    // remain in scene order for rendering, but Left/Right traverse the visible
    // icons instead of unexpectedly switching Guide blades. Up/Down enter and
    // leave the adjacent content row, so every visible action is D-pad-only.
    if(guide_music_page==0&&guide_blade==BladeMedia&&media_player&&now_playing>=0){
        const auto control=[](GuideAction action){return action==GuideAction::MusicPause||action==GuideAction::MusicPrevious||action==GuideAction::MusicStop||action==GuideAction::MusicNext||action==GuideAction::MusicShuffle||action==GuideAction::MusicVolume;};
        std::vector<int> controls;for(std::size_t i=0;i<entries.size();++i)if(control(entries[i].action))controls.push_back(int(i));
        if(!controls.empty()){
            guide_media_control_item=std::clamp(guide_media_control_item,0,int(controls.size())-1);
            const auto found=std::find(controls.begin(),controls.end(),item);const bool on_icons=found!=controls.end();
            if(on_icons){guide_media_control_item=int(found-controls.begin());if(button==xb::Left||button==xb::Right){guide_media_control_item=std::clamp(guide_media_control_item+(button==xb::Right?1:-1),0,int(controls.size())-1);item=controls[std::size_t(guide_media_control_item)];return {};}if(button==xb::Up){if(controls.front()>0)item=controls.front()-1;return {};}if(button==xb::Down){if(controls.back()+1<int(entries.size()))item=controls.back()+1;return {};}}
            else if(button==xb::Down&&item==controls.front()-1){item=controls[std::size_t(guide_media_control_item)];return {};}
            else if(button==xb::Up&&item==controls.back()+1){item=controls[std::size_t(guide_media_control_item)];return {};}
        }
    }
    if(guide_exbox_page&&(button==xb::Left||button==xb::Right||button==xb::LB||button==xb::RB))return {};
    if (era==0 && guide_blade==BladeHome) {
        if (button==xb::Left || button==xb::Right) {
            if (item<3) {item=std::clamp(item+(button==xb::Right?1:-1),0,2);guide_community_item=item;}
            return {};
        }
        if (button==xb::Down && item<3) {guide_community_item=item;item=3;return {};}
        if (button==xb::Up && item==3) {item=guide_community_item;return {};}
    }
    if (era==0 && button==xb::B && guide_blade!=BladeHome) {guide_blade=BladeHome;return {};}
    switch (button)
    {
    case xb::Up: item = std::max(0, item - 1); return {};
    case xb::Down: item = std::min(items - 1, item + 1); return {};
    case xb::LB:
    case xb::Left: guide_blade = std::max(era==3 && !metro_v1()?int(BladeGames):0, guide_blade - 1); return {};
    case xb::RB:
    case xb::Right: guide_blade = std::min(BladeCount - 1, guide_blade + 1); return {};
    case xb::B:
    case xb::Guide:
    case xb::Start: guide = false; return {};
    case xb::Y: // "Xbox Dashboard"
        guide = false;
        go(MyXbox);
        return {};
    case xb::A: break;
    default: return {};
    }
    if (entries.empty())
        return {};
    return perform_guide_entry(entries[std::size_t(item)]);
}

Effect Model::perform_guide_entry(const GuideEntry &entry)
{
    if(entry.action==GuideAction::AvatarEdit && external_avatar_editor) return {Command::OpenAvatarEditor,-1};
    int &item = guide_item[guide_blade];
    if (entry.action==GuideAction::OpenGuideSettings) {guide_blade=BladeSettings;return {};}
    if(entry.action==GuideAction::OpenExBoxMenu){guide_exbox_page=true;guide_blade=BladeHome;guide_item[BladeHome]=0;return {};}
    // Settings that change in place keep the Guide open, so the change shows.
    if (entry.action == GuideAction::ChangeGamerpic)
        return {Command::NextGamerpic, -1};
    if (entry.action == GuideAction::ToggleSounds)
    {
        sounds = !sounds;
        return {Command::SaveSettings, -1};
    }
    if (entry.action == GuideAction::InitialSetup) return open_setup();
    if (entry.action == GuideAction::SelectTheme) return open_themes();
    if (entry.action == GuideAction::MusicVolume)
    {
        guide_volume_open=true;
        return {};
    }
    if (entry.action==GuideAction::Music && guide)
    {
        guide_host=false;guide_blade=BladeMedia;guide_music_page=5;guide_item[BladeMedia]=0;
        return {Command::GuideMusicRefresh,5};
    }
    if (entry.action==GuideAction::GuideMusicAlbums || entry.action==GuideAction::GuideMusicSongs ||
        entry.action==GuideAction::GuideMusicQueue || entry.action==GuideAction::GuideMusicSearch)
    {
        guide_music_page=entry.action==GuideAction::GuideMusicAlbums?1:entry.action==GuideAction::GuideMusicSongs?2:entry.action==GuideAction::GuideMusicQueue?3:4;
        guide_music_search_edit=entry.action==GuideAction::GuideMusicSearch;
        guide_item[BladeMedia]=0;
        return {Command::GuideMusicRefresh,guide_music_page};
    }
    if (entry.action==GuideAction::GuideMusicBack)
    {
        guide_music_page=0;guide_music_search_edit=false;guide_item[BladeMedia]=0;return {};
    }
    if (entry.action==GuideAction::GuideMusicPlay)
        return {Command::GuideMusicPlay,entry.app};
    if (entry.action==GuideAction::GuideMusicOpenFull)
    {
        guide=false;guide_host=false;guide_music_page=0;return open_viewer(Viewer::Music);
    }
    if (entry.action == GuideAction::CycleEra)
    {
        picker_return_item=item;picker_return_host=guide_host;
        picker_previous_viewer=viewer;picker_previous_item=viewer_item;picker_previous_full=viewer_full;
        guide=false;
        open_viewer(Viewer::DashboardEras);
        viewer_item=dashboard_family(era,dashboard_build);
        return {};
    }
    if (entry.action==GuideAction::AdsSettings)
    {
        ad_return_item=item;ad_return_host=guide_host;
        if (viewer!=Viewer::AdsSettings) {ad_previous_viewer=viewer;ad_previous_item=viewer_item;ad_previous_full=viewer_full;}
        guide=false;return open_viewer(Viewer::AdsSettings);
    }
    if (entry.action == GuideAction::CycleButtonStyle)
    {
        button_style = (std::clamp(button_style, 0, 2) + 1) % 3;
        return {Command::SaveSettings, -1};
    }
    if (entry.action == GuideAction::ToggleDimming)
    {
        dimming = !dimming;
        return {Command::SaveSettings, -1};
    }
    if (entry.action == GuideAction::CycleLibraryOrder)
    {
        library_order = (std::clamp(library_order, 0, 2) + 1) % 3;
        return {Command::SaveSettings, -1};
    }
    if (entry.action == GuideAction::CycleNotifications)
    {
        notification_level = (std::clamp(notification_level, 0, 3) + 1) % 4;
        return {Command::SaveSettings, -1};
    }
    if (entry.action==GuideAction::TogglePromotions)
    {
        promotion_mode=promotions?2:0;promotions=promotion_mode!=2;clamp_all();return {Command::ReloadPromotions,-1};
    }
    if(entry.action==GuideAction::CyclePromotionMode)
    {
        promotion_mode=(std::clamp(promotion_mode,0,2)+1)%3;
        promotions=promotion_mode!=2;clamp_all();return {Command::ReloadPromotions,-1};
    }
    // The music controls work in place too; the list may get shorter.
    if (entry.action == GuideAction::MusicPause || entry.action == GuideAction::MusicNext ||
        entry.action == GuideAction::MusicStop || entry.action == GuideAction::MusicPrevious ||
        entry.action == GuideAction::MusicShuffle)
    {
        Effect e{Command::MusicNext, -1};
        if (entry.action == GuideAction::MusicPrevious)
            e.command = Command::MusicPrevious;
        else if (entry.action == GuideAction::MusicShuffle)
        {
            music_shuffle = !music_shuffle;
            e.command = Command::MusicShuffle;
        }
        else if (entry.action == GuideAction::MusicPause)
        {
            music_paused = !music_paused;
            e.command = Command::MusicPause;
        }
        else if (entry.action == GuideAction::MusicStop)
        {
            now_playing = -1;
            music_paused = false;
            e.command = Command::MusicStop;
        }
        item = std::min(item, std::max(0, int(blade_entries(guide_blade).size()) - 1));
        return e;
    }
    guide = false;
    switch (entry.action)
    {
    case GuideAction::MyXbox: go(MyXbox); return {};
    case GuideAction::Games: go(Games); return {};
    case GuideAction::Apps: go(Apps); return {};
    case GuideAction::Media: go(Media); return {};
    case GuideAction::LibrarySummary:
    {
        std::string summary = "Games: " + std::to_string(games.size()) +
            "   Apps: " + std::to_string(apps.size()) +
            "\nAttached to My Xbox: " + std::to_string(pins.size()) +
            "\nRecently played:";
        int shown = 0;
        for (const std::string &key : recent)
        {
            const int g = game_index(games, key);
            const int a = g < 0 ? app_index(apps, key) : -1;
            if (g >= 0) summary += "\n" + games[std::size_t(g)].title;
            else if (a >= 0) summary += "\n" + apps[std::size_t(a)].title;
            else continue;
            if (++shown == 3) break;
        }
        if (!shown) summary += " None";
        show("Library Summary", summary);
        return {};
    }
    case GuideAction::NotificationHistory:
    {
        std::string history;
        for (std::size_t i = 0; i < std::min<std::size_t>(3, notification_history.size()); ++i)
        {
            const Notification &note = notification_history[i];
            if (!history.empty()) history += "\n";
            history += note.title;
            if (!note.text.empty()) history += ": " + note.text;
        }
        show("Notifications This Session", history.empty() ? "No notifications yet." : history);
        return {};
    }
    case GuideAction::Refresh: return {Command::Refresh, -1};
    case GuideAction::NetworkSettings:
        network_from_blades=era==0 && viewer!=Viewer::SystemSettings;
        if (network_from_blades) { system_history.clear();system_page=NetworkMain;viewer_item=0; }
        else open_system_page(NetworkMain);
        guide=false;viewer=Viewer::SystemSettings;viewer_full=false;
        network={};network_test={};++network_generation;
        return {Command::ReadNetwork,-1};
    case GuideAction::SystemSettings:
        system_page=SystemRoot;system_history.clear();return open_viewer(Viewer::SystemSettings);
    case GuideAction::ControllerMap: controller_map = true; return {};
    case GuideAction::ExitToPs5: return {Command::ExitToPs5, -1};
    case GuideAction::Launch: return {Command::Launch, entry.app};
    case GuideAction::LaunchTitle: return {Command::LaunchTitle, entry.app};
    case GuideAction::Unavailable: show(entry.label, "Not available on PS5 yet."); return {};
    case GuideAction::InitialSetup:
    case GuideAction::ChangeGamerpic:
    case GuideAction::ToggleSounds:
    case GuideAction::SelectTheme:
    case GuideAction::CycleEra:
    case GuideAction::ToggleDimming:
    case GuideAction::CycleLibraryOrder:
    case GuideAction::CycleNotifications:
    case GuideAction::CycleButtonStyle:
    case GuideAction::TogglePromotions:
    case GuideAction::CyclePromotionMode:
    case GuideAction::OpenGuideSettings:
    case GuideAction::AdsSettings:
    case GuideAction::MusicPause:
    case GuideAction::MusicNext:
    case GuideAction::MusicPrevious:
    case GuideAction::MusicShuffle:
    case GuideAction::MusicStop:
    case GuideAction::MusicVolume:
    case GuideAction::GuideMusicAlbums:
    case GuideAction::GuideMusicSongs:
    case GuideAction::GuideMusicQueue:
    case GuideAction::GuideMusicSearch:
    case GuideAction::GuideMusicPlay:
    case GuideAction::GuideMusicOpenFull:
    case GuideAction::GuideMusicBack:
    case GuideAction::OpenExBoxMenu: return {};
    case GuideAction::Music: return open_viewer(Viewer::Music);
    case GuideAction::AvatarEdit:
        viewer = Viewer::AvatarEdit;
        viewer_item = 0;
        viewer_full = false;
        avatar_category = 0;
        return {Command::AvatarEditStart, -1};
    case GuideAction::Pictures: return open_viewer(Viewer::Pictures);
    case GuideAction::Videos: return open_viewer(Viewer::Videos);
    case GuideAction::RecordScreen:
        if(recorder_available){
            if(recorder_busy){show("Screen Recording","The recorder is responding. You can keep using the dashboard while it finishes.");return {};}
            if(recorder_error){show("Screen Recording","Glint reported a recording error. Stop the current recording or restart Glint, then try again.");return {};}
            if(!recorder_recording&&recorder_control==0)return {Command::RecorderAction,0};
            if(recorder_recording&&recorder_paused&&recorder_control==0)return {Command::RecorderAction,2};
            if(recorder_recording&&!recorder_paused&&recorder_pause_supported&&recorder_control==1)return {Command::RecorderAction,1};
            if(recorder_recording&&recorder_control==2)return {Command::RecorderAction,3};
            return {};
        }
        show("Record Screen",
             "Direct recording is ready automatically when the optional Glint companion is running.\n\n"
             "Press the DualSense Create button, then choose Start New Recording.\n\n"
             "Open Create again and choose Stop Recording. Your clip will appear in Media Gallery.\n\n"
             "For 4K recording, enable it first in PS5 Settings > Captures and Broadcasts.");
        return {};
    case GuideAction::LaunchSystem: return {Command::LaunchSystem, entry.app};
    case GuideAction::Achievements: return open_achievements();
    case GuideAction::Profile: return open_viewer(Viewer::Profile);
    case GuideAction::Avatar: return open_viewer(Viewer::Avatar);
    case GuideAction::RecentGames: return open_collection(Collection::Recent);
    case GuideAction::AllGames: return open_collection(Collection::All);
    case GuideAction::ConsoleSettings: guide=true; guide_blade=BladeSettings; return {};
    }
    return {};
}

std::string Model::blades_page_title() const
{
    constexpr const char *titles[] = {"System", "Console Settings", "Memory", "Hard Drive", "Family Settings", "Console Controls"};
    return titles[std::clamp(blades_page, 0, 5)];
}

void Model::close_blades_page()
{
    const int from = blades_page;
    blades_page = from == 3 ? 2 : from == 5 ? 4 : 0;
    selected[MyXbox] = blades_page_return[std::clamp(from, 0, 5)];
}

Effect Model::run_blades_item(const GuideEntry &entry)
{
    // The System blade opens its second and third levels in place.
    if (era == 0 && channel == MyXbox)
    {
        const int next = blades_page == 0 && entry.action == GuideAction::ConsoleSettings ? 1
                       : blades_page == 0 && entry.label == "Memory"                    ? 2
                       : blades_page == 2                                               ? 3
                       : blades_page == 0 && entry.label == "Family Settings"           ? 4
                       : blades_page == 4 && entry.label == "Console Controls"          ? 5
                                                                                        : -1;
        if (next > 0)
        {
            blades_page_return[next] = selected[MyXbox];
            blades_page = next;
            selected[MyXbox] = 0;
            return next == 3 ? Effect{Command::ScanMusic, -1} : Effect{};
        }
    }
    // Signed out, the gamercard (and Create Gamer Profile) sign back in.
    if (blades_signed_out && (entry.label==kBladesGamercard || entry.label=="Create Gamer Profile"))
    {
        blades_signed_out=false;
        clamp_all();
        return {};
    }
    switch (entry.action)
    {
    case GuideAction::RecentGames: return open_collection(Collection::Recent);
    case GuideAction::AllGames: return open_collection(Collection::All);
    case GuideAction::ConsoleSettings: open_guide(); guide_blade=BladeSettings; return {};
    case GuideAction::CycleEra:
        picker_return_item=0;
        picker_previous_viewer=viewer;picker_previous_item=viewer_item;picker_previous_full=viewer_full;
        open_viewer(Viewer::DashboardEras);
        viewer_item=dashboard_family(era,dashboard_build);
        return {};
    case GuideAction::Unavailable: show(entry.label, "Not available on PS5 yet."); return {};
    default:
    {
        // The rest act exactly as their Guide entries do.
        const bool was_open=guide;
        guide=true;
        const int saved_blade=guide_blade;
        Effect e=perform_guide_entry(entry);
        if (!was_open && guide) guide=false;
        guide_blade=saved_blade;
        return e;
    }
    }
}

Effect Model::press_hub(xb::Button button)
{
    int &sel = selected[channel];
    const int last = count() - 1;
    if (era == 0)
    {
        int blade = 0;
        while (blade < 4 && kBladesChannels[blade] != channel) ++blade;
        if (button == xb::B && channel == MyXbox && blades_page > 0) { close_blades_page(); return {}; }
        if (blades_page > 0 && (button == xb::Left || button == xb::LB || button == xb::Right || button == xb::RB))
            blades_page = 0, selected[MyXbox] = 0; // switching blades leaves the settings pages
        if (button == xb::Left || button == xb::LB) { go(kBladesChannels[std::max(dashboard_build<5759?1:0, blade - 1)]); return {}; }
        if (button == xb::Right || button == xb::RB) { go(kBladesChannels[std::min(4, blade + 1)]); return {}; }
        if (button == xb::Up) { sel = std::max(0, sel - 1); return {}; }
        if (button == xb::Down) { sel = std::min(last, sel + 1); return {}; }
        if (button==xb::X)
        {
            if (!blades_signed_out) {blades_signed_out=true;clamp_all();for (int &s:selected) s=0;}
            return {};
        }
        if (button==xb::A && channel!=Apps && sel<int(blades_items(channel).size()))
            return run_blades_item(blades_items(channel)[std::size_t(sel)]);
    }
    if (era == 3)
    {
        if (button == xb::B)
        {
            if (metro_focus != -1) metro_focus = -1;
            else { select_metro_tab(1); metro_focus = -1; }
            return {};
        }
        if (button == xb::LB || button == xb::RB)
        {
            const bool tabs = metro_focus == -1;
            select_metro_tab(metro_tab + (button == xb::RB ? 1 : -1));
            if (tabs) metro_focus = -1;
            return {};
        }
        if(metro_tab==2 && metro_focus>=kMetroSocialOwner && metro_focus<=kMetroSocialFriend2) {
            if(button==xb::Left) {
                if(metro_focus==kMetroSocialOwner) metro_focus=std::clamp(sel,0,count()-1);
                else --metro_focus;
                return {};
            }
            if(button==xb::Right) {
                if(metro_focus<kMetroSocialFriend2) ++metro_focus;
                else {select_metro_tab(3);metro_focus=std::min(selected[channel]%metro_page_size(),count()-1);}
                return {};
            }
            if(button==xb::Up){metro_focus=-1;return {};}
            if(button==xb::Down){metro_focus=std::clamp(sel,0,count()-1);return {};}
            if(button==xb::A) {
                if(metro_focus==kMetroSocialOwner)return open_viewer(Viewer::Profile);
                return {Command::AvatarWave,metro_focus-kMetroSocialFriend0+1};
            }
            if(button!=xb::Guide&&button!=xb::Start&&button!=xb::X)return {};
        }
        if (metro_avatar_home() && metro_focus==kMetroAvatar) {
            if(button==xb::A)return open_viewer(Viewer::Avatar);
            if(button==xb::Up){metro_focus=-1;return {};}
            const int size=metro_page_size(),page=sel/size,n=std::min(size,count()-page*size);
            if(button==xb::Left || button==xb::Down){const int slot=button==xb::Down&&n>3?3:n>2?2:0;sel=page*size+slot;metro_focus=slot;return {};}
            if(button==xb::Right){
                if((page+1)*size<count()){sel=(page+1)*size;metro_focus=0;}
                else if(metro_tab<7){select_metro_tab(metro_tab+1);metro_focus=selected[channel]%metro_page_size();}
                return {};
            }
            if(button!=xb::Guide&&button!=xb::Start&&button!=xb::LT&&button!=xb::RT)return {};
        }
        if (metro_focus == -1)
        {
            if (button == xb::Left || button == xb::Right || button == xb::LT || button == xb::RT)
            {
                select_metro_tab(button == xb::LT ? 0 : button == xb::RT ? 7 :
                                 metro_tab + (button == xb::Right ? 1 : -1));
                metro_focus = -1;
                return {};
            }
            if (button == xb::Down || button == xb::A) { metro_focus = sel % metro_page_size(); return {}; }
            if (button != xb::Guide && button != xb::Start && button != xb::X) return {};
        }
        else if (metro_tab==7 && (button==xb::Left || button==xb::Right || button==xb::Up || button==xb::Down)) {
            const int slot=sel%8, first=sel/8*8;
            int next=sel;
            if (button==xb::Up) {if (slot<4) metro_focus=-1;else next-=4;}
            if (button==xb::Down && slot<4 && sel+4<count()) next+=4;
            if (button==xb::Right) {
                if (slot%4<3 && sel+1<count()) ++next;
                else if (first+8<count()) next=first+8;
                else if (metro_tab<7) {select_metro_tab(metro_tab+1);metro_focus=selected[channel]%metro_page_size();return {};}
            }
            if (button==xb::Left) {
                if (slot%4>0) --next;
                else if (first>0) next=first-8+3;
                else if (metro_tab>0) {select_metro_tab(metro_tab-1);metro_focus=selected[channel]%metro_page_size();return {};}
            }
            sel=next;if (metro_focus!=-1) metro_focus=sel%8;
            return {};
        }
        else if (metro_tab==2 && (button==xb::Left || button==xb::Right || button==xb::Up || button==xb::Down)) {
            // Social is the retail three-row action strip rather than the
            // generic Metro tile topology.  Up/down walks the strip; left and
            // right continue across channels without forcing a trip to tabs.
            if(button==xb::Up) {
                if(sel<=0) metro_focus=-1;
                else {--sel;metro_focus=sel;}
            } else if(button==xb::Down) {
                if(sel+1<count()) {++sel;metro_focus=sel;}
            } else if(button==xb::Right) {
                metro_focus=kMetroSocialOwner;
            } else {
                select_metro_tab(1);
                metro_focus=std::min(selected[channel]%metro_page_size(),count()-1);
            }
            return {};
        }
        else if (button == xb::Left || button == xb::Right || button == xb::Up || button == xb::Down)
        {
            const int direction = button == xb::Left ? 0 : button == xb::Right ? 1 : button == xb::Up ? 2 : 3;
            const int size=metro_page_size();
            const int page = sel / size, n = std::min(size, count() - page * size);
            if(metro_avatar_home() && button==xb::Right && (metro_focus==2 || (metro_focus==0 && n<=2))){metro_focus=kMetroAvatar;return {};}
            const auto &neighbors=metro_v1_home()?kMetroV1HomeNeighbors:metro_v1()?kMetroV1Neighbors:kMetroNeighbors;
            int next = neighbors[direction][std::clamp(metro_focus, 0, kMetroRecent)];
            const bool missing = next >= n && next < size;
            while (next >= 0 && next < size && next >= n) next = neighbors[direction][next];
            if (next < 0 && missing)
            {
                // Sparse pages use the nearest visible tile beyond an empty cell.
                const auto &tiles=metro_v1_home()?kMetroV1HomeTiles:metro_v1()?kMetroV1Tiles:kMetroTiles;
                const auto &from=tiles[std::clamp(metro_focus,0,kMetroRecent)];
                int best=0x7fffffff;
                for (int node=0;node<=kMetroRecent;++node)
                {
                    const bool visible=node<n || (!metro_v1() && node>=kMetroPins) || (metro_v1_home() && node==kMetroPins);
                    if (!visible || node==metro_focus) continue;
                    const auto &to=tiles[node];
                    const int dx=(to.x*2+to.w)-(from.x*2+from.w),dy=(to.y*2+to.h)-(from.y*2+from.h);
                    const int forward=direction==0?-dx:direction==1?dx:direction==2?-dy:dy;
                    const int sideways=direction<2?std::abs(dy):std::abs(dx);
                    if (forward>0 && forward+sideways*2<best) {best=forward+sideways*2;next=node;}
                }
            }
            if (next >= 0)
            {
                metro_focus = next;
                if (next < size) sel = page * size + next;
            }
            else if (button == xb::Up) metro_focus = -1;
            else if (button == xb::Right && (page + 1) * size < count()) { sel = (page + 1) * size; metro_focus = 0; }
            else if (button == xb::Left && page > 0) { sel = (page - 1) * size; metro_focus = 0; }
            else if (button == xb::Right && metro_tab < 7) { select_metro_tab(metro_tab + 1); metro_focus=selected[channel]%metro_page_size(); }
            else if (button == xb::Left && metro_tab > 0) { select_metro_tab(metro_tab - 1); metro_focus=selected[channel]%metro_page_size(); }
            return {};
        }
        else if (button == xb::LT || button == xb::RT)
        {
            sel = button == xb::LT ? 0 : last;
            metro_focus = sel % metro_page_size();
            return {};
        }
        if (metro_tab!=7 && metro_focus >= kMetroPins)
        {
            if (button == xb::A) return open_collection(metro_v1_home()?Collection::Recent:metro_focus == kMetroPins ? Collection::Pins : Collection::Recent);
            if (button != xb::Guide && button != xb::Start && button != xb::X) return {};
        }
    }
    switch (button)
    {
    case xb::Left: sel = std::max(0, sel - 1); return {};
    case xb::Right: sel = std::min(last, sel + 1); return {};
    case xb::LB: sel = std::max(0, sel - kPageStep); return {};
    case xb::RB: sel = std::min(last, sel + kPageStep); return {};
    case xb::LT: sel = 0; return {};
    case xb::RT: sel = last; return {};
    case xb::Up:
        if (channel==MyXbox) go(welcome_available()?Welcome:Apps);
        else if (channel==Welcome) go(Apps);
        else if (channel>0) go(channel-1);
        return {};
    case xb::Down:
        if (channel==Apps) go(welcome_available()?Welcome:MyXbox);
        else if (channel==Welcome) go(MyXbox);
        else if (channel<MyXbox) go(channel+1);
        return {};
    case xb::Guide:
    case xb::Start:
        open_guide();
        return {};
    case xb::B:
        if (channel != MyXbox)
            go(MyXbox);
        return {};
    case xb::X:
        if (channel == Games || channel == Apps || channel == MyXbox)
            return {Command::Refresh, -1};
        return {};
    case xb::Y:
    {
        // Details: play, attach, order on My Xbox, and which channel it is in.
        const Card c = current_card();
        if (c.kind == Kind::App || c.kind == Kind::Game || c.kind == Kind::Pinned || c.kind == Kind::Missing)
            open_info(c);
        return {};
    }
    case xb::A:
    {
        const Card c = current_card();
        switch (c.kind)
        {
        case Kind::App: return {Command::Launch, c.app};
        case Kind::Game: return {Command::LaunchTitle, c.game};
        case Kind::Pinned:
            return c.game >= 0 ? Effect{Command::LaunchTitle, c.game} : Effect{Command::Launch, c.app};
        case Kind::Missing:
            show(c.title, "This is not installed now. Press Y to detach it.");
            return {};
        case Kind::Empty:
            show(c.title, channel == Games
                              ? "No installed games were found. Press Y on an app for Details to list it here."
                              : "Put a homebrew folder in /data/homebrew, then press X to refresh.");
            return {};
        case Kind::Hub: break;
        }
        switch (c.goal)
        {
        case Goal::ToWelcome:
            welcome_hot=era==1 && current()==0;welcome_confirm=false;
            return open_viewer(Viewer::WelcomePages);
        case Goal::HideWelcome:
            welcome_confirm=true;welcome_yes=false;
            return open_viewer(Viewer::WelcomePages);
        case Goal::ExitToPs5: return {Command::ExitToPs5,-1};
        case Goal::ToApps: go(Apps); break;
        case Goal::ToGames: go(Games); break;
        case Goal::ToMedia: go(Media); break;
        case Goal::ControllerMap: controller_map = true; break;
        case Goal::Unavailable: show(c.title, "Not available on PS5 yet."); break;
        case Goal::ToPictures: return open_viewer(Viewer::Pictures);
        case Goal::ToVideos:
        {
            Effect result=open_viewer(Viewer::Videos);
            const auto list=video_list();
            const auto preview=std::find(list.begin(),list.end(),c.preview);
            if (!c.preview.empty() && preview!=list.end()) {
                viewer_item=int(preview-list.begin());
                return request_video();
            }
            return result;
        }
        case Goal::ToProfile: return open_viewer(Viewer::Profile);
        case Goal::ToAvatar: return open_viewer(Viewer::Avatar);
        case Goal::ToMusic: return open_viewer(Viewer::Music);
        case Goal::ToMusicAlbums:
            music_view=1;
            return open_viewer(Viewer::Music);
        case Goal::ToMusicRandomAlbum:
            return {Command::MusicEdit,int(MusicOp::RandomAlbum)};
        case Goal::ToMusicMostPlayed:
            music_view=5;
            return open_viewer(Viewer::Music);
        case Goal::ToMusicPlaylists:
            music_view=4;
            return open_viewer(Viewer::Music);
        case Goal::ToMusicRecentlyAdded:
            music_view=6;
            return open_viewer(Viewer::Music);
        case Goal::ToMusicNowPlaying:
            open_viewer(Viewer::Music);
            music_page=MusicNowPlaying;
            return {Command::MusicBrowse,-1};
        case Goal::ToSearch: return open_collection(Collection::All);
        case Goal::ToMediaCenter: return {Command::LaunchSystem, 2};
        case Goal::ToSettings:
            if (era >= 1) { system_page = 0; return open_viewer(Viewer::SystemSettings); }
            open_guide(); guide_blade = BladeSettings; break;
        case Goal::ToRomGameGuidePreview:
            rom_game_guide_preview.session_lost();
            rom_game_guide_preview.press(exbox_rom_guide::Button::Guide);
            return open_viewer(Viewer::RomGameGuidePreview);
        case Goal::None: break;
        }
        return {};
    }
    default: return {};
    }
}

std::vector<Hint> Model::hints() const
{
    if (controller_map)
        return {{xb::B, "Back"}};
    if (!guide && (viewer==Viewer::WelcomePages || viewer==Viewer::InitialSetup)) {
        if (viewer==Viewer::WelcomePages && !welcome_confirm) return {{xb::LB,"Previous"},{xb::RB,"Next"},{xb::B,"Back"}};
        std::vector<Hint> out{{xb::A,viewer==Viewer::InitialSetup?(setup_step==SetupDone?"Done":"Continue"):"Select"},{xb::B,"Back"}};
        if (viewer==Viewer::InitialSetup && (setup_step==SetupAutoOff || setup_step==SetupClock)) out.push_back({xb::Left,"Change"}); // Ex-Box addition: no 360 original
        return out;
    }
    if ((!guide || era==0) && (viewer==Viewer::DashboardEras || viewer==Viewer::DashboardUpdates))
        return {{xb::A, viewer==Viewer::DashboardEras?"Choose Era":"Use Style"}, {xb::B,"Back"}, {xb::X,picker_previews?"Hide Previews":"Show Previews"}, {xb::LB,"Page"}};
    // GuideMain.xur LegendA/B/Y are inherited by every tab in all four builds.
    // None of the signed-in tab scenes overrides them or authors an X action.
    if (guide)
        return {{xb::A, "Select"}, {xb::B, "Back"}, {xb::Y, era == 3 ? "Xbox Home" : "Xbox Dashboard"}};
    if (viewer==Viewer::Achievements) {
        if (achievement_game.empty() && viewer_count()>0) return {{xb::A,"Select"},{xb::B,"Back"}};
        return {{xb::B,"Back"}};
    }
    if (viewer==Viewer::Themes) {
        const int index=theme_catalog_index_for_row(viewer_item);bool favorite=false;
        if(index>=0)favorite=std::find(theme_favorites.begin(),theme_favorites.end(),installed_themes[std::size_t(index)].stable_id)!=theme_favorites.end();
        const char*filter=theme_filter==1?"Favorites":theme_filter==2?"Recent":"All";
        std::vector<Hint> out{{xb::A,"Select"},{xb::B,"Back"},{xb::Y,std::string("Filter: ")+filter}};
        if(index>=0)out.push_back({xb::X,favorite?"Unfavorite":"Favorite"});return out;
    }
    if (viewer==Viewer::Profile) return {{xb::A,"Select"},{xb::B,"Back"}};
    if (viewer==Viewer::SystemSettings)
    {
        if (details) return {{xb::A, "OK"}, {xb::B, "Back"}};
        if (system_page==SystemHealth || system_page==SystemDiagnostics || system_page==SystemLibraryStatus)
            return {{xb::B, "Back"}};
        if (storage_page()) {
            std::vector<Hint> out{{xb::B,"Back"}};
            if (!storage_entries().empty()) out.insert(out.begin(),{xb::A,"Select"});
            if (system_page==StorageDevices && !storage.devices.empty()) out.push_back({xb::Y,"Device Options"});
            if (system_page==StorageItems && !storage_item_list().empty()) out.push_back({xb::X,"Delete"});
            if (system_page==StorageCategories) out.push_back({xb::Y,"Transfer Content"});
            return out;
        }
        if (system_page==SystemInfo || system_page==SystemDateTime ||
            system_page==SystemScreenFormat)
            return {{xb::A,"Details"},{xb::B, "Back"}};
        return {{xb::A, "Select"}, {xb::B, "Back"}};
    }
    if (viewer==Viewer::AdsSettings)
        return {{xb::A,viewer_item>=5?"Toggle":"Change"},{xb::X,ad_show_all?"Ad candidates":"All resources"},{xb::LB,"Page"},{xb::B,"Back"}};
    if (info) return {{xb::A, "Select"}, {xb::B, "Back"}};
    if (details) return {{xb::A, "OK"}, {xb::B, "Back"}};
    if (viewer == Viewer::Library) {
        const auto cards=library_cards();
        if(!cards.empty()) {
            const Card&card=cards[std::size_t(std::clamp(viewer_item,0,int(cards.size())-1))];
            if(card.game<0&&card.app<0)
                return {{xb::X,"Refresh"},{xb::LB,"Page"},{xb::B,"Back"}};
        }
        return {{xb::A, "Play"}, {xb::Y, "Details"}, {xb::LB, "Page"}, {xb::B, "Back"}};
    }
    if (viewer == Viewer::RomGameGuidePreview) {
        const auto rows=rom_game_guide_preview.rows();
        std::vector<Hint> out;
        const int item=std::clamp(viewer_item,0,std::max(0,int(rows.size())-1));
        if(!rows.empty()&&rows[std::size_t(item)].enabled)out.push_back({xb::A,"Select"});
        out.push_back({xb::B,"Close"});
        return out;
    }
    if (viewer == Viewer::AvatarEdit)
        return {{xb::Up, "Choose"}, {xb::LB, "Turn"}, {xb::X, "Random"}, {xb::Y, "Emote"}, {xb::A, "Save"}, {xb::B, "Cancel"}};
    if (viewer == Viewer::Avatar)
    {
        std::vector<Hint> out;
        if (viewer_count() > 0)
            out = {{xb::Up, "Turn"}, {xb::A, "Wave"}, {xb::LT, "Face Front"}};
        if (avatar_editable || external_avatar_editor)
            out.push_back({xb::Y, "Edit"});
        out.push_back({xb::B, "Back"});
        return out;
    }
    if (viewer == Viewer::Music && music_page != MusicLibrary)
    {
        if (music_page==MusicAlbumInfo) {
            if (music_album_field>=0) return {{xb::A,"Select"},{xb::X,"Backspace"},{xb::Y,"Done"},{xb::B,"Cancel"}}; // Ex-Box addition: no 360 original keyboard
            return {{xb::A,"Select"},{xb::B,"Cancel"}}; // 1025_EditAlbumInfo
        }
        // The pages' legends: 1016 / 1028 / 1030 legend_A "Select", legend_B "Back".
        std::vector<Hint> out;
        const bool song_row = music_page == MusicEditPlaylist && viewer_item >= 3 && music_queue > 0;
        if (viewer_count() > 0)
            out.push_back({xb::A, "Select"});
        if (music_page == MusicNowPlaying && now_playing >= 0)
        {
            out.push_back({xb::X, music_paused ? "Resume" : "Pause"});
            out.push_back({xb::Y, "Stop"}); // Ex-Box addition: the 360's Y was the visualizer
        }
        if (song_row)
        {
            out.push_back({xb::X, "Remove"});        // 1030 listDeleteBtns
            out.push_back({xb::LB, "Move Up / Down"}); // 1030 listMoveUpBtns / listMoveDownBtns
        }
        if (music_page == MusicChooseCover)
            out.push_back({xb::X, "Use the Song's Own"}); // Ex-Box addition: no 360 original
        out.push_back({xb::B, "Back"});
        return out;
    }
    if (viewer == Viewer::Music)
    {
        if (music_page == MusicLibrary && music_tabs_focused)
            return {{xb::Left, "Previous View"}, {xb::Right, "Next View"},
                    {xb::Down, "List"}, {xb::A, music_random_focused?"Play":music_game_scan_focused?"Scan":"Select"}, {xb::B, "Back"}};
        std::vector<Hint> out;
        if (viewer_count() > 0)
            out.push_back({xb::A, "Select"}); // music/1003_IndividualDevice.xur legend_A, every build
        if (now_playing >= 0)
            out.push_back({xb::X, music_paused ? "Resume" : "Pause"});
        out.push_back({xb::Y, "Now Playing"}); // music/1003_IndividualDevice.xur legend_Y
        out.push_back({xb::B, "Back"});
        constexpr const char *next_view[] = {"Albums", "Artists", "Genres", "Saved Playlists", "Most Played", "Recently Added", "Songs"}; // first five are music/1003_IndividualDevice.xur; smart views retain the same wrap contract
        out.push_back({xb::RT, next_view[std::clamp(music_view, 0, kMusicViews - 1)]});
        if (viewer_count() > kMusicRows)
            out.push_back({xb::LB, "Page"}); // Ex-Box addition: no 360 original
        if (viewer_count() > 0) // the page shows how they stand
        {
            out.push_back({xb::LS, "Shuffle"}); // Ex-Box addition: no 360 original
            out.push_back({xb::RS, "Repeat"}); // Ex-Box addition: no 360 original
        }
        return out;
    }
    if (viewer == Viewer::Videos && !video_pending.empty())
        return {{xb::A, "Select"}, {xb::B, "Back"}};
    if (viewer == Viewer::Videos && viewer_full)
    {
        const int length = video_length();
        // "Restart": videos/DetailsActions.xur (9199 on), VideoStart.xur (6717).
        std::vector<Hint> out{{xb::A, video_paused && length > 0 && video_elapsed >= length ? "Restart"
                                      : video_paused ? "Play" : "Pause"},
                              {xb::RT, "Seek"}, {xb::LB, "Previous / Next"}}; // Seek, Previous / Next: Ex-Box additions: no 360 original
        const auto it = video_info.find(video_key());
        if (it != video_info.end() && it->second.subtitles)
            out.push_back({xb::Y, media_text::captions(*this, video_subtitles)});
        if (video_theater) {
            out.push_back({xb::Up, "Choose Reaction"});
            out.push_back({xb::X, kTheaterReactions[std::clamp(video_theater_reaction,0,kTheaterReactionCount-1)]});
        }
        out.push_back({xb::RS, video_theater ? "Full Screen" : "Theater Party"});
        out.push_back({xb::B, "Back"});
        return out;
    }
    if (viewer == Viewer::Videos)
    {
        std::vector<Hint> out;
        if (viewer_count()>0) {
            out.push_back({xb::A,"Select"});
            out.push_back({xb::RS,"Theater Party"});
            out.push_back({xb::X,kVideoOrders[(video_order+1)%kVideoOrderCount]});
        }
        out.push_back({xb::Y,"Refresh"});
        out.push_back({xb::B,"Back"});
        return out;
    }
    if (viewer == Viewer::Pictures && picture_background_confirm) return {{xb::A, "Select"}, {xb::B, "Back"}};
    if (viewer == Viewer::Pictures && viewer_full)
    {
        std::vector<Hint> out;
        if (picture_control >= 0) out.push_back({xb::A, "Select"});
        else if (viewer_count() > 1) out.push_back({xb::A, picture_slideshow ? "Pause" : "Play"});
        // Ex-Box additions: no 360 original; navigation to the transport and metadata.
        out.push_back({xb::Up, picture_zoom > 0 ? "Pan" : picture_control >= 0 ? "Controls" : "Previous / Next"});
        out.push_back({xb::LB, "Previous / Next"});
        out.push_back({xb::RT, "Zoom"});
        out.push_back({xb::Y, picture_background_available() ? "Apply as Background" : "Unavailable"});
        out.push_back({xb::RS, picture_info ? "Hide Info" : "Info"});
        out.push_back({xb::B, "Back"});
        return out;
    }
    if (viewer != Viewer::None)
    {
        if (viewer_full)
            return {{xb::Up, "Previous / Next"}, {xb::B, "Back"}};
        std::vector<Hint> out;
        if (viewer_count() > 0)
            // legend_A of pictures/905_IndividualDeviceMain.xur and videos/VideoContent.xur.
            out.push_back({xb::A, "Select"});
        out.push_back({xb::B, "Back"});
        if (viewer == Viewer::Pictures)
        {
            out.push_back({xb::X, "Sort"}); // Ex-Box addition: no 360 original
            if (viewer_count() > 0 && !picture_groups())
                out.push_back({xb::Y, picture_background_available() ? "Apply as Background" : "Unavailable"});
            out.push_back({xb::RS, "Refresh"}); // Ex-Box addition: no 360 original
        }
        if (viewer == Viewer::Videos)
        {
            out.push_back({xb::X, kVideoOrders[(video_order + 1) % kVideoOrderCount]}); // GetNextSortString
            out.push_back({xb::Y, "Refresh"}); // Ex-Box addition: no 360 original
        }
        if (viewer_count() > kViewerColumns * kViewerRows)
            out.push_back({xb::LB, "Page"});
        return out;
    }
    std::vector<Hint> out;
    // Metro's legend (2012 capture) names only the focused tile's action.
    if (era == 3 && metro_focus == -1)
        return {{xb::A, "Select"}, {xb::Guide, "Guide"}};
    if (era == 3 && metro_focus >= kMetroPins)
        return {{xb::A, "Select"}, {xb::Guide, "Guide"}};
    const Card c = current_card();
    const bool title = c.kind == Kind::App || c.kind == Kind::Game || c.kind == Kind::Pinned;
    out.push_back({xb::A, title ? (c.kind == Kind::Game || c.game >= 0 ? "Play" : "Open") : "Select"});
    // NXE showed no B prompt at the hub root (nor Metro's home grid).
    if (era != 3 && channel != MyXbox)
        out.push_back({xb::B, "My Xbox"});
    if (c.kind == Kind::App || c.kind == Kind::Game || c.kind == Kind::Pinned || c.kind == Kind::Missing)
        out.push_back({xb::Y, "Details"});
    if (era == 0) {
        if (channel == MyXbox && blades_page > 0) out.push_back({xb::B, "Back"});
        if (!blades_signed_out) out.push_back({xb::X, "Sign Out"});
    }
    else if (era == 1 || era == 2 || era == 3) {
        // The 2008 / 2010 / 2012 legends list only what the focused slot does.
        out.push_back({xb::Guide, "Guide"});
        return out;
    }
    else if (channel == Games || channel == Apps || channel == MyXbox)
        out.push_back({xb::X, "Refresh"});
    if (era == 0 || era == 3) out.push_back({xb::RB, era == 0 ? "Blade" : "Tab"});
    else if (count() > kPageStep) out.push_back({xb::RB, "Page"});
    out.push_back({xb::Guide, "Guide"});
    return out;
}

Sound sound_for(const Model &before, const Model &after, xb::Button button, const Effect &effect)
{
    if (before.viewer==Viewer::InitialSetup && after.viewer==Viewer::InitialSetup &&
        before.setup_step!=Model::SetupDone && after.setup_step==Model::SetupDone) return Sound::SetupComplete;
    const bool direction =
        button == xb::Up || button == xb::Down || button == xb::Left || button == xb::Right;
    const bool paging = button == xb::LB || button == xb::RB || button == xb::LT || button == xb::RT;
    // The Controller Map lights its rows; it has no sounds of its own.
    if (before.controller_map)
        return after.controller_map ? Sound::None : Sound::Back;
    // Starting a game or app, from anywhere: the dashboard's transition whoosh.
    if (effect.command == Command::Launch || effect.command == Command::LaunchTitle)
        return Sound::Launch;
    if (!before.guide && after.guide)
        return Sound::GuideOpen;
    if (before.guide && !after.guide)
    {
        const bool dismissed = (button == xb::B || button == xb::Guide || button == xb::Start) &&
                               effect.command == Command::None && !after.controller_map;
        return dismissed ? Sound::GuideClose : Sound::GuideSelect;
    }
    if (before.guide)
    {
        if (after.guide_blade != before.guide_blade)
            return Sound::BladeSwitch;
        if (after.guide_item[after.guide_blade] != before.guide_item[before.guide_blade])
            return Sound::GuideFocus;
        if (effect.command != Command::None)
            return Sound::GuideSelect;
        return direction || paging ? Sound::Blocked : Sound::None;
    }
    // The Media viewer: unfolds over the hub, folds away, focus moves on its grid.
    if (before.viewer == Viewer::None && after.viewer != Viewer::None)
        return Sound::Unfold;
    if (before.viewer != Viewer::None && after.viewer == Viewer::None)
        return after.guide ? Sound::GuideSelect : Sound::Fold;
    if ((before.viewer==Viewer::Profile && after.viewer==Viewer::Themes) ||
        (before.viewer==Viewer::Themes && after.viewer==Viewer::Profile))
        return button==xb::B ? Sound::Back : Sound::Select;
    if ((before.viewer==Viewer::Achievements || after.viewer==Viewer::Achievements) &&
        (before.viewer!=after.viewer || before.achievement_game!=after.achievement_game))
        return button==xb::B?Sound::Back:Sound::Select;
    // The avatar editor opening over the stage, and closing back to it.
    if (before.viewer != Viewer::AvatarEdit && after.viewer == Viewer::AvatarEdit)
        return Sound::Unfold;
    if (before.viewer == Viewer::AvatarEdit && after.viewer == Viewer::Avatar)
        return effect.command == Command::AvatarSave ? Sound::Select : Sound::Fold;
    if (after.viewer == Viewer::AvatarEdit && after.avatar_category != before.avatar_category)
        return Sound::Focus;
    if (after.viewer != Viewer::None)
    {
        if (after.viewer==Viewer::InitialSetup && before.setup_step!=after.setup_step)
            return button==xb::B?Sound::Back:Sound::Select;
        if (after.viewer==Viewer::WelcomePages && before.welcome_yes!=after.welcome_yes) return Sound::Focus;
        if (after.video_pending != before.video_pending)
            return after.video_pending.empty() ? (button == xb::B ? Sound::Back : Sound::Select) : Sound::Unfold;
        if (after.video_start_item != before.video_start_item) return Sound::Focus;
        if (after.viewer_full != before.viewer_full)
            return after.viewer_full ? Sound::Select : Sound::Back;
        if (effect.command == Command::VideoSeek && button != xb::A && button != xb::X)
            return Sound::Focus; // scrubbing ticks like moving the focus
        if (after.viewer == Viewer::Pictures)
        {
            if (after.picture_group != before.picture_group)
                return after.picture_group >= 0 ? Sound::Select : Sound::Back;
            if (after.picture_control != before.picture_control) return Sound::Focus;
            if (after.picture_rotation != before.picture_rotation || after.picture_background_confirm != before.picture_background_confirm ||
                after.picture_background_yes != before.picture_background_yes) return Sound::Select;
            if (after.picture_zoom != before.picture_zoom)
                return Sound::Focus;
            if (after.picture_slideshow != before.picture_slideshow || after.picture_info != before.picture_info)
                return Sound::Select;
            if (after.picture_pan_x != before.picture_pan_x || after.picture_pan_y != before.picture_pan_y)
                return Sound::None; // panning is quiet
        }
        if (after.viewer == Viewer::Music && after.music_tabs_focused && before.music_tabs_focused &&
            (after.music_random_focused != before.music_random_focused || after.music_game_scan_focused != before.music_game_scan_focused)) return Sound::Focus;
        if (after.viewer == Viewer::Music && after.music_tabs_focused != before.music_tabs_focused)
            return button == xb::B ? Sound::Back : button == xb::A ? Sound::Select : Sound::Focus;
        if (after.viewer == Viewer::Music && after.music_page != before.music_page)
            return button == xb::B ? Sound::Back : Sound::Select; // a music page opens or closes
        if (after.viewer == Viewer::SystemSettings) {
            if (before.details) return after.details ? Sound::None : Sound::Back;
            if (after.details) return Sound::Refused;
            if (after.system_page != before.system_page) return button == xb::B ? Sound::Back : Sound::Select;
            if (!before.controller_map && after.controller_map) return Sound::Select;
        }
        if (after.viewer_item != before.viewer_item)
            return Sound::Focus;
        if (effect.command != Command::None)
            return Sound::Select; // the avatar waves, Pictures looks again
        return direction || paging ? Sound::Blocked : Sound::None;
    }
    if (!before.info && after.info)
        return Sound::Unfold;
    if (before.info)
    {
        if (!after.info)
            return effect.command != Command::None || after.guide ? Sound::Select : Sound::Fold;
        if (after.info_item != before.info_item)
            return Sound::Focus;
        // Stepping through trailers ticks like moving the focus.
        if (effect.command == Command::SaveTrailers && direction)
            return Sound::Focus;
        if (effect.command != Command::None)
            return Sound::Select;
        return direction ? Sound::Blocked : Sound::None;
    }
    if (before.details)
        return after.details ? Sound::None : Sound::Back;
    if (!before.controller_map && after.controller_map)
        return Sound::Select;
    if (after.details)
        return Sound::Refused;
    if (direction || paging)
    {
        if (after.era==3 && (after.metro_tab != before.metro_tab || after.metro_focus != before.metro_focus)) return Sound::Focus;
        if (after.library_filter != before.library_filter) return Sound::Focus;
        if (after.channel != before.channel)
            return after.channel_rank(after.channel) < before.channel_rank(before.channel) ? Sound::ChannelUp : Sound::ChannelDown;
        if (after.current() != before.current())
            return after.current() < before.current() ? Sound::PaneLeft : Sound::PaneRight;
        return Sound::Blocked;
    }
    if (button == xb::B)
        return after.channel != before.channel ? Sound::Back : Sound::None;
    if (effect.command != Command::None || after.channel != before.channel)
        return Sound::Select;
    return Sound::None;
}
} // namespace dash
