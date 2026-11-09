// Ex-Box 360: dashboard state and button behaviour. Pure.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dash_model.hpp"
#include "dashboard_layout.hpp"

#include <algorithm>
#include <cctype>
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
    case BladeApps: return era>=1 && !guide_host?"Marketplace":"Apps";
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
    if (guide_host) return host_blade_entries(blade);
    if (era==0) {
        if (blade!=BladeHome) return host_blade_entries(blade);
        // Original CommunityBar positions must not launch unrelated host apps.
        // Xbox network services are unavailable; retain the source semantics.
        using A=GuideAction;
        return {{"Messages",A::Unavailable,-1,"btnMessages"},
                {"Friends",A::Unavailable,-1,"btnFriends"},
                {"Players",A::Unavailable,-1,"btnRecentPlayers"},
                {dashboard_build>=5759?"Chat and IM":"Private Chat",A::Unavailable,-1,"btnQuickChat"},
                {"Personal Settings",A::OpenGuideSettings,-1,"btnOptions"},
                {"Select Music",A::Music,-1,"btnPlaylist"},
                {"Xbox Dashboard",A::MyXbox,-1,"btnGameBanner"}};
    }
    // hud/GuideMain and *TabSignedIn.xur, 9199/13604/15574/17559.
    // Preserve scene order; source-hidden conditional/debug rows stay hidden.
    using A=GuideAction;
    std::vector<GuideEntry> out;
    auto row=[&](const char *id,const char *label,A action=A::Unavailable) {
        out.push_back({label,action,-1,id});
    };
    switch (blade) {
    case BladeApps:
        if (era==3 && !metro_v1()) break;
        if (era>=2) {
            row("btnActiveDownloads","Active Downloads");row("btnRedeemCode","Redeem Code");
        }
        row("btnGameMarketplace","Game Marketplace");
        if (era<3) row("btnVideoMarketplace","Video & Music Marketplace");
        row("btnAvatarMarketplace","Avatar Marketplace");
        if (era==1) {
            row("btnActiveDownloads","Active Downloads");row("btnRedeemCode","Redeem Code");
        }
        break;
    case BladeGames:
        row("btnAchievements","Achievements",A::Achievements);
        row("btnAvatarAwards","Awards");
        if (era>=2) row("btnQuickLaunch",era==2?"Quick Launch":metro_v1()?"Quickplay":"Recent",A::RecentGames);
        row("btnGamesLibrary",era<3?"Game Library":"My Games",A::AllGames);
        if (era==3 && !metro_v1()) {
            row("btnActiveDownloads","Active Downloads");row("btnRedeemCode","Redeem Code");
        }
        break;
    case BladeHome:
        row("btnDashboard",era<3?"Xbox Dashboard":"Xbox Home",A::MyXbox);
        if (era==1) row("btnJumpIn","Quick Launch",A::RecentGames);
        row("btnConnectToLive",era==3 && !metro_v1()?"Connect to Xbox Live":"Connect to Xbox LIVE");
        row("btnDiscInTray","Disc in Tray");
        break;
    case BladeMedia:
        row("btnVideoLibrary",era<3?"Video Library":metro_v1()?"Video Player":"System Video Player",A::Videos);
        row("btnMusicLibrary",era<3?"Music Library":metro_v1()?"Music Player":"System Music Player",A::Music);
        row("btnPictureLibrary",era<3?"Picture Library":"Picture Viewer",A::Pictures);
        row("btnMCX","Windows Media Center");
        // Keep the existing scene-driven mini player's ordering and actions.
        if (media_player) {
            const GuideEntry song{now_playing>=0?now_playing_title:"Select Music",A::Music};
            if (media_player==2) out.push_back(song);
            if (now_playing>=0) {
                out.push_back({music_paused?"Resume Music":"Pause Music",A::MusicPause});
                out.push_back({"Previous Song",A::MusicPrevious});out.push_back({"Stop Music",A::MusicStop});
                out.push_back({"Next Song",A::MusicNext});out.push_back({music_shuffle?"Shuffle Off":"Shuffle On",A::MusicShuffle});
            }
            if (media_player==1) out.push_back(song);
        }
        break;
    default:
        row("btnProfile","Profile",A::Profile);
        if (era==1) row("btnNotifications","Notifications");
        row("btnPersonalSettings","Preferences");row("btnFamilySettings","Family Settings");
        row("btnSystemSettings","System Settings",A::SystemSettings);
        row("btnAccountManagement","Account Management");
        if (era==3) row("btnKinectTuner","Kinect Tuner");
        row("btnShutdown","Turn Off Console",A::ExitToPs5);
        break;
    }
    return out;
}

// Ex-Box addition: no 360 original. Existing utilities, grouped in one host menu.
std::vector<GuideEntry> Model::host_blade_entries(int blade) const
{
    using A = GuideAction;
    std::vector<GuideEntry> out;
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
        if (media_player && era >= 1)
        {
            out.push_back({"Media Channel", A::Media});
            out.push_back({"Pictures", A::Pictures});
            out.push_back({"Video Library", A::Videos});
            out.push_back({kSystemApps[2].title, A::LaunchSystem, 2});
            out.push_back({kSystemApps[3].title, A::LaunchSystem, 3});
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
        break;
    default:
        out.push_back({"Controller Map", A::ControllerMap});
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
        out.push_back({promotions?"Period Promotions: On":"Period Promotions: Off",A::TogglePromotions});
        out.push_back({"Ads >",A::AdsSettings});
        out.push_back({"Exit to PS5 Home", A::ExitToPs5});
        break;
    }
    return out;
}

void Model::open_guide()
{
    guide = true;
    guide_host = false;
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
        if (metro_tab == 2) return 3; // social: own profile, Friends, Messages
        if (metro_tab == 7) return metro_v1()?10:8;
        if (metro_tab == 0) return 1;
        if (channel == Media && metro_tab == 4) return 2;
        if (channel == Media && metro_tab == 5) return 1;
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
    case Games: return std::max(1, int(games.size() + game_homebrew().size()));
    case Apps: return std::max(1, int(app_homebrew().size()));
    default: return my_xbox_slots(era).count + int(pins.size()) + (promotions && era!=0?int(promo_cards.size()):0);
    }
}

// The Music card names the song playing, on every dashboard's hub.
Card Model::card(int index) const
{
    Card c = card_base(index);
    if (c.goal == Goal::ToMusic && now_playing >= 0 && !now_playing_title.empty())
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
            if (index == 0) return {"Profile",Kind::Hub,IconAvatar,Goal::ToProfile,-1,-1,""};
            return fixed(kFriends,index-1);
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
        if (channel == Media && metro_tab == 5) return fixed(kMedia, 0);
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
        const int total = int(games.size() + extra.size());
        if (total == 0)
        {
            Card c;
            c.title = "No Games Found";
            c.kind = Kind::Empty;
            return c;
        }
        index = std::clamp(index, 0, total - 1);
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
        if ((path || title) && recency(line) == int(recent.size()) && recent.size() < kMaxRecent)
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
    clamp_all();
}

void Model::set_games(std::vector<App> found)
{
    games = std::move(found);
    clamp_all();
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
    if (info)
        return press_info(button);
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
    case xb::X:
        // At the end, play again from the start.
        if (video_paused && length > 0 && video_elapsed >= length)
        {
            video_paused = false;
            video_elapsed = 0;
            return {Command::VideoSeek, 0};
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
    if (network_page()) return network_entries();
    if (storage_page()) return storage_entries();
    const bool v2 = era == 3 && !metro_v1();
    // consoles/dashSysCslSet.xur button order, all four builds. btnPolicy has
    // no known PS5 equivalent and remains hidden.
    // Labels/current values: each build's consoles/dashCSettingsStrings.xus.
    if (system_page == SystemConsole) {
        std::vector<GuideEntry> out{{"Display",A::ConsoleSettings,SystemDisplay},
            {"Audio",A::ConsoleSettings,SystemAudio},{"Language and Locale",A::Unavailable},
            {"Clock",A::ConsoleSettings,SystemClock},{"Startup and Shutdown",A::ConsoleSettings,SystemStartup},
            {"Auto-Play",A::Unavailable}};
        if (v2) out.push_back({"Auto-Downloads",A::Unavailable});
        out.push_back({"Remote Control",A::ControllerMap});
        // 17559's dynamically supplied pages (ConsoleName/ConsoleDiscoverable).
        if (v2) {
            out.push_back({"Console Name",A::Unavailable});
            out.push_back({"Connected Devices",A::Unavailable});
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
        return out;
    }
    if (system_page == SystemAudio) {
        // dashSysCslSetAudio.xur; 17559 adds AudioNielsen.
        std::vector<GuideEntry> out{{"Digital Output",A::Unavailable},
                                   {"Sound Effects",A::ConsoleSettings,SystemSoundEffects}};
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
    if (system_page == SystemResolution) return {{"1080p",A::Unavailable}};
    if (system_page == SystemScreenFormat) return {{"Widescreen",A::Unavailable}};
    if (system_page == SystemInfo || system_page == SystemDateTime) return {};
    // consoles/SystemScene: NXE lists Family Settings second; Kinect and Metro
    // Kinect Settings; Metro renames Memory to Storage and adds TV.
    std::vector<GuideEntry> out{{"Console Settings",A::ConsoleSettings}};
    if (era == 1) out.push_back({"Family Settings",A::Unavailable});
    else out.push_back({"Kinect Settings",A::Unavailable});
    out.push_back({era == 3 ? "Storage" : "Memory",A::ConsoleSettings,StorageDevices});
    out.push_back({"Network Settings",A::NetworkSettings});
    out.push_back({"Computers",A::Unavailable});
    if (era == 3) out.push_back({"TV",A::Unavailable});
    out.push_back({era == 3 && !metro_v1() ? "Xbox Live Vision" : "Xbox LIVE Vision",A::Unavailable});
    out.push_back({"Initial Setup",A::InitialSetup});
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
    // consoles/dashCSettingsStrings.xus (9199/13604/15574/17559).
    if (label == "Display" || label == "Screen Resolution") return "1080p";
    if (label == "Screen Format") return "Widescreen";
    if (label == "Audio" || label == "Sound Effects") return sounds ? "Sound Effects Enabled" : "Sound Effects Disabled";
    if (label == "Welcome Channel") return welcome_visible?"Show":"Hide";
    if (label == "Time Format") return clock_24_hour ? "24-Hour Clock" : "12-Hour Clock";
    if (label == "Screen Saver") return dimming ? "Screen Saver Enabled" : "Screen Saver Disabled";
    if (label == "System Info") return "Dashboard: 2.0." + std::to_string(dashboard_build) + ".0";
    // No guessed locale, AV pack, remote, startup, or PS5 power policy.
    return "";
}

std::string Model::system_description() const
{
    if (system_page==SystemWelcome) return "Show or hide the Welcome channel."; // dashSysCslSetWelcomeChannel.xur

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
    return "";
}

void Model::load_theme(int legacy_value)
{
    // All five former hue transforms migrate to the installed Xbox 360 default.
    // Decoded floor tints do not establish standalone retail colour themes.
    (void)legacy_value;
    theme = 0;
}
std::vector<std::string> Model::theme_entries() const
{
    std::vector<std::string> rows{kThemes[0]};
    // Ex-Box addition: no 360 original; the owned copy created by T09.
    if (picture_background_available() && picture_background > 0) rows.push_back("Picture Background");
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
    viewer_item=picture_background_enabled && theme_entries().size()>1 ? 1 : 0;
    return {};
}
#include "welcome_model.inc.hpp"
#include "achievements_model.inc.hpp"

Effect Model::press_viewer(xb::Button button)
{
    if (viewer==Viewer::Achievements) return press_achievements(button);
    if (viewer==Viewer::WelcomePages || viewer==Viewer::InitialSetup) return press_welcome(button);
    if (viewer == Viewer::Themes || viewer == Viewer::Profile) {
        // The Guide opens over these pages as over every other viewer.
        if (button == xb::Guide || button == xb::Start) { open_guide(); return {}; }
        viewer_item=std::clamp(viewer_item,0,std::max(0,viewer_count()-1));
        // ThemePicker/lstThemes Wrap=true (9199, 13604, 15574, 17559).
        if (viewer==Viewer::Themes && (button==xb::Up || button==xb::Down))
            viewer_item=(viewer_item+(button==xb::Down?1:viewer_count()-1))%viewer_count();
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
                picture_background_enabled=viewer_item==1;
                return {Command::SaveSettings,-1};
            }
            return perform_guide_entry(profile_entries()[std::size_t(viewer_item)]);
        }
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
            if (entries.empty() || system_page == SystemResolution || system_page == SystemScreenFormat) return {};
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
                const int item = page == SystemWelcome ? (welcome_visible?0:1)
                               : page == SystemSoundEffects ? (sounds ? 0 : 1)
                               : page == SystemTimeFormat ? (clock_24_hour ? 1 : 0)
                               : page == SystemScreenSaver ? (dimming ? 0 : 1) : 0;
                open_system_page(page,item);
                if (page==StorageDevices) {storage_device=-1;storage_item=-1;return {Command::ScanStorage,-1};}
                return {};
            }
            if (entry.action == GuideAction::Unavailable) { show(entry.label, "Not available on PS5 yet."); return {}; }
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
            if (n > 0) open_info(library_cards()[std::size_t(viewer_item)]);
            return {};
        case xb::A:
            if (n > 0)
            {
                const Card c = library_cards()[std::size_t(viewer_item)];
                return c.game >= 0 ? Effect{Command::LaunchTitle, c.game} : Effect{Command::Launch, c.app};
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
            music_view = (music_view + (button == xb::RT ? 1 : kMusicViews - 1)) % kMusicViews;
            music_group = -1;
            viewer_item = 0;
            return {Command::MusicBrowse, -1};
        case xb::Up: viewer_item = std::max(0, viewer_item - 1); return {};
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
        if (button == xb::A && n > 0)
            return request_video();
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
    case xb::Left: viewer_item = std::max(0, viewer_item - 1); return {};
    case xb::Right: viewer_item = std::min(last, viewer_item + 1); return {};
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
    // Ex-Box addition: no 360 original. R3 switches to a separate host menu.
    if (button==xb::RS || (guide_host && button==xb::B)) {
        guide_host=!guide_host;
        if (!guide_host && era==3 && !metro_v1() && guide_blade==BladeApps) guide_blade=BladeGames;
        for (int &at:guide_item) at=0;
        return {};
    }
    const std::vector<GuideEntry> entries = blade_entries(guide_blade);
    const int items = std::max(1, int(entries.size()));
    int &item = guide_item[guide_blade];
    item = std::clamp(item, 0, items - 1);
    if (era==0 && !guide_host && guide_blade==BladeHome) {
        if (button==xb::Left || button==xb::Right) {
            if (item<3) {item=std::clamp(item+(button==xb::Right?1:-1),0,2);guide_community_item=item;}
            return {};
        }
        if (button==xb::Down && item<3) {guide_community_item=item;item=3;return {};}
        if (button==xb::Up && item==3) {item=guide_community_item;return {};}
    }
    if (era==0 && !guide_host && button==xb::B && guide_blade!=BladeHome) {guide_blade=BladeHome;return {};}
    switch (button)
    {
    case xb::Up: item = std::max(0, item - 1); return {};
    case xb::Down: item = std::min(items - 1, item + 1); return {};
    case xb::LB:
    case xb::Left: guide_blade = std::max(era==3 && !metro_v1() && !guide_host?int(BladeGames):0, guide_blade - 1); return {};
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
        promotions=!promotions;clamp_all();return {Command::SaveSettings,-1};
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
    case GuideAction::OpenGuideSettings:
    case GuideAction::AdsSettings:
    case GuideAction::MusicPause:
    case GuideAction::MusicNext:
    case GuideAction::MusicPrevious:
    case GuideAction::MusicShuffle:
    case GuideAction::MusicStop: return {};
    case GuideAction::Music: return open_viewer(Viewer::Music);
    case GuideAction::AvatarEdit:
        viewer = Viewer::AvatarEdit;
        viewer_item = 0;
        viewer_full = false;
        avatar_category = 0;
        return {Command::AvatarEditStart, -1};
    case GuideAction::Pictures: return open_viewer(Viewer::Pictures);
    case GuideAction::Videos: return open_viewer(Viewer::Videos);
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
            }
            if (button==xb::Left) {if (slot%4>0) --next;else if (first>0) next=first-8+3;}
            sel=next;if (metro_focus!=-1) metro_focus=sel%8;
            return {};
        }
        else if (button == xb::Left || button == xb::Right || button == xb::Up || button == xb::Down)
        {
            const int direction = button == xb::Left ? 0 : button == xb::Right ? 1 : button == xb::Up ? 2 : 3;
            const int size=metro_page_size();
            const int page = sel / size, n = std::min(size, count() - page * size);
            const auto &neighbors=metro_v1_home()?kMetroV1HomeNeighbors:metro_v1()?kMetroV1Neighbors:kMetroNeighbors;
            int next = neighbors[direction][std::clamp(metro_focus, 0, kMetroRecent)];
            while (next >= 0 && next < size && next >= n) next = neighbors[direction][next];
            if (next >= 0)
            {
                metro_focus = next;
                if (next < size) sel = page * size + next;
            }
            else if (button == xb::Up) metro_focus = -1;
            else if (button == xb::Right && (page + 1) * size < count()) { sel = (page + 1) * size; metro_focus = 0; }
            else if (button == xb::Left && page > 0) { sel = (page - 1) * size; metro_focus = 0; }
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
        case Goal::ToSearch: return open_collection(Collection::All);
        case Goal::ToMediaCenter: return {Command::LaunchSystem, 2};
        case Goal::ToSettings:
            if (era >= 1) { system_page = 0; return open_viewer(Viewer::SystemSettings); }
            open_guide(); guide_blade = BladeSettings; break;
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
    if (guide && guide_host) // Ex-Box addition: no 360 original
        return {{xb::A,"Select"},{xb::B,"Back to Guide"},{xb::LB,"Previous section"},{xb::RB,"Next section"}};
    if (guide)
        return {{xb::A, "Select"}, {xb::B, "Back"}, {xb::Y, era == 3 ? "Xbox Home" : "Xbox Dashboard"}};
    if (viewer==Viewer::Achievements) {
        if (achievement_game.empty() && viewer_count()>0) return {{xb::A,"Select"},{xb::B,"Back"}};
        return {{xb::B,"Back"}};
    }
    if (viewer==Viewer::Themes || viewer==Viewer::Profile) return {{xb::A,"Select"},{xb::B,"Back"}};
    if (viewer==Viewer::SystemSettings)
    {
        if (details) return {{xb::A, "OK"}, {xb::B, "Back"}};
        if (storage_page()) {
            std::vector<Hint> out{{xb::B,"Back"}};
            if (!storage_entries().empty()) out.insert(out.begin(),{xb::A,"Select"});
            if (system_page==StorageDevices && !storage.devices.empty()) out.push_back({xb::Y,"Device Options"});
            if (system_page==StorageItems && !storage_item_list().empty()) out.push_back({xb::X,"Delete"});
            if (system_page==StorageCategories) out.push_back({xb::Y,"Transfer Content"});
            return out;
        }
        if (system_page==SystemInfo || system_page==SystemDateTime ||
            system_page==SystemResolution || system_page==SystemScreenFormat)
            return {{xb::B, "Back"}};
        return {{xb::A, "Select"}, {xb::B, "Back"}};
    }
    if (viewer==Viewer::AdsSettings)
        return {{xb::A,viewer_item>=5?"Toggle":"Change"},{xb::X,ad_show_all?"Ad candidates":"All resources"},{xb::LB,"Page"},{xb::B,"Back"}};
    if (info) return {{xb::A, "Select"}, {xb::B, "Back"}};
    if (details) return {{xb::A, "OK"}, {xb::B, "Back"}};
    if (viewer == Viewer::Library)
        return {{xb::A, "Play"}, {xb::Y, "Details"}, {xb::LB, "Page"}, {xb::B, "Back"}};
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
        std::vector<Hint> out;
        if (viewer_count() > 0)
            out.push_back({xb::A, "Select"}); // music/1003_IndividualDevice.xur legend_A, every build
        if (now_playing >= 0)
            out.push_back({xb::X, music_paused ? "Resume" : "Pause"});
        out.push_back({xb::Y, "Now Playing"}); // music/1003_IndividualDevice.xur legend_Y
        out.push_back({xb::B, "Back"});
        constexpr const char *next_view[] = {"Albums", "Artists", "Genres", "Saved Playlists", "Songs"}; // music/1003_IndividualDevice.xur
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
        out.push_back({xb::B, "Back"});
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
