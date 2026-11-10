// Ex-Box 360: what the dashboard shows and what each Xbox button does. Pure.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// No SDL, no files, no sockets: main.cpp draws this state and carries out the
// Effect a button returns (launch, refresh, save attachments, exit).
#pragma once

#include "xbox_input.hpp"
#include "dashboard_updates.hpp"
#include "network_state.hpp"
#include "modules/games_frontend/games_blade.hpp"
#include "modules/games_frontend/rom_game_guide.hpp"
#include "modules/games_frontend/dashboard_context.hpp"
#include "modules/dashboard_services/compat_health.hpp"

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dash
{
// Read-only memory inventory. -1 bytes/device means unknown, never zero by guess.
enum StorageCategory { StorageGames, StorageProfiles, StorageDemos, StorageVideos,
                       StorageThemes, StoragePictures, StorageAvatars, StorageSystem, StorageMusic,
                       StorageCategoryCount };
struct StorageDevice {
    std::string name, path;
    long long capacity = -1, free = -1;
    bool external = false;
};
struct StorageItem {
    std::string name, path, owner, created;
    long long bytes = -1;
    int device = -1, category = StorageGames;
};
struct StorageInventory {
    std::vector<StorageDevice> devices;
    std::vector<StorageItem> items;
    bool limited = false; // a bounded scan omitted further entries
};
std::string storage_size(long long bytes);

// A homebrew app (executable set) or an installed PS5/PS4 game (title_id set).
struct App
{
    std::string title;
    std::string executable;
    std::string title_id;
    std::string icon; // icon0.png on the console, or empty
    std::string playstation_icon{};
    std::string xbox_cover{};
};

// Dashboard-owned, read-only inventory supplied by main.cpp. G1 deliberately
// accepts immediate folder names and metadata only; it never scans game data.
struct GameFolderInventory
{
    exbox_games::Source source{};
    std::vector<exbox_games::Folder> folders;
    bool limited = false;
};

enum Channel
{
    Friends,
    Media,
    Games,
    Apps,
    MyXbox,
    Welcome, // homepage/emb_homepage.xml: visually before XBOX360; keep existing channel IDs stable.
    ChannelCount,
};

constexpr const char *channel_name(int channel)
{
    return channel == Welcome ? "Welcome"
           : channel == Friends ? "Friends"
           : channel == Media ? "Media"
           : channel == Games ? "Games"
           : channel == Apps  ? "Apps"
                              : "My Xbox";
}

// Card art. Matches the card_icons order main.cpp loads.
enum Icon
{
    IconDisc,
    IconGames,
    IconMusic,
    IconPhotos,
    IconSettings,
    IconVideo,
    IconAvatar,
    IconFriends,
    IconMessages,
    IconCount,
};

enum class Kind
{
    Hub,     // a fixed card on a channel
    App,     // a discovered homebrew app
    Game,    // an installed PS5 or PS4 game
    Pinned,  // an app or game attached to My Xbox
    Missing, // an attached app that is not installed now
    Empty,   // the placeholder on an app channel with nothing found
};

// What a fixed card does when selected.
// The Blades gamercard's row in a blade's list (always its first).
inline constexpr const char *kBladesGamercard = "Gamercard";

enum class Goal
{
    None,
    ToApps,
    ToGames,
    ToMedia,
    ControllerMap,
    Unavailable,
    ToPictures, // the screenshot viewer
    ToVideos,   // the game video library
    ToProfile,  // gamer profile actions, including Change Theme
    ToAvatar,   // the avatar stage
    ToMusic,    // the Music Library
    ToSearch,
    ToSettings,
    ToRomGameGuidePreview, // Experimental dashboard-only PS5X360 Guide preview
    ToWelcome,
    HideWelcome,
    ExitToPs5,
    ToMediaCenter, // Windows Media Center's slot: the PS5 Media Gallery
    // Ex-Box Metro Music dashboard tiles.  Appended to preserve the existing
    // Goal values used by bridge snapshots and older saved activity.
    ToMusicAlbums,
    ToMusicRandomAlbum,
    ToMusicMostPlayed,
    ToMusicPlaylists,
    ToMusicRecentlyAdded,
    ToMusicNowPlaying,
};

// What the Media viewer is showing over the hub.
enum class Viewer
{
    None,
    Pictures, // the PS5's screenshots
    Videos,   // every trailer on the console, as the 360 Marketplace's Game Videos
    Avatar,   // the owner's avatar on a stage: Left / Right turn it, A waves
    Music,    // the Music Library: songs on the console or a USB drive, in a list
    AvatarEdit, // the avatar editor: categories down the side, the avatar beside them
    Library,
    Profile,
    Achievements,
    Themes,
    DashboardEras,
    DashboardUpdates,
    AdsSettings,
    WelcomePages,
    InitialSetup,
    SystemSettings, // NXE / Kinect / Metro: consoles/SystemScene and its Console Settings page
    RomGameGuidePreview, // Experimental; never a cross-title overlay by itself
};
enum class Collection { All, Pins, Recent };
constexpr int kViewerColumns = 4, kViewerRows = 3;
constexpr int kMusicRows = 10; // songs the Music Library shows at once
constexpr int kMusicViews = 7; // authored five, plus Most Played and Recently Added
// Game Videos' player: Left / Right and the triggers seek, as the 360's did.
constexpr int kVideoStep = 10000, kVideoJump = 30000; // ms
// Closer than this to either end and there is nothing worth resuming.
constexpr int kResumeMin = 5000;
// videos/Strings.xus and VideoContent.scb GetNextSortString (9199).
constexpr const char *kVideoOrders[] = {"Sort by Download Date", "Sort by Title", "Sort by Type"};
constexpr const char *kVideoStartActions[] = {"Resume", "Restart"}; // videos/DetailsActions.xur
constexpr const char *kTheaterReactions[] = {"Wave", "Cheer", "Popcorn"};
constexpr int kTheaterReactionCount = int(sizeof(kTheaterReactions) / sizeof(kTheaterReactions[0]));
constexpr int kVideoOrderCount = 3;
// What main.cpp found of a clip on disk; absent means playable, length unknown.
struct VideoInfo
{
    int ms = 0;            // frames / fps
    long long bytes = 0;   // .frames + .wav
    bool playable = true;  // its packed frames are on the console
    bool subtitles = false; // a sidecar .srt beside them
    long long downloaded = 0; // packed file mtime; 0 if unknown
};
// Pictures: the grid's sort tabs (X moves along them), as Game Videos has.
// "Games" lists one tile per game; A opens its pictures and B comes back.
constexpr const char *kPictureOrders[] = {"Newest", "Oldest", "Name", "Games"}; // Ex-Box addition: no 360 original
constexpr int kPictureOrderCount = 4;
constexpr int kPictureGames = 3; // the "Games" tab
// Full screen: RT / LT step through these, and the stick or D-pad pans a
// zoomed picture (pan runs -kPicturePan..kPicturePan, an edge at each end).
constexpr float kPictureZooms[] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
constexpr int kPictureZoomCount = 5;
constexpr int kPicturePan = 1000;
constexpr int kSlideshowMs = 5000;
// A screenshot main.cpp found, and what its path and file say about it.
struct PictureFile
{
    std::string path;
    std::string title_id; // the folder the PS5 filed it under ("PPSA01949"), or ""
    std::string game;     // that title's name, when main.cpp knows it
    std::string taken;    // "YYYYMMDD_hhmmss" from the PS5's file name, or the file name
    long long bytes = 0;
    // The game's name, the title id, or "Other": what Name and Games sort on.
    std::string label() const { return !game.empty() ? game : !title_id.empty() ? title_id : "Other"; }
};
// What a capture's path says: the PS5 files it as
// /user/av_contents/photo/<TITLE>/<TITLE>/<n>/YYYYMMDD_hhmmss_<n>.jpg
PictureFile picture_file(const std::string &path);
// "2026-10-17  20:08" from "20261017_200850", or what was given.
std::string picture_date(const std::string &taken);

// PS5 system apps the Guide can open (title ids from psdevwiki's Title ID page).
struct SystemApp
{
    const char *title;
    const char *title_id;
};
constexpr SystemApp kSystemApps[] = {
    {"Game Library", "NPXS40071"},
    {"PlayStation Store", "NPXS40047"},
    {"Media Gallery", "NPXS40075"},
    {"Web Browser", "NPXS40169"},
};
constexpr int kSystemAppCount = int(sizeof kSystemApps / sizeof kSystemApps[0]);

struct Card
{
    std::string title;
    Kind kind = Kind::Hub;
    int icon = IconDisc;
    Goal goal = Goal::None;
    int app = -1;  // index into apps (App, Pinned homebrew)
    int game = -1; // index into games (Game, Pinned game)
    std::string image; // the title's own icon, when it has one
    int promo = -1;
    std::string preview{};
    std::string subtitle{};
};

// Ex-Box addition: no 360 original. A verified per-title source supplies a complete
// catalogue; absent titles remain unknown. Keys match recent (title:<ID> or executable).
enum class TrophyGrade { Bronze, Silver, Gold, Platinum };
constexpr int trophy_points(TrophyGrade grade) {
    return grade==TrophyGrade::Bronze?15:grade==TrophyGrade::Silver?30:grade==TrophyGrade::Gold?90:300;
}
struct Achievement {
    std::string name, description, earned_date;
    TrophyGrade grade=TrophyGrade::Bronze;
    bool earned=false, secret=false;
    int art() const { return earned?0:secret?2:1; } // arcade earned / xam unearned / secret
    // shared/LiveAll.xus (9199): concealed achievements reveal text only when earned.
    std::string display_name() const { return secret && !earned?"Secret":name; }
    std::string display_description() const {
        return secret && !earned?"This is a secret achievement. Unlock it to find out more about it.":description;
    }
    std::string display_date() const { return earned?earned_date:""; }
};
struct TitleAchievements {
    bool known=false;
    std::vector<Achievement> entries;
};
struct AchievementProgress {
    bool known=false;
    int earned=0, total=0, points=0, possible=0;
    bool complete() const { return known && total>0 && earned==total; }
};
struct PlayedGame { std::string key, title, image; };
// games/402_Achievements.xur (9199).
constexpr const char *kAchievementsEmpty="Your achievements will be listed here. Unlock achievements by playing games.";

struct AdEntry
{
    // Classification records where bytes came from; role records recovered
    // use. Neither turns an unverified resource into a historical campaign.
    std::string id, title, image, preview, subtitle, classification;
    std::string role="unknown", preferred_fit="contain";
    std::string caption_source="unknown";
    std::string game_id;
    std::uint32_t era_mask=0xffffffffu,page_mask=0xffffffffu;
    std::int64_t begin_utc=0,end_utc=0;
    int promotion_priority=0;
    bool authentic=false;
    bool auto_eligible=true;
    int width = 0, height = 0;
};

// The retail five blades plus one clearly labelled Ex-Box recording blade.
// Recording sits beside Media so capture controls remain inside the Guide.
enum GuideBlade
{
    BladeApps,     // where retail had Marketplace
    BladeGames,
    BladeHome,     // titled with the profile's name
    BladeMedia,
    BladeSettings,
    BladeCount,
};

enum class GuideAction
{
    MyXbox,
    Games,
    Apps,
    Media,
    Refresh,
    NetworkSettings,
    ControllerMap,
    ExitToPs5,
    Launch,
    LaunchTitle,
    Unavailable,
    ChangeGamerpic,
    ToggleSounds,
    SelectTheme,
    Profile,
    InitialSetup,
    CycleEra,
    CycleButtonStyle,
    ToggleDimming,
    CycleLibraryOrder,
    CycleNotifications,
    LibrarySummary,
    NotificationHistory,
    Pictures,     // open the screenshot viewer
    Videos,       // open the game video library
    RecordScreen, // toggle optional Glint recording; explain PS5 Create when unavailable
    LaunchSystem, // a PS5 system app (app = index into kSystemApps)
    Avatar,       // open the avatar stage
    Music,        // open the Music Library
    MusicPause,   // pause or resume the background music
    MusicNext,    // skip to the next song
    MusicStop,    // stop the background music
    MusicPrevious, // back a song (the scene Guides' mini player)
    MusicShuffle,  // shuffle on / off (the scene Guides' mini player)
    MusicVolume,   // open the original Guide volume slider (Ex-Box music only)
    GuideMusicAlbums,
    GuideMusicSongs,
    GuideMusicQueue,
    GuideMusicSearch,
    GuideMusicPlay,
    GuideMusicOpenFull,
    GuideMusicBack,
    OpenExBoxMenu, // open the dashboard-owned rows inside the ordinary Home blade
    AvatarEdit,   // open the avatar editor
    TogglePromotions,
    CyclePromotionMode,
    OpenGuideSettings,
    SystemSettings, // Guide System Settings opens the existing root page
    AdsSettings,
    Achievements, // gamer/PlayedGamePanelScene and games/402_Achievements
    RecentGames,  // Blades "Played Games": the library's recently played
    AllGames,     // Blades "Xbox Live Arcade": the whole library
    ConsoleSettings, // Blades System "Console Settings": the settings list
};

struct GuideEntry
{
    std::string label;
    GuideAction action = GuideAction::MyXbox;
    int app = -1; // for Launch (apps) and LaunchTitle (games)
    const char *node = ""; // authored Guide control; empty for host/mini-player entries
};

enum class Command
{
    None,
    Launch,
    Refresh,
    SavePins,
    ExitToPs5,
    NextGamerpic,
    LaunchTitle,
    SaveSettings,
    SaveCategories,
    SaveTrailers,
    SaveArtwork,
    RefreshMedia,
    RequestCover,
    PlayTrailer,
    ApplyPictureBackground, // copy the confirmed picture before committing the setting
    ScanPictures, // the Pictures viewer opened: look for screenshots
    LaunchSystem, // app = index into kSystemApps
    AvatarWave,   // the avatar waves (on My Xbox and its stage)
    ScanMusic,    // the Music Library opened: look for songs
    ScanGameMusic, // opt in, save, then include accessible audio under local game folders
    MusicBrowse,  // the Music Library's view or opened group changed: rebuild its rows
    MusicPlay,    // app = index into the library: play from there on
    MusicPause,   // model.music_paused says which
    MusicNext,
    MusicPrevious,
    MusicStop,
    MusicShuffle, // model.music_shuffle says which
    MusicRepeat,  // model.music_repeat says which
    MusicVolume,  // app = 0..10; Ex-Box music only, never game/system audio
    GuideMusicRefresh,
    GuideMusicPlay, // app = popup row; its song indices are already resolved
    MusicEdit,    // app = a MusicOp; model.music_target says on what
    OpenAvatarEditor, // separate original editor title; retain the current screen
    AvatarEditStart, // the editor opened: keep the look to go back to
    AvatarChange,    // app = -1 / +1: the current category's choice
    AvatarRandom,    // a random look
    AvatarEmote,     // the avatar does something (wave, dance ...)
    AvatarSave,      // keep the edited look
    AvatarCancel,    // put the old look back
    SaveAdProfiles,
    ReloadPromotions,
    VideoSeek,  // app = ms into the playing video
    VideoPause, // model.video_paused says which
    ScanStorage, // read-only inventory, refreshed when Memory / Storage opens
    ReadNetwork,
    TestNetwork,
    SaveVideos, // where each video was left, and which were watched
    SaveRenderScale, // app=1 or2; persist requested rendering scale for next startup
    ApplyInstalledTheme, // app>=0 owner catalog; -1 default; -2 Picture Background
    RecorderAction, // app: 0 start, 1 pause, 2 resume, 3 stop
};

// What a Music Library page asks main.cpp's player and library to do
// (music/1003-1030 in the owner's files: the options, Now Playing and Edit
// Current Playlist pages). The target is model.music_target: a library row,
// a place in the current playlist, or a picture.
enum class MusicOp
{
    OpenAlbumInfo, SaveAlbumInfo, // music/1025_EditAlbumInfo.xur
    OpenSong,       // the song page opens for library row music_target
    PlayGroup,      // the opened album / artist / genre / saved playlist
    AddGroup,       // ... added to the current playlist
    PlaySong,       // the song page's song, and the list after it
    AddSong,        // the song page's song added to the current playlist
    OpenCover,      // Choose Cover for the song page's song, or the opened album
    SetCover,       // the picture music_target is the cover from now on
    ClearCover,     // back to the song's own cover
    Jump,           // play the current playlist's song music_target now
    Remove,         // take song music_target out of the current playlist
    MoveUp,
    MoveDown,
    SavePlaylist,   // the current playlist saved (Playlist 1, 2 ...)
    ClearPlaylist,  // the current playlist emptied (the music stops)
    DeletePlaylist, // the opened saved playlist deleted
    RandomAlbum,    // choose a real album, excluding recent successful picks
};

struct Effect
{
    Command command = Command::None;
    int app = -1; // for Launch (apps) and LaunchTitle (games)
    const char *node = ""; // authored Guide control; empty for host/mini-player entries
};

// The 360's interface sounds, as the dashboard and the Guide used them.
enum class Sound
{
    None,
    Focus,       // moved along the hub
    Blocked,     // nothing further that way
    Select,      // opened something on the hub
    Back,        // went back on the hub
    Refused,     // selected something unavailable
    GuideOpen,
    GuideClose,
    GuideFocus,  // moved in the Guide's list
    GuideSelect,
    BladeSwitch, // LB / RB in the Guide
    // The NXE dashboard's own (controlp) sounds.
    ChannelUp,   // Up to the channel above
    ChannelDown,
    PaneLeft,    // along a channel, toward the first pane
    PaneRight,
    Unfold,      // the details page opens
    Fold,        // and closes
    SetupComplete, // oobe/celebrate_sound.xma (13604/15574).
    Launch,      // a game or app starts: the transition whoosh
};

// The details page Y opens for an app or a game: what it can be made to do.
enum class InfoAction
{
    Play,
    Attach,
    Detach,
    MoveEarlier, // on My Xbox
    MoveLater,
    ToGames, // homebrew: list it in Games
    ToApps,  // homebrew: back to Apps
    CycleTrailer, // Automatic -> each trailer -> None
    CycleArtwork, // Automatic -> PlayStation -> Xbox cover
    DownloadCover, // request an Xbox cover for this game
    PlayTrailer, // start the selected game's preview now
    RefreshMedia, // reread staged covers and trailers
    GameUpdates,  // return to PS5 Home for the selected game's update check
};

struct InfoEntry
{
    std::string label;
    InfoAction action;
};

// The title a details page is about, found again by key after any change.
struct InfoTarget
{
    std::string key;   // executable, or "title:<ID>"
    std::string title; // kept for a title that has gone missing
};

struct Hint
{
    xb::Button button;
    std::string label;
};

struct Notification
{
    std::string title;
    std::string text;
};

// gamer/Strings.xus (9199-17559): Xbox 360; Change Theme belongs to the profile.
// Only the installed default is verified. AuraScene colour states are not themes yet.
constexpr const char *kThemes[] = {"Xbox 360"};
constexpr int kThemeCount = int(sizeof kThemes / sizeof kThemes[0]);
struct InstalledTheme { std::string stable_id, display_name; bool complete=false; unsigned wallpapers=0; };
constexpr const char *kEras[] = {"Blades 1888-6770", "NXE 2008-9199", "Kinect 12413-13604", "Metro 15574-17559"};
constexpr int kEraCount = int(sizeof kEras / sizeof kEras[0]);

constexpr int kPageStep = 5; // panes per bumper press, as in WebOS NXE's hub
constexpr int kMetroPageSize=7, kMetroPins=7, kMetroRecent=8, kMetroAvatar=9;
constexpr int kMetroSocialOwner=10, kMetroSocialFriend0=11, kMetroSocialFriend2=13;
constexpr std::size_t kMaxPins = 8;
constexpr std::size_t kMaxRecent = 12;
constexpr std::size_t kMaxVideoRecent = 64;

class Model
{
  public:
    // State the renderer reads.
    int channel = MyXbox;
    int selected[ChannelCount] = {};
    int metro_tab = 1;
    bool blades_signed_out = false; // Blades X "Sign Out": the blades show their signed-out scenes
    // The System blade's second and third levels (dashSysCslSet, memory):
    // 0 the blade itself, 1 Console Settings, 2 Memory, 3 a storage device,
    // 4 Family Settings, 5 its Console Controls.
    int blades_page = 0;
    int blades_page_return[6] = {0, 0, 0, 0, 0, 0}; // the row to land on when B leaves each page
    std::string storage_free;                 // main.cpp: "%s free" for the console's drive
    std::string blades_page_title() const;
    // Where attached apps and games start on My Xbox. NXE and Kinect keep the
    // disc slot and Gamer Card first, as xbox360channel.xml orders them (the
    // Kinect home's avatar stands beside the second tile); elsewhere right
    // after the front card.
    int pin_first() const { return era == 1 || era == 2 ? 2 : 1; }
    void close_blades_page();
    int metro_focus = 0; // -1 tabs, 0..6 cards, 7 pins, 8 recent, 9 home avatar, 10..13 Social people
    int metro_selected[8] = {};
    Collection collection = Collection::All;
    int library_filter = 0; // 0 all, 1..26 A..Z, 27 numbers
    bool details = false;
    std::string details_title, details_text;
    bool guide = false;
    bool guide_host = false; // retired overlay compatibility bit; production Guide keeps this false
    bool picker_return_host = false, ad_return_host = false;
    int guide_blade = BladeHome;
    int guide_item[BladeCount] = {};
    int guide_media_control_item = 0; // horizontal mini-player icon row, 0..4
    int guide_community_item = 0;
    std::string player = "Player"; // the Home blade's title
    bool controller_map = false;
    std::vector<App> apps;  // homebrew
    std::vector<App> games; // installed PS5 / PS4 games
    exbox_games::Blade games_frontend; // independent 360/PS5 browse identities; no dispatch in G1
    // Experimental dashboard-safe preview only. Existing dashboard Guide input
    // is unchanged; a cooperative foreground host must opt into every capability.
    exbox_rom_guide::Model rom_game_guide_preview;
    GameFolderInventory games_frontend_inventory;
    std::size_t games_frontend_rejected = 0;
    bool games_frontend_inventory_published = false;
    bool games_frontend_limited = false;
    std::string context_focus_key;
    std::string saved_dashboard_context() const;
    void load_dashboard_context(const std::string &line);
    void restore_context_focus();
    std::vector<std::string> pins; // executables, or "title:<ID>" for games
    std::set<std::string> game_apps; // homebrew executables shown in Games
    // Trailers on the console, by key, and the ones the owner chose per title:
    // key -> trailer key, or "" for no trailer. Absent means match by name.
    std::vector<std::string> trailers;
    std::map<std::string, std::string> trailer_choice;
    // A trailer's display name ("Tony Hawk's Pro Skater HD") by key.
    std::map<std::string, std::string> trailer_names;
    std::map<std::string, int> artwork_choice; // title ID -> 1 PlayStation, 2 Xbox; absent = Automatic
    bool sounds = true;
    bool promotions = false;
    int promotion_mode=0; // 0 Authentic period playlist, 1 Local Personalized, 2 Off
    bool load_promotion_mode_setting(const char* line);
    std::string saved_promotion_mode_setting()const;
    std::vector<Card> promo_cards;
    std::vector<AdEntry> ad_catalog;
    bool ad_show_all = false;
    // Controller cursors index the filtered list. Saved picks always use full
    // source SHA identities, so filtering never renumbers a saved selection.
    int ad_visible_count() const {
        int count=0;
        for (const auto &entry:ad_catalog) {
            if (ad_show_all || entry.auto_eligible) {
                ++count;
            }
        }
        return count;
    }
    int ad_catalog_index(int visible_index) const {
        for (size_t i=0;i<ad_catalog.size();i++) {
            if (ad_show_all || ad_catalog[i].auto_eligible) {
                if (visible_index==0) {
                    return int(i);
                }
                --visible_index;
            }
        }
        return -1;
    }
    std::array<int,6> ad_mode{};
    std::array<std::vector<std::string>,6> ad_selected;
    std::array<unsigned,6> ad_shuffle_seed{};
    int ad_return_item = 0;
    Viewer ad_previous_viewer = Viewer::None;
    int ad_previous_item = 0;
    bool ad_previous_full = false;
    int current_ad_profile() const {
        return era==0 ? (dashboard_build<5759?0:1) : dashboard_family(era,dashboard_build)+1;
    }
    int theme = 0;       // default dashboard artwork for the active era
    std::vector<InstalledTheme> installed_themes;
    std::string installed_theme_id;
    std::vector<std::string> theme_favorites,theme_recent;
    int theme_filter=0; // 0 all, 1 favorites, 2 recently used
    void load_theme(int legacy_value);
    void set_installed_themes(std::vector<InstalledTheme> entries);
    void load_installed_theme_setting(const std::string &line);
    std::string saved_installed_theme_setting() const;
    std::vector<std::string> saved_theme_library_settings() const;
    std::vector<int> theme_visible_indices() const;
    int theme_catalog_index_for_row(int row) const;
    int theme_row_for_catalog_index(int index) const;
    int theme_count() const;
    std::string theme_entry(int index) const;
    std::vector<std::string> theme_entries() const;
    std::vector<GuideEntry> profile_entries() const;
    Effect open_themes();
    std::map<std::string,TitleAchievements> achievement_source;
    std::string achievement_game;
    int achievement_list_item=0;
    Viewer achievement_previous_viewer=Viewer::None;
    int achievement_previous_item=0;
    bool achievement_previous_full=false;
    std::vector<PlayedGame> achievement_games() const;
    const TitleAchievements *achievement_title(const std::string &key) const;
    AchievementProgress achievement_progress(const std::string &key) const;
    std::string achievement_counts(const std::string &key, bool points=false) const;
    int achievement_count() const;
    Effect open_achievements();
    Effect press_achievements(xb::Button button);
    Viewer theme_previous_viewer = Viewer::None;
    int theme_previous_item = 0;
    bool theme_previous_full = false, theme_previous_guide = false;
    int era = 1;         // visual layout; shared library and launch behavior
    int dashboard_build = 9199;
    int picker_family = 1;
    int picker_return_item = 0;
    bool picker_previews = false;
    Viewer picker_previous_viewer = Viewer::None;
    int picker_previous_item = 0;
    bool picker_previous_full = false;
    bool metro_v1() const {return era==3 && dashboard_family(era,dashboard_build)==3;}
    bool metro_avatar_home() const {return era==3 && metro_tab==1;}
    bool metro_v1_home() const {return metro_v1() && metro_tab==1;}
    int metro_page_size() const {return era==3 && metro_tab==7?8: metro_v1_home() && promotions && ad_mode[current_ad_profile()]!=0?4:metro_v1()?5:kMetroPageSize;}
    int button_style = 0; // 0 polished glossy, 1 retail, 2 simple
    bool dimming = true; // dim the screen after ten idle minutes
    int render_scale = 1; // Ex-Box request; applied at next startup after persistence
    int active_render_scale = 1; // actual runtime target, set only by startup
    bool welcome_visible = true; // settings.txt; Welcome Channel and Hide Channel share this.
    bool welcome_available() const { return (era==1 || era==2) && welcome_visible; }
    int channel_rank(int c) const { return c==Welcome?4:c==MyXbox && welcome_available()?5:c; }
    void load_welcome_setting(const std::string &line);
    std::string saved_welcome_setting() const;
    struct WelcomePage { std::string title, body, art; };
    std::map<std::string,WelcomePage> welcome_content;
    void load_welcome_content(const std::vector<std::string> &lines);
    std::vector<std::string> welcome_pages() const;
    std::string welcome_key() const;
    WelcomePage welcome_page() const;
    bool welcome_hot=false, welcome_confirm=false, welcome_yes=false;
    enum SetupStep { SetupIntro, SetupResolution, SetupAutoOff, SetupClock, SetupDone };
    int setup_step=SetupIntro;
    Viewer setup_previous_viewer=Viewer::None;
    int setup_previous_item=0;
    Effect open_setup();
    Effect press_welcome(xb::Button button);
    bool clock_24_hour = false; // settings.txt; the Guide and Console Settings share this format
    int library_order = 0; // 0 recent, 1 title A-Z, 2 title Z-A
    int notification_level = 2; // 0 off, 1 short, 2 standard, 3 long
    bool local_friend_avatars = true; // validated editor friends, then deterministic local-kit fallback; never Xbox Live identities
    bool game_library_companion = false; // read-only runtime discovery source shown in Settings
    std::vector<Notification> notification_history; // newest first, this session only
    // Recently played apps and games (keys as for pins), most recent first.
    std::vector<std::string> recent;
    // The Media viewer: a grid of pictures or videos, or one of them full screen.
    Viewer viewer = Viewer::None;
    int viewer_item = 0;
    bool viewer_full = false;
    // Game Videos: what is on disk per trailer key, the order the grid shows,
    // which were watched (most recent first) and where each was left.
    std::map<std::string, VideoInfo> video_info;
    int video_order = 0; // kVideoOrders; unknown/equal download dates keep manifest order
    std::string video_pending; // DetailsActions bookmark choice; empty when closed
    int video_start_item = 0; // Resume / Restart
    void load_video_order(int value, bool legacy);
    std::vector<std::string> video_recent;
    std::map<std::string, int> video_resume; // key -> ms
    // The full-screen player; main.cpp reports the position each frame.
    int video_elapsed = 0; // ms
    bool video_paused = false;
    bool video_subtitles = true;
    bool video_theater = false; // Ex-Box offline Theater Party room; RS toggles full-screen
    int video_theater_reaction = 0;
    unsigned video_theater_reaction_serial = 0;
    std::vector<std::string> video_list() const; // the grid: playable clips, in order
    std::string video_key() const;               // the selected clip, or ""
    int video_length() const;                    // its ms, 0 when unknown
    int video_hidden() const;                    // in the manifest but not on the console
    void load_videos(const std::vector<std::string> &lines);
    std::vector<std::string> saved_videos() const; // "key\tms", most recent first
    int pictures = 0;      // how many screenshots main.cpp found
    // Pictures: the files (newest first, as found), the sort, and the game
    // opened from the Games tab (-1: the list of games).
    std::vector<PictureFile> picture_files;
    int picture_order = 0; // index into kPictureOrders, kept in the settings
    int picture_group = -1;
    // Full screen: the slideshow, the info panel, and the zoom and pan.
    bool picture_slideshow = false;
    bool picture_info = false;
    // pictures/slideshowControls.xur: Play, Previous, Stop, Next, Shuffle, Loop, Rotate CW/CCW.
    int picture_control = -1; // -1: picture navigation; Down enters the transport row
    int picture_rotation = 0; // clockwise quarter turns, display-only
    bool picture_repeat = true, picture_shuffle = false;
    std::vector<int> picture_visited;
    unsigned picture_random = 0x3609199u;
    bool picture_background_confirm = false, picture_background_yes = false;
    std::string picture_background_pending;
    int picture_background = 0; // owned BMP slot (0: theme; 1/2: alternating copies)
    bool picture_background_enabled = true; // legacy T09 slots remain selected on load
    bool picture_background_available() const { return era == 1 || era == 2; }
    const char *picture_control_help() const;
    std::vector<std::string> saved_picture_settings() const;
    void load_picture_setting(std::string line);
    int picture_zoom = 0; // index into kPictureZooms
    int picture_pan_x = 0, picture_pan_y = 0;
    void set_pictures(std::vector<PictureFile> found); // keeps the selected one
    bool picture_groups() const { return viewer == Viewer::Pictures && picture_order == kPictureGames && picture_group < 0; }
    std::vector<std::string> picture_games() const; // the Games tab, A-Z
    std::vector<int> picture_list() const;          // the grid: indices into picture_files
    std::vector<int> picture_group_list(int group) const; // one game's, newest first
    const PictureFile *picture() const;             // the selected one (a game's newest), or null
    // The slideshow's next picture (main.cpp's clock says when); false when none runs.
    bool slideshow_step();
    int avatar_angles = 0; // turntable frames of the installed avatar (0: none)
    int songs = 0;         // rows the Music Library shows (songs, or albums / artists)
    // How the Music Library is browsed, as the 360's player offered it:
    // Songs (A-Z), Albums, Artists or Genres; in the last three, A opens one
    // and B goes back. main.cpp builds the rows; the model only keeps the place.
    int music_view = 0;    // 0 Songs, 1 Albums, 2 Artists, 3 Genres, 4 Saved Playlists
    bool music_random_focused = false; // sixth Library action; transient, never saved
    bool music_game_scan_focused = false; // seventh Library action; transient, never saved
    bool music_tabs_focused = false; // D-pad focus on the library view selector.
    bool music_game_scan = false; // include accessible audio in configured local game folders
    int music_group = -1;  // the album / artist opened, or -1 for the list of them
    bool music_in_groups() const { return music_view > 0 && music_view <= 4 && music_group < 0; }
    // The pages over the library (the 360's music scenes): 0 the library,
    // 1 a song's options (1016_IndividualSongMain), 2 Now Playing
    // (1028_NowPlaying), 3 Edit Current Playlist (1030_EditPlaylist),
    // 4 Choose Cover (Ex-Box addition), 5 Delete Saved Playlist (Strings.xus).
    enum MusicPage { MusicLibrary, MusicSong, MusicNowPlaying, MusicEditPlaylist, MusicChooseCover, MusicConfirmDelete, MusicAlbumInfo };
    // 9199 music/1025_EditAlbumInfo.xur; input grid is an Ex-Box addition.
    std::array<std::string,3> music_album_fields{}, music_album_original{};
    int music_album_field=-1, music_album_key=0;
    unsigned music_album_changed=0;
    std::string music_album_input;
    static constexpr const char *music_album_keys="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .-'!?&()";
    Effect press_album_keyboard(xb::Button button);
    void album_save_result(bool ok);
    int music_page = MusicLibrary;
    int music_library_item = 0; // the library's place while another page is open
    int music_cover_from = MusicLibrary; // the page Choose Cover goes back to
    int music_target = -1;  // what a MusicEdit effect is about
    int music_queue = 0;    // songs in the current playlist (main.cpp, every frame)
    int music_covers = 0;   // pictures Choose Cover offers (main.cpp)
    // An opened group's rows begin with its options (1005_MediaContainerOptions).
    std::vector<std::string> music_group_actions() const;
    std::vector<std::string> music_page_rows() const; // the fixed rows of pages 1, 2, 3 and 5
    // The avatar editor: main.cpp says whether there is a kit and how many categories.
    bool external_avatar_editor = false; // Ex-Box addition: PPSA99734
    bool avatar_editable = false;
    int avatar_categories = 0;
    int avatar_category = 0;
    // Background music, as main.cpp's player reports it each frame.
    int now_playing = -1; // -1: nothing playing
    bool music_paused = false;
    // Set by the original-scene Guides (NXE on): the Media blade's rows come
    // first and its mini player last, in the scene's top-to-bottom order:
    // 1 controls above the song (NXE), 2 the song above the controls.
    int media_player = 0;
    // The player's order and repeat, kept in the settings: LS click shuffles,
    // RS click turns repeat Off -> All -> One (the PS5 keeps Create).
    bool music_shuffle = false;
    int music_repeat = 0; // 0 off, 1 the list, 2 the song
    int music_volume = 8; // 0..10; 8 preserves the player's previous 205/256 mix
    int music_volume_restore = 8;
    bool guide_volume_open = false;
    bool guide_exbox_page = false; // normal Home-blade subpage; B returns to the retail rows
    struct GuideMusicRow { std::string label, detail; std::vector<int> songs; };
    int guide_music_page = 0; // 0 Media blade, 1 albums, 2 songs, 3 queue, 4 search, 5 Music root
    std::vector<GuideMusicRow> guide_music_rows;
    std::string guide_music_query;
    int guide_music_key = 0;
    bool guide_music_search_edit = false;
    std::string now_playing_title;
    int viewer_count() const
    {
        return viewer == Viewer::WelcomePages ? (welcome_confirm?2:int(welcome_pages().size()))
               : viewer == Viewer::InitialSetup ? 5
               : viewer == Viewer::Themes ? theme_count()
               : viewer == Viewer::Achievements ? achievement_count()
               : viewer == Viewer::Profile ? int(profile_entries().size())
               : viewer == Viewer::DashboardEras ? 5
               : viewer == Viewer::AdsSettings ? ad_visible_count()+5
               : viewer == Viewer::DashboardUpdates ? int(updates_for(picker_family,picker_previews).size())
               : viewer == Viewer::Library ? int(library_cards().size())
               : viewer == Viewer::Pictures ? (picture_groups() ? int(picture_games().size())
                                                : picture_group >= 0 ? int(picture_list().size()) : pictures)
               : viewer == Viewer::Videos ? int(video_list().size())
               : viewer == Viewer::Avatar || viewer == Viewer::AvatarEdit ? avatar_angles
               : viewer == Viewer::Music  ? music_rows()
               : viewer == Viewer::SystemSettings ? int(system_entries().size())
               : viewer == Viewer::RomGameGuidePreview ? int(rom_game_guide_preview.rows().size())
                                          : 0;
    }
    // The details page.
    bool info = false;
    int info_item = 0;
    InfoTarget info_target;

    int count() const;
    bool has_rom_game_guide_preview() const;
    int current() const
    {
        return selected[channel];
    }
    Card card(int index) const;
    Card card_base(int index) const;
    Card current_card() const
    {
        return card(current());
    }
    // The selected card is a game or app, so a trailer may play for it.
    bool current_is_title() const;
    bool content_focused() const { return era != 3 || (metro_focus >= 0 && metro_focus < metro_page_size()); }
    void select_metro_tab(int tab);
    std::vector<Card> library_cards() const;
    Effect open_collection(Collection which);
    bool pinned(const std::string &key) const;
    // What attaching a card stores: an executable, or "title:<ID>".
    static std::string game_key(const App &game)
    {
        return "title:" + game.title_id;
    }
    // Homebrew the owner has put in Games, and what is left in Apps.
    std::vector<int> game_homebrew() const;
    std::vector<int> app_homebrew() const;
    // Installed games, most recently played first, then by name.
    std::vector<int> game_order() const;
    // Recording a launch: that title moves to the front of its channel.
    void played(const std::string &key);
    void record_notification(const std::string &title, const std::string &text);
    void load_recent(const std::vector<std::string> &lines);
    // A key's place in recent (recent.size() when never played).
    int recency(const std::string &key) const;
    // The details page: the title's own card and the actions it offers.
    Card info_card() const;
    std::vector<InfoEntry> info_entries() const;
    void load_categories(const std::vector<std::string> &lines);
    // "Automatic", a trailer's name, or "None" for a title's key.
    std::string trailer_label(const std::string &key) const;
    // Moves a title's choice through Automatic, each trailer, None (wrapping).
    void step_trailer(const std::string &key, int step);
    std::string artwork_label(const std::string &title_id) const;
    void step_artwork(const std::string &title_id, int step);

    // The Guide's blades: titles and the open blade's list.
    std::string blade_title(int blade) const;
    std::vector<GuideEntry> blade_entries(int blade) const;
    std::vector<GuideEntry> exbox_guide_entries() const;
    GuideEntry preferred_guide_play() const;
    // Additional Ex-Box rows and nested pages rendered by the normal Guide.
    std::vector<GuideEntry> host_blade_entries(int blade,bool full=false) const;
    // Blades (era 0): each blade's own items as the 1888 dashboard lists them
    // (dashmain / gamesbla / mediabla / live scenes), each mapped to a PS5 action.
    std::vector<GuideEntry> blades_items(int channel) const;
    // consoles/dashSysCslSet*.xur (9199, 13604, 15574, 17559).
    enum SystemPage { SystemRoot, SystemConsole, SystemDisplay, SystemAudio, SystemClock,
                      SystemStartup, SystemInfo, SystemSoundEffects, SystemTimeFormat,
                      SystemScreenSaver, SystemDateTime, SystemResolution, SystemScreenFormat,
                      SystemExBox, SystemGameMusic, SystemLocalFriends, SystemLibraryStatus,
                      StorageDevices, StorageSummary, StorageCategories, StorageItems,
                      StorageDetails, StorageOptions, NetworkMain, NetworkTesting,
                      NetworkDetails, NetworkIP, NetworkDNS, NetworkConsole, SystemWelcome,
                      SystemHealth, SystemDiagnostics, SystemFamily, SystemAccount,
                      SystemKinect, SystemComputers, SystemTV, SystemCamera,
                      SystemLanguage, SystemAutoPlay, SystemConnectedDevices,
                      SystemMusicVolume };
    NetworkInfo network;
    NetworkTest network_test;
    unsigned network_generation=0;
    bool network_from_blades=false;
    bool developer_diagnostics_available=false; // hidden unless the explicit local flag is present
    bool recorder_available=false, recorder_recording=false, recorder_paused=false, recorder_pause_supported=false;
    bool recorder_busy=false, recorder_error=false;
    int recorder_control=0; // horizontal transport: 0 Play/Resume, 1 Pause, 2 Stop
    double recorder_seconds=0.0;
    std::string recorder_name;
    exbox_services::health::Snapshot health_runtime;
    exbox_services::health::Snapshot compat_health_snapshot() const;
    std::vector<exbox_services::health::Row> compat_health_rows() const;
    bool network_page() const { return system_page>=NetworkMain && system_page<=NetworkConsole; }
    std::vector<GuideEntry> network_entries() const;
    std::string network_value(const std::string &label) const;
    Effect press_network(xb::Button button);
    int system_page = 0;
    StorageInventory storage;
    int storage_device = -1, storage_category = 0, storage_item = -1;
    bool storage_page() const { return system_page >= StorageDevices && system_page <= StorageOptions; }
    std::string storage_category_name(int category) const;
    std::vector<int> storage_item_list() const;
    std::vector<GuideEntry> storage_entries() const;
    std::string storage_body() const;
    std::string storage_row_size(int row) const;
    int storage_row_icon(int row) const;
    Effect press_storage(xb::Button button);
    struct SystemReturn { int page, item; };
    std::vector<SystemReturn> system_history;
    void open_system_page(int page, int item = 0);
    std::string system_page_title() const;
    std::string system_value(const std::string &label) const;
    std::string system_description() const;
    std::vector<GuideEntry> system_entries() const;
    std::string system_meta(const std::string &label) const; // the item's verbatim description
    Effect run_blades_item(const GuideEntry &entry);
    Effect perform_guide_entry(const GuideEntry &entry);
    void open_guide();

    // A button press (or a repeat of a held direction or bumper).
    Effect press(xb::Button button);
    // The prompts along the bottom for the current state.
    std::vector<Hint> hints() const;

    // After discovery: keep every channel's place in range.
    void set_apps(std::vector<App> found);
    void set_games(std::vector<App> found);
    void set_game_frontend_inventory(GameFolderInventory found);
    void refresh_game_frontend();
    std::vector<std::string> saved_game_frontend_settings() const;
    void load_game_frontend_setting(const std::string &line);
    void load_pins(const std::vector<std::string> &lines);
    // Tell the model a launch failed so it can say so.
    void launch_failed(const std::string &title);

  private:
    Effect press_hub(xb::Button button);
    int base_count() const;
    Effect press_guide(xb::Button button);
    Effect press_info(xb::Button button);
    Effect press_viewer(xb::Button button);
    Effect press_music_page(xb::Button button);
    int music_rows() const;
    Effect open_viewer(Viewer which);
    Effect press_video_full(xb::Button button);
    Effect press_picture_full(xb::Button button);
    void reset_picture_view();
    Effect request_video();
    void enter_video(bool resume = true);
    void leave_video();
    void open_info(const Card &card);
    void go(int channel_index);
    void clamp_all();
    void toggle_pin(const std::string &key);
    void show(const std::string &title, const std::string &text);
};
// The media screens' wording (Music, Videos, Pictures), era by era, as the
// retail dashboards' string tables and scenes have it (strings/*.json).
namespace media_text {
enum class Build { Blades1888, Blades6717, Nxe9199, Kinect13604, MetroV1, MetroV2 };
inline Build build(const Model &m)
{
    if (m.era == 0) return m.dashboard_build < 5759 ? Build::Blades1888 : Build::Blades6717;
    if (m.era == 1) return Build::Nxe9199;
    if (m.era == 2) return Build::Kinect13604;
    return m.metro_v1() ? Build::MetroV1 : Build::MetroV2;
}
// music/1028_NowPlaying.xur: "Music Player" in 1888, "Now Playing" from 6717
// (and the Y legend of music/1003_IndividualDevice.xur after it).
inline const char *now_playing(const Model &m)
{
    return build(m) == Build::Blades1888 ? "Music Player" : "Now Playing";
}
// An empty view: music/1003_SongList.xur "No songs found." (every build);
// the group lists' texts are in 1888's music/Strings.xus only.
inline const char *music_empty(int view)
{
    constexpr const char *kEmpty[] = {"No songs found.", "No albums found", "No artists found", "No genres found",
                                      "No playlists found"};
    return kEmpty[view < 0 || view > 4 ? 0 : view];
}
// The Videos page's name: the Media blade's entry (mediabla/mediaSignedIn.xur
// "Video" in 1888, "Videos" in 6717), then homepage/strings.xus "Video Library"
// (9199, 13604; the Metro builds keep 13604's library scenes unchanged).
inline const char *videos_title(const Model &m)
{
    const Build b = build(m);
    return b == Build::Blades1888 ? "Video" : b == Build::Blades6717 ? "Videos" : "Video Library";
}
// pictures/900_PicturesMain.xur and the Media blade "Pictures" (1888, 6717),
// then homepage/strings.xus "Picture Library" (9199, 13604, and Metro as above).
inline const char *pictures_title(const Model &m)
{
    return m.era == 0 ? "Pictures" : "Picture Library";
}
// A picture on its way: pictures/slideshowControls.xur "Loading picture"
// (6717, 9199); the others showed only the spinner.
inline const char *loading_picture(const Model &m)
{
    const Build b = build(m);
    return b == Build::Blades6717 || b == Build::Nxe9199 ? "Loading picture"
                                                        : "Loading..."; // Ex-Box addition: no 360 original
}
// The player's caption toggle: Metro V2's dashcomm/dashStrings.xus "Closed
// Captions On" / "Closed Captions Off"; before it there was no such legend.
inline const char *captions(const Model &m, bool on)
{
    if (build(m) == Build::MetroV2) return on ? "Closed Captions Off" : "Closed Captions On";
    return on ? "Subtitles Off" : "Subtitles On"; // Ex-Box addition: no 360 original
}
} // namespace media_text
// Which sound a press makes, from the state before and after it.
Sound sound_for(const Model &before, const Model &after, xb::Button button, const Effect &effect);
} // namespace dash
