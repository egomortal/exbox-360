#include "async_log.hpp"
// Ex-Box 360: native NXE-inspired dashboard for PS5 homebrew.
// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef NXE_DIRECT_VIDEO
#ifdef __PROSPERO__
#error Direct video is a desktop integration test until native presentation is verified.
#endif
#include "modules/direct_video/transport.hpp"
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include "direct_blit.hpp"
#include "concepts/nxe_bridge.hpp"
#include "dash_model.hpp"
#include "network_runtime.hpp"
#include "recorder_runtime.hpp"
#include "dashboard_layout.hpp"
#include "media_transport_layout.hpp"
#include "image_resample.hpp"
#include "era_chrome.hpp"
#include "chrome_startup.hpp"
#include "blades_rail_roles.hpp"
#include "xui_navigation.hpp"
#include "xui_viewport.hpp"
#include "xui_renderer.hpp"
#include "custom_scene_playback.hpp"
#include <fstream>
#ifdef EXBOX_NATIVE_TITLE
#include "native_audio_endpoint.hpp"
extern "C" void exbox_native_note(const char*,...);
#endif
#include "ps5_library.hpp"
#include "system_data.hpp"
#include "modules/editor_dashboard/bridge.hpp"
#include "modules/avatar_slots/slot.hpp"
#include "xbox_input.hpp"
#include "hdr_image.hpp"
#include "music.hpp" // compiles the audio decoders: this file only
#include "music_metadata.hpp"
#include "startup_job.hpp"
#include "music_index.hpp"
#include "modules/themes/theme_manifest.hpp"
#include "modules/themes/theme_runtime.hpp"
#include "modules/games_frontend/music_library_activity.hpp"
#include "modules/dashboard_director/dashboard_director.hpp"
#include "modules/dashboard_director/friends_stage.hpp"
#include "modules/dashboard_services/dashboard_diagnostics.hpp"
#include "modules/dashboard_services/promotion_scheduler.hpp"
#include "modules/dashboard_services/rom_guide_companion.hpp"
#include "avatar_kit.hpp"
#include "modules/editor_dashboard/prepared_actor.hpp"
#include "avatar_alpha.hpp"
#include "x21/packet_builder.hpp"
#include "subtitles.hpp"
#include <memory>
#ifdef NXE_JXR
#include "jxr_glue.h"
#endif

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#ifdef __PROSPERO__
extern "C" int sceSystemServiceLoadExec(const char *path, const char **arguments);
extern "C" int sceKernelUsleep(unsigned int microseconds);
extern "C" int sceUserServiceInitialize(const void *parameters);
extern "C" int sceUserServiceGetInitialUser(int *user);
extern "C" int sceUserServiceGetUserName(int user, char *name, size_t size);
#endif

#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
#include "session_adapter.hpp"
#include "native_session.h"
extern "C" [[noreturn]] void catchReturnFromMain(int status);
extern "C" void exbox_presenter_log(const char* text){exbox_native_note("%s",text);}
#endif

#include "random_album.hpp"
#include "render_density.hpp"
#include "x21/fullapp_gpu_hook.hpp"
#include "x21/opaque_surface.hpp"
#include "x21/layer_scope.hpp"
#include "x21/guide_commands.hpp"
#include "x21/slot_builder.hpp"
#include "x21/circle_command.hpp"
#include "x21/reflection_command.hpp"
#ifndef __PROSPERO__
#include "x21/host_audit.hpp"
#else
#include "x21/native_2d_client.h"

#endif
#include "render_preference.hpp"
#include "solid_blend.hpp"
#include "reflection_blend.hpp"
#include "realtime_scale.hpp"
#include "video_output_storage.hpp"
#include "frame_pacing.hpp"
#include "frame_metrics.hpp"
#include "packed_media_prep.hpp"
#include "packed_thumbnail_prep.hpp"
#include "text_cache.hpp"
#undef SDL_BlitSurface
#undef SDL_BlitScaled
#define SDL_BlitSurface x21hook::blit
#define SDL_BlitScaled x21hook::blit_scaled
#define SDL_FillRect x21hook::fill
#define SDL_SetClipRect render_density::set_clip
#define SDL_GetClipRect render_density::get_clip
#define SDL_FreeSurface render_density::free_surface
#define TTF_OpenFont render_density::open_font
#define TTF_CloseFont render_density::close_font
#define TTF_FontHeight render_density::font_height
#define TTF_SizeUTF8 render_density::size_utf8
#define TTF_RenderUTF8_Blended render_density::text

namespace {
exbox_services::diagnostics::Metrics dashboard_diagnostics;
exbox::dashboard_director::PromotionClock promotion_rotation_clock;
chrome_startup::Job chrome_startup_job;
bool chrome_startup_once=false,chrome_startup_deferred=false;
void begin_chrome_startup(){
    if(chrome_startup_once)return;
    chrome_startup_once=true;
    // Startup-only: published and queued packet storage share one cap.
    if(era_chrome::chrome_backing_bytes || era_chrome::retained_chrome_count())return;
    for(const auto& family:era_chrome::chrome_backing)for(const auto& packet:family)if(!packet.empty())return;
    chrome_startup_deferred=chrome_startup_job.start(render_density::factor);
}
void finish_chrome_startup(){
    if(!chrome_startup_deferred)return;
    chrome_startup_deferred=false;
    chrome_startup::Result result;
    if(!chrome_startup_job.take(result)||!result.ok||result.density!=render_density::factor||result.bytes>era_chrome::chrome_backing_limit){result=chrome_startup::Result{};era_chrome::prepare_backing();return;}
    SDL_Surface* probe=SDL_CreateRGBSurfaceWithFormat(0,1,1,32,SDL_PIXELFORMAT_ARGB8888);
    if(!probe){result=chrome_startup::Result{};era_chrome::prepare_backing();return;}
    SDL_BlendMode blend;SDL_GetSurfaceBlendMode(probe,&blend);SDL_FreeSurface(probe);
    era_chrome::free_backing();
    for(int f=0;f<2;++f)for(int a=f?1:0;a<5;++a){std::swap(era_chrome::chrome_backing[f][a],result.packets[f][a]);era_chrome::chrome_backing_blend[f][a]=blend;}
    era_chrome::chrome_backing_bytes=result.bytes;era_chrome::chrome_backing_density=result.density;
    era_chrome::chrome_backing_failures=0;era_chrome::chrome_raster_fallbacks=0;
}

constexpr int W = 1920, H = 1080;
int persisted_render_scale=1;
constexpr const char *root = "/data/homebrew/ExBox360/";
constexpr const char *pins_path = "/data/nxe/attachments.txt";
SDL_Window *window;
SDL_Surface *screen, *display_surface, *background;
SDL_Surface *hub_backdrop=nullptr;
SDL_Surface *library_base=nullptr;
TTF_Font *regular, *large, *small, *semibold, *era_text;
TTF_Font *kinect_tag=nullptr, *kinect_count=nullptr; // the Kinect home's top-right gamertag and counters (measured: 33 / 26 px caps)
TTF_Font *font_sets[2][5]{};
TTF_Font *metro_tabs_font=nullptr;
TTF_Font *blades_body=nullptr, *blades_caption=nullptr, *blades_heading=nullptr;
TTF_Font *nxe_channel[5]{}; // the NXE channel stack, current to farthest (designer capture sizes)
TTF_Font *blades_rail[3][2]{}; // [source point regime][selected]; calibrated native sizes.
void select_typeface(int era) {
    auto &fonts=font_sets[era>=2?1:0];
    regular=fonts[0];large=fonts[1];small=fonts[2];semibold=fonts[3];era_text=fonts[4];
}
SDL_Surface *glyphs[xb::Count]{};
SDL_Surface *retail_glyphs[xb::Count]{}, *simple_glyphs[xb::Count]{};
// Each era's own A/B/X/Y art (shared/*-Button: NXE's glossy orbs, the 2010-12
// flat discs), with the letter laid over it as the scenes did. Built once at
// 96 px; index [family][button]: 0 NXE, 1 Kinect, 2 Metro V1, 3 Metro V2.
SDL_Surface *era_glyphs[4][4]{};
// Guide skin controller art: 13604, 15574, 17559; sparse xb::Button slots.
SDL_Surface *era_controller_glyphs[3][xb::Count]{};
SDL_Surface *card_art[5]{};
SDL_Surface *card_icons[dash::IconCount]{}, *blades_icons[dash::IconCount]{};
SDL_Surface *era_icons[dash::IconCount]{}, *era_panel=nullptr, *era_floor=nullptr;
SDL_Surface *era_background=nullptr,*era_aura_floor=nullptr;
SDL_Surface *era_banner=nullptr;
bool era_light_navigation=false,era_dark_footer=false;
SDL_Surface *era_pin=nullptr,*era_recent=nullptr;
bool blades_native[dash::IconCount]{};
std::string blades_icon_paths[dash::IconCount];
SDL_Surface *dashboard_title=nullptr; // dash.xex/FFFE07D1, per-era XDBF image
SDL_Surface *achievement_art[3]={}; // earned, unearned, secret; load_library_art owns these
SDL_Surface *storage_art[12]={}; // memory category/device icons, owned by load_library_art
SDL_Surface *network_art[13]={}; // NetworkMain's ConnectBar, progress and signal art
SDL_Surface *avatar_art=nullptr, *disc_art=nullptr, *shade_pixel=nullptr;
SDL_Surface *metro_symbols[3]{};
SDL_Surface *metro_legend[4]{}; // A B X Y: the Metro dashboards' flat legend buttons (controlp)
SDL_Surface *toast_segments=nullptr;
SDL_Surface *xbox_logo=nullptr, *music_controls[6]{};
SDL_Surface *picture_controls[11]{}, *picture_backdrop=nullptr;
SDL_Surface *video_controls[7]{}; // XmfPadPlayback: Play, Pause, Last, RR, Stop, FF, Next
// Optional owner-imported marketplace prop.  The installer derives/copies it
// locally; clean exports contain no Microsoft asset.
SDL_Surface *theater_popcorn_art=nullptr;
// The 1888 music scene's own pieces (private/xbox/unpacked/music): the order
// buttons (in order, shuffled) and the row glyphs for a song, album, artist
// and genre. Any of them may be missing; the pages draw stand-ins.
SDL_Surface *music_order_art[2]{}, *music_kind_art[7]{};
SDL_Surface *xbox_live_logo=nullptr;
SDL_Surface *blades_star=nullptr,*blades_star_full=nullptr,*blades_gscore=nullptr,*blades_disc=nullptr;
// The System blade's own row icons (dashmain / shared), by name.
std::map<std::string,SDL_Surface *> blades_system_icons; // the owner's Blades gamercard glyphs
SDL_Surface *blades_labels[3][2][5]{}; // Family/state surfaces retain authentic font outlines.
int artwork_build=-1;
void refresh_update_art();
void refresh_picture_background();
void load_promotions();
// The user's classic gamerpics and the gamerscore G (shrdres.xzp). Optional;
// the avatar's head and a drawn G stand in.
std::vector<SDL_Surface *> gamerpics;
int gamerpic=3;
constexpr const char *gamerpic_path="/data/nxe/gamerpic.txt";
SDL_Surface *gamerscore_icon=nullptr;
// The Guide's own art from huduiskin.xex (the 360's HUD skin): the open blade,
// the silver blades beside it and the Ring of Light on the tab. Optional.
SDL_Surface *nxe_panel=nullptr; // NXE 9199 dashcomm/BackgroundPanel.png, the settings scenes' 9-grid
SDL_Surface *blade_dark=nullptr, *blade_grey=nullptr, *ring_of_light=nullptr, *trophy_icon=nullptr;
// The signed-in PS5 user (sceUserService), and what their profile cache holds.
int user_id=-1;
ps5lib::Trophies trophies;
bool profile_picture=false; // gamerpics[0] is the PS5 profile picture
constexpr const char *settings_path="/data/nxe/settings.txt";
constexpr const char *categories_path="/data/nxe/categories.txt";
constexpr const char *trailer_choices_path="/data/nxe/trailers.txt";
constexpr const char *artwork_choices_path="/data/nxe/artwork.txt";
bool retail_background=false;
dash::Model model;
exbox_network::Worker network_worker;
exbox_recorder::Worker recorder_worker;
exbox::StartupJob<exbox::themes::ManifestResult> theme_catalog_job;
std::vector<exbox::themes::CatalogEntry> installed_theme_catalog;
bool theme_catalog_reported=false;
bool running = true;
float position = 0;
int shown_channel = -1;
// Controller state, as Xbox buttons.
std::uint32_t held = 0;
Uint32 flashed[xb::Count]{};
xb::Repeat repeat;
xb::Threshold left_trigger, right_trigger;
xb::StickDpad stick;
float stick_x = 0, stick_y = 0;
std::uint32_t logged_sdl_buttons = 0;
std::string last_input = "Press any button";
struct Trailer { std::string key, folder; int frames=0, fps=15; std::string name; int aspect_num=16, aspect_den=9; };
std::vector<Trailer> trailers;
#ifdef NXE_DIRECT_VIDEO
exbox::video::Transport direct_video;
int direct_trailer_index=-1;
std::string direct_video_path;
Uint64 direct_seen_generation=0;
#endif
int active_trailer=-1, active_frame=-1;
Uint32 trailer_started=0;
SDL_Surface *trailer_image=nullptr;
SDL_Surface *trailer_scaled=nullptr,*video_source_argb=nullptr;
bool scaled_video_opaque=false;
realtime_scale::Workspace video_scale_workspace;
video_output_storage::Backing video_output_backing;
SDL_Surface* video_render_surface(int width,int height,int density){return video_output_backing.create(width,height,density);}
Uint64 trailer_generation=0,scaled_video_generation=0;
FILE *trailer_frames=nullptr;
const std::vector<int64_t>* trailer_offsets=nullptr;
SDL_AudioDeviceID trailer_audio=0;
SDL_AudioSpec trailer_audio_spec{};
Uint8 *trailer_audio_data=nullptr;
Uint32 trailer_audio_size=0;
const char *trailer_root="/data/homebrew/ExBox360/trailers";
// Game Videos' player: paused, or held on the last frame at the end (full
// screen plays once; tiles and the hub loop).
bool trailer_paused=false, trailer_hold_end=false;
Uint32 trailer_paused_ms=0;
// The playing clip's sidecar .srt, by folder.
std::string subtitle_folder;
std::vector<srt::Cue> subtitle_cues;
const char *video_state_path="/data/nxe/videos.txt"; // where each video was left
// The Music Library and the background player (music.hpp). The player is
// mixed into the interface sounds' audio device (sfx::mix).
music::Player music_player;
exbox_music::LibraryActivity music_activity;
std::vector<music::Song> music_songs;
// Music is heard now: trailers stay silent under it, as the 360's did.
bool music_audible() {return music_player.active() && !music_player.paused();}
constexpr const char *recent_path="/data/nxe/recent.txt";
constexpr const char *trophies_path="/data/nxe/trophies.txt";
// Transitions and the moments around them.
int slide_from=-1;          // the channel sliding away, or -1
Uint32 slide_at=0;
float slide_position=0;     // where that channel's panes were
constexpr Uint32 kSlide=280;
constexpr int kSlideDistance=190;
int card_dy=0;              // vertical offset for draw_cards
bool cards_static=false;    // drawing a channel that is leaving: no disc or trailer
bool booting=false;
Uint32 boot_at=0;
constexpr Uint32 kBoot=2700;
bool suspended=false;       // a game was started; the dashboard waits behind it
Uint32 suspended_drawn=0;
Uint32 last_input_at=0;
Uint32 idle_limit=10*60*1000; // the 360 dimmed after ten idle minutes
float dim_level=0;
Uint32 return_at=0;         // fading back in after a launch attempt
Uint32 trophies_checked=0;
SDL_Surface *ring_frames[24]{};
SDL_Surface *glow=nullptr, *kinect_glow=nullptr, *aura=nullptr;
std::array<SDL_Surface*,3> atmosphere_glow{};
// The 360's own notification icons (huduiskin shrdres ico_64x_*). Optional.
SDL_Surface *toast_trophy=nullptr, *toast_downloads=nullptr, *toast_controller=nullptr, *toast_themes=nullptr;
size_t pads_seen=0; // controllers present at startup do not announce themselves
// The hub recoloured for the chosen theme (the green theme uses the originals).
int themed_for=-1;
SDL_Surface *themed_background=nullptr, *themed_cards[5]{}, *themed_icons[dash::IconCount]{};
SDL_Surface *installed_theme_background=nullptr;
SDL_Surface *installed_theme_previous=nullptr;
std::shared_ptr<std::vector<Uint32>> installed_theme_pixels,installed_theme_previous_pixels;
exbox::StartupJob<exbox::themes::PreparedTheme> theme_apply_job;
std::string requested_theme_id;
int requested_theme_slot=0,installed_theme_slot=-1;
std::string failed_theme_id;
int failed_theme_slot=-1;
bool retire_installed_theme=false;
SDL_Surface *installed_theme_preview=nullptr;
std::shared_ptr<std::vector<Uint32>> installed_theme_preview_pixels;
exbox::StartupJob<exbox::themes::PreparedTheme> theme_preview_job;
std::string theme_preview_focus_id,theme_preview_requested_id,theme_preview_published_id;
Uint32 theme_preview_focus_at=0;
// A game or app starting: the fade to black runs first.
struct Launching { dash::Effect effect; std::string title; Uint32 at=0; bool live=false; };
Launching launching;
constexpr Uint32 kLaunchFade=650, kLaunchHold=250;

struct DashboardLogSink {FILE *file=nullptr;bool failure_reported=false;};
DashboardLogSink dashboard_log_sink;
exbox_log::AsyncLog dashboard_log;
bool dashboard_log_sync=false;
std::mutex dashboard_log_format_mutex;
bool dashboard_log_write(void *context,const char *bytes,std::size_t size,bool first,bool last) {
    auto &sink=*static_cast<DashboardLogSink*>(context);
    if(first)sink.file=fopen("/data/nxe/bigapp.log","a");
    bool ok=sink.file!=nullptr;
    if(sink.file && fwrite(bytes,1,size,sink.file)!=size)ok=false;
    if(last && sink.file){if(fclose(sink.file)!=0)ok=false;sink.file=nullptr;}
    if(!ok && !sink.failure_reported){sink.failure_reported=true;fprintf(stderr,"ExBox log writer failed; inspect shutdown log status.\n");}
    return ok;
}
void close_dashboard_log() {
    const auto state=dashboard_log.close();
    static bool reported=false;
    if(!state.ok() && !reported){reported=true;fprintf(stderr,"ExBox log status: accepted=%llu written=%llu failed=%llu rejected=%llu start_failures=%llu\n",
        static_cast<unsigned long long>(state.accepted),static_cast<unsigned long long>(state.written),
        static_cast<unsigned long long>(state.failed),static_cast<unsigned long long>(state.rejected),
        static_cast<unsigned long long>(state.start_failures));}
}
struct DashboardLogSession {
    DashboardLogSession(){if(!dashboard_log.start({&dashboard_log_sink,dashboard_log_write})){dashboard_log_sync=true;fprintf(stderr,"ExBox log worker unavailable; using synchronous fallback.\n");}}
    ~DashboardLogSession(){close_dashboard_log();}
};
void log(const char *message) {
    const std::time_t now=std::time(nullptr);
    std::lock_guard<std::mutex> format_guard(dashboard_log_format_mutex);
    char stamp[32]="";
    if(const std::tm *t=std::localtime(&now))std::strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S ",t);
    if(dashboard_log_sync){
        dashboard_log_write(&dashboard_log_sink,stamp,std::strlen(stamp),true,false);
        dashboard_log_write(&dashboard_log_sink,message,std::strlen(message),false,false);
        dashboard_log_write(&dashboard_log_sink,"\n",1,false,true);
    }else dashboard_log.append(stamp,message);
}
bool local_time_snapshot(std::time_t now,std::tm &result) {
    std::lock_guard<std::mutex> lock(dashboard_log_format_mutex);
    if(const std::tm *local=std::localtime(&now)){result=*local;return true;}
    return false;
}

void log_after_close(const char *message) {
    close_dashboard_log();
    const std::time_t now=std::time(nullptr);
    std::lock_guard<std::mutex> lock(dashboard_log_format_mutex);
    char stamp[32]="";
    if(const std::tm *local=std::localtime(&now))std::strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S ",local);
    dashboard_log_write(&dashboard_log_sink,stamp,std::strlen(stamp),true,false);
    dashboard_log_write(&dashboard_log_sink,message,std::strlen(message),false,false);
    dashboard_log_write(&dashboard_log_sink,"\n",1,false,true);
}
#ifndef NXE_DIAGNOSTICS
#define NXE_DIAGNOSTICS 0
#endif
constexpr bool diagnostic_logging(){return NXE_DIAGNOSTICS!=0;}

bool developer_diagnostics_requested(){
#ifndef __PROSPERO__
    if(const char* value=std::getenv("NXE_DEVELOPER_DIAGNOSTICS"))return std::strcmp(value,"1")==0;
#endif
    char value[4]{};
    FILE* file=std::fopen("/data/nxe/exbox/developer-diagnostics.flag","rb");
    if(!file)return false;
    const std::size_t size=std::fread(value,1,sizeof value,file);
    std::fclose(file);
    return (size==1&&value[0]=='1') || (size==2&&value[0]=='1'&&value[1]=='\n') ||
           (size==3&&value[0]=='1'&&value[1]=='\r'&&value[2]=='\n');
}

bool gpu_dashboard_health_requested(){
#ifndef __PROSPERO__
    if(const char* value=std::getenv("NXE_GPU_DASHBOARD"))return std::strcmp(value,"1")==0;
    return false;
#else
    char value[4]{};
    FILE* file=std::fopen("/data/nxe/exbox/gpu-dashboard.flag","rb");
    if(!file)return false;
    const std::size_t size=std::fread(value,1,sizeof value,file);
    std::fclose(file);
    return (size==1&&value[0]=='1') || (size==2&&value[0]=='1'&&value[1]=='\n') ||
           (size==3&&value[0]=='1'&&value[1]=='\r'&&value[2]=='\n');
#endif
}

std::chrono::steady_clock::time_point startup_epoch;
void startup_mark(const char *phase) {
    char line[160];
    std::snprintf(line,sizeof line,"startup: %s elapsed_ms=%lld",phase,
        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-startup_epoch).count()));
    log(line);
}
// The process's flexible-memory budget in bytes, or 0 when unknown (desktop).
// Homebrew launched by websrv inherits it from FAKE00000's param.json:
// 448 MiB by default, 1 GiB with "kernel": {"flexibleMemorySize": 1073741824}
// (the raise made for Omni; it applies to every app that loader starts).
size_t memory_budget() {
#ifdef __PROSPERO__
    using Query=int (*)(size_t *);
    static Query query=nullptr;
    if (!query) {
        void *h=dlopen("libkernel.sprx",RTLD_LAZY);
        if (!h) h=dlopen("libkernel_sys.sprx",RTLD_LAZY);
        if (h) query=reinterpret_cast<Query>(dlsym(h,"sceKernelConfiguredFlexibleMemorySize"));
    }
    size_t size=0;
    if (query && query(&size)==0) return size;
#endif
    return 0;
}
// Generic icons keep at most 1024 pixels on their long edge. Ads can retain
// original dimensions; both use this count limit and the shared 64 MiB cap.
size_t icon_cache_limit=96;
Uint32 color(unsigned hex) { return SDL_MapRGB(screen->format, (hex >> 16) & 255, (hex >> 8) & 255, hex & 255); }
void rect(int x, int y, int w, int h, unsigned hex) {
    if(x21hook::rectangle(screen,x,y,w,h,hex))return; x21hook::Pause gpu_cpu_reference(x21hook::recording(screen));
    if (w <= 0 || h <= 0) return;
    SDL_Rect r{x,y,w,h}; SDL_FillRect(screen, &r, color(hex));
}
// A translucent fill: a 1x1 pixel stretched with blending.
void shade(int x, int y, int w, int h, unsigned hex, Uint8 alpha) {
    if(x21hook::shade(screen,x,y,w,h,hex,alpha))return; x21hook::Pause gpu_cpu_reference(x21hook::recording(screen));
    if (!shade_pixel || w <= 0 || h <= 0) return;
    SDL_Rect physical;if(render_density::checked_rect({x,y,w,h},render_density::density(screen),physical)&&solid_blend::fill(screen,physical,hex,alpha))return;
    SDL_FillRect(shade_pixel,nullptr,SDL_MapRGBA(shade_pixel->format,(hex>>16)&255,(hex>>8)&255,hex&255,255));
    SDL_SetSurfaceAlphaMod(shade_pixel,alpha);
    SDL_Rect r{x,y,w,h}; SDL_BlitScaled(shade_pixel,nullptr,screen,&r);
}
void physical_rect(int x,int y,int w,int h,unsigned hex) {
    if(w<1||h<1)return;
    SDL_Rect r{x,y,w,h};
    if(x21hook::recording(screen)){x21hook::sync_clip();auto c=x21hook::active->base(r);c.kind=EXBOX_GPU_RECT;c.top=0xff000000u|hex;x21hook::active->push(c);if(!x21hook::audit_cpu())return;}
    render_density::physical_fill(screen,&r,color(hex));
}
void physical_shade(int x,int y,int w,int h,unsigned hex,Uint8 alpha) {
    if(!shade_pixel||w<1||h<1)return;
    if(x21hook::recording(screen)){x21hook::sync_clip();auto c=x21hook::active->base({x,y,w,h});c.kind=EXBOX_GPU_RECT;c.top=(uint32_t(alpha)<<24)|(hex&0xffffff);c.blend=8;c.fill_type=0x53444cu;x21hook::active->push(c);if(!x21hook::audit_cpu())return;}
    x21hook::Pause cpu_reference(x21hook::recording(screen));
    if(solid_blend::fill(screen,{x,y,w,h},hex,alpha))return;
    SDL_FillRect(shade_pixel,nullptr,SDL_MapRGBA(shade_pixel->format,(hex>>16)&255,(hex>>8)&255,hex&255,255));
    SDL_SetSurfaceAlphaMod(shade_pixel,alpha);SDL_Rect r{x,y,w,h};SDL_UpperBlitScaled(shade_pixel,nullptr,screen,&r);
}
void gradient(int x,int y,int w,int h,unsigned top,unsigned bottom) {
    if(x21hook::gradient(screen,x,y,w,h,top,bottom))return; x21hook::Pause gpu_cpu_reference(x21hook::recording(screen));
    const int d=render_density::density(screen);x*=d;y*=d;w*=d;h*=d;
    const int step=d==1?3:1;
    for(int row=0;row<h;row+=step){float t=float(row)/float(std::max(1,h));unsigned c=0;
      for(int shift:{16,8,0})c|=unsigned(((top>>shift)&255)*(1-t)+((bottom>>shift)&255)*t)<<shift;
      physical_rect(x,y+row,w,std::min(step,h-row),c);
    }
}
void outline(int x, int y, int w, int h, int t, unsigned hex) {
    rect(x,y,w,t,hex); rect(x,y+h-t,w,t,hex); rect(x,y,t,h,hex); rect(x+w-t,y,t,h,hex);
}
SDL_Surface *load_png(const std::string &path) {
    SDL_RWops *file=SDL_RWFromFile(sysdata::local_path(path).c_str(),"rb");
    if (!file) return nullptr;
    SDL_Surface *image=IMG_LoadPNG_RW(file);
    SDL_RWclose(file);
    surface_generations::seal(image);return image;
}
// A five-pointed star (64 px), filled gold with a dark rim, or pale and empty.
SDL_Surface *make_rep_star(bool earned) {
    // The Blades gamercard's Rep star (designer capture): a plump star with
    // rounded points and a soft dark rim; earned ones are gold, the rest are
    // the card's own colour seen through a faint embossed star.
    constexpr int N=64;auto *s=render_density::create(N,N,SDL_PIXELFORMAT_ARGB8888);
    if (!s) return nullptr;
    float px[10],py[10];
    for (int i=0;i<10;i++) {const float a=-1.5707963f+i*0.6283185f,r=i%2?14.0f:27.0f;px[i]=32+r*std::cos(a);py[i]=34.5f+r*std::sin(a);}
    auto signed_distance=[&](float x,float y) {
        float best=1e9f;bool in=false;
        for (int i=0,j=9;i<10;j=i++) {
            const float ex=px[i]-px[j],ey=py[i]-py[j],wx=x-px[j],wy=y-py[j];
            const float t=std::clamp((wx*ex+wy*ey)/(ex*ex+ey*ey),0.0f,1.0f);
            best=std::min(best,std::hypot(wx-ex*t,wy-ey*t));
            if ((py[i]>y)!=(py[j]>y) && x<(px[j]-px[i])*(y-py[i])/(py[j]-py[i])+px[i]) in=!in;
        }
        return in?-best:best;
    };
    SDL_LockSurface(s);
    const float density=float(render_density::density(s));
    for (int y=0;y<s->h;y++) for (int x=0;x<s->w;x++) {
        const float d=signed_distance((x+.5f)/density,(y+.5f)/density)-3.0f;          // rounded by 3 px
        const float cover=std::clamp(0.5f-d,0.0f,1.0f);
        Uint32 c=0;
        if (cover>0) {
            const float rim=std::clamp((d+7.0f)/5.0f,0.0f,1.0f);  // a soft dark rim ~6 px wide
            const float t=float(y)/(N*density);
            float r,g,b,a;
            if (earned) {
                const float inner=std::clamp((-d-6.0f)/4.0f,0.0f,1.0f);  // a lit inner bevel
                r=238-18*t;g=214-30*t;b=40;
                r+=11*inner*(1-t);g+=20*inner*(1-t);
                const float k=rim*rim;r=r*(1-k)+104*k;g=g*(1-k)+100*k;b=b*(1-k)+30*k;a=255*(1-0.35f*k);
            } else {
                const float inner=std::clamp(1-std::fabs(-d-6.5f)/1.5f,0.0f,1.0f);  // light emboss line
                r=40;g=110;b=40;a=255*(0.30f+0.32f*rim*rim);
                if (inner>0) {r=r*(1-inner)+200*inner;g=g*(1-inner)+232*inner;b=b*(1-inner)+196*inner;a=a*(1-inner)+255*0.30f*inner;}
            }
            c=Uint32(a*cover)<<24|Uint32(std::clamp(r,0.0f,255.0f))<<16|Uint32(std::clamp(g,0.0f,255.0f))<<8|Uint32(std::clamp(b,0.0f,255.0f));
        }
        reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(s->pixels)+y*s->pitch)[x]=c;
    }
    SDL_UnlockSurface(s);SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_BLEND);return s;
}
// The pale disc behind an unlabelled legend letter (Y / B at the Blades root).
SDL_Surface *make_soft_disc() {
    constexpr int N=64;auto *s=render_density::create(N,N,SDL_PIXELFORMAT_ARGB8888);
    if (!s) return nullptr;
    SDL_LockSurface(s);
    const float density=float(render_density::density(s));
    for (int y=0;y<s->h;y++) for (int x=0;x<s->w;x++) {
        const float d=std::hypot((x+.5f)/density-32,(y+.5f)/density-32),edge=std::clamp(31.f-d,0.f,1.f);
        const Uint32 a=Uint32(edge*(d>28?120:95)),l=Uint32(235-std::max(0.f,d-26)*8);
        reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(s->pixels)+y*s->pitch)[x]=a<<24|l<<16|l<<8|l;
    }
    SDL_UnlockSurface(s);SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_BLEND);return s;
}
SDL_Surface *load_art(const std::string &path) {
    SDL_RWops *file=SDL_RWFromFile(sysdata::local_path(path).c_str(),"rb");
    auto*image=file?IMG_Load_RW(file,1):nullptr;surface_generations::seal(image);return image;
}
struct ScaledArt {
    SDL_Surface *source=nullptr, *image=nullptr;
    SDL_Rect crop{};
    int w=0,h=0;
    Uint32 used=0;
};
std::vector<ScaledArt> scaled_art;
size_t scaled_art_bytes=0;
constexpr size_t kScaledArtBudget=48u<<20;
void clear_scaled_art() {
    for (auto &entry:scaled_art) SDL_FreeSurface(entry.image);
    scaled_art.clear();
    scaled_art_bytes=0;
}
void invalidate_scaled_art(SDL_Surface *source) {
    if (!source) return;
    for (auto it=scaled_art.begin(); it!=scaled_art.end();) {
        if (it->source!=source) { ++it; continue; }
        scaled_art_bytes-=size_t(it->image->pitch)*it->image->h;
        SDL_FreeSurface(it->image);
        it=scaled_art.erase(it);
    }
}

SDL_Surface *resample_art(SDL_Surface *source, const SDL_Rect &crop, int w, int h) {
    if (!source || w<1 || h<1 || crop.w<1 || crop.h<1) return nullptr;
    SDL_Surface *input=SDL_CreateRGBSurfaceWithFormat(0,crop.w,crop.h,32,SDL_PIXELFORMAT_ARGB8888);
    if (!input) return nullptr;
    SDL_BlendMode blend;
    Uint8 alpha=255;
    SDL_GetSurfaceBlendMode(source,&blend);
    SDL_GetSurfaceAlphaMod(source,&alpha);
    SDL_SetSurfaceBlendMode(source,SDL_BLENDMODE_NONE);
    SDL_SetSurfaceAlphaMod(source,255);
    SDL_Rect from=crop;
    SDL_UpperBlit(source,&from,input,nullptr);
    SDL_SetSurfaceBlendMode(source,blend);
    SDL_SetSurfaceAlphaMod(source,alpha);
    if (w==crop.w && h==crop.h) {surface_generations::seal(input);return input;}
    const bool has_alpha=source->format->Amask!=0;
    for (int y=0;has_alpha && y<input->h;y++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(input->pixels)+y*input->pitch);
        for (int x=0;x<input->w;x++) {
            const Uint32 p=row[x], a=p>>24;
            row[x]=(a<<24)|((((p>>16)&255)*a+127)/255<<16)|((((p>>8)&255)*a+127)/255<<8)|(((p&255)*a+127)/255);
        }
    }
    SDL_Surface *out=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    if (out) image_quality::resize_argb(input->pixels,input->w,input->h,input->pitch,out->pixels,w,h,out->pitch);
    SDL_FreeSurface(input);
    if (!out) return nullptr;
    for (int y=0;has_alpha && y<h;y++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(out->pixels)+y*out->pitch);
        for (int x=0;x<w;x++) {
            const Uint32 p=row[x],a=p>>24;
            if (!a) {row[x]=0;continue;}
            auto straight=[&](int shift) {return std::min(255u,((((p>>shift)&255)*255+a/2)/a));};
            row[x]=(a<<24)|(straight(16)<<16)|(straight(8)<<8)|straight(0);
        }
    }
    surface_generations::seal(out);return out;
}
void blit_quality(SDL_Surface *source, const SDL_Rect *crop, SDL_Surface *target, SDL_Rect *dest, bool cache=true, bool narrow_gpu=false) {
    if (!source || !target || !dest || dest->w<1 || dest->h<1) return;
    SDL_Rect physical=render_density::pixel_rect(*dest,render_density::density(target));
    dest=&physical;
    const SDL_Rect from=crop?render_density::pixel_rect(*crop,render_density::density(source)):SDL_Rect{0,0,source->w,source->h};
    if(x21hook::quality(source,from,target,physical,narrow_gpu))return;
    x21hook::Pause cpu_quality_reference(x21hook::recording(target));
    if(!cache && direct_blit::eligible(source,target,from,physical)){SDL_Rect at=physical;if(!pixel_blend::blit(source,nullptr,target,&at))SDL_UpperBlit(source,nullptr,target,&at);return;}
    SDL_Surface *image=nullptr;
    for (auto &entry:scaled_art) if (cache && entry.source==source && entry.w==dest->w && entry.h==dest->h &&
        entry.crop.x==from.x && entry.crop.y==from.y && entry.crop.w==from.w && entry.crop.h==from.h) {
        image=entry.image;entry.used=SDL_GetTicks();break;
    }
    if (!image) {
        Uint8 mr=255,mg=255,mb=255; SDL_GetSurfaceColorMod(source,&mr,&mg,&mb); SDL_SetSurfaceColorMod(source,255,255,255);
        image=resample_art(source,from,dest->w,dest->h);
        SDL_SetSurfaceColorMod(source,mr,mg,mb);
        if (!image) return;
        const size_t bytes=size_t(image->pitch)*image->h;
        if (cache && bytes<=kScaledArtBudget) {
            while (!scaled_art.empty() && scaled_art_bytes+bytes>kScaledArtBudget) {
                auto old=std::min_element(scaled_art.begin(),scaled_art.end(),[](const ScaledArt &a,const ScaledArt &b){return a.used<b.used;});
                scaled_art_bytes-=size_t(old->image->pitch)*old->image->h;
                SDL_FreeSurface(old->image);scaled_art.erase(old);
            }
            scaled_art.push_back({source,image,from,dest->w,dest->h,SDL_GetTicks()});
            scaled_art_bytes+=bytes;
        } else cache=false;
    }
    Uint8 alpha=255; SDL_BlendMode blend;
    SDL_GetSurfaceAlphaMod(source,&alpha);SDL_GetSurfaceBlendMode(source,&blend);
    SDL_SetSurfaceAlphaMod(image,alpha);SDL_SetSurfaceBlendMode(image,blend);
    Uint8 cr=255,cg=255,cb=255; SDL_GetSurfaceColorMod(source,&cr,&cg,&cb); SDL_SetSurfaceColorMod(image,cr,cg,cb);
    SDL_Rect at=*dest;
    x21hook::upper_blit(image,nullptr,target,&at);
    if (!cache) SDL_FreeSurface(image);
}
// ---- The owner's avatar -------------------------------------------------
// Rendered on the PC from a rigged GLB (tools/render_avatar.py; 360sona.com
// exports one) into transparent PNG frames packed like the trailers ("NXEF",
// count, then length-prefixed PNGs). One frame is decoded at a time.
struct SpritePack {
    FILE *file=nullptr;
    std::vector<long> offsets;
    int shown=-1;
    SDL_Surface *image=nullptr;
    bool open(const std::string &path) {
        close();
        file=fopen(path.c_str(),"rb");
        char magic[4]; Uint32 count=0;
        if (!file || fread(magic,1,4,file)!=4 || memcmp(magic,"NXEF",4)!=0 || fread(&count,4,1,file)!=1 || count>4000) {close(); return false;}
        for (Uint32 i=0;i<count;i++) {
            const long at=ftell(file);
            Uint32 bytes=0;
            if (fread(&bytes,4,1,file)!=1 || bytes==0 || bytes>20000000u || fseek(file,long(bytes),SEEK_CUR)!=0) {close(); return false;}
            offsets.push_back(at);
        }
        return true;
    }
    int count() const {return int(offsets.size());}
    SDL_Surface *frame(int i) {
        if (!file || offsets.empty()) return nullptr;
        i=((i%count())+count())%count();
        if (i==shown) return image;
        if (image) {SDL_FreeSurface(image); image=nullptr;}
        shown=i;
        Uint32 bytes=0;
        if (fseek(file,offsets[size_t(i)],SEEK_SET)!=0 || fread(&bytes,4,1,file)!=1) return nullptr;
        std::vector<unsigned char> data(bytes);
        if (fread(data.data(),1,bytes,file)!=bytes) return nullptr;
        if (SDL_RWops *rw=SDL_RWFromConstMem(data.data(),int(bytes))) {
            image=IMG_LoadPNG_RW(rw);
            SDL_RWclose(rw);
            if (image) SDL_SetSurfaceBlendMode(image,SDL_BLENDMODE_BLEND);
        }
        return image;
    }
    void close() {
        if (image) SDL_FreeSurface(image);
        image=nullptr; shown=-1;
        if (file) fclose(file);
        file=nullptr;
        offsets.clear();
    }
};
#include "runtime/avatar_sprites.inc.hpp"
// ---- The avatar editor's avatar ------------------------------------------
// Built from the kit (avatar_kit.hpp; Quaternius's CC0 parts) and drawn live,
// so changing it on the console shows at once. The look is saved in
// /data/nxe/avatar_look.txt. An imported (360sona) avatar can be chosen instead.
#include "runtime/avatar_scene.inc.hpp"
void prepare_background() {
    background=render_density::create(W,H,screen->format->format);
    if (!background) return;
    if (SDL_Surface *art=load_png(std::string(root)+"private_art/background.png")) {
        SDL_Rect destination{0,0,W,H};
        blit_quality(art,nullptr,background,&destination,false);
        SDL_FreeSurface(art);
        retail_background=true;
        surface_generations::seal(background);return;
    }
    if (SDL_MUSTLOCK(background)) SDL_LockSurface(background);
    for (int y=0;y<background->h;y++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(background->pixels)+y*background->pitch);
        for (int x=0;x<background->w;x++) {
            float horizontal=float(x)/float(background->w-1);
            float vertical=float(y)/float(y<615*render_density::factor?614*render_density::factor:(H-615)*render_density::factor);
            if (y>=615*render_density::factor) vertical=float(y-615*render_density::factor)/float((H-615)*render_density::factor);
            unsigned tl=y<615*render_density::factor?0x59951c:0x53616a;
            unsigned tr=y<615*render_density::factor?0xe9eee4:0x667580;
            unsigned bl=y<615*render_density::factor?0xb8d956:0xd0d7dc;
            unsigned br=y<615*render_density::factor?0xf1f3e9:0xe5e9e8;
            auto mix=[&](int shift) {
                float top=float((tl>>shift)&255)*(1-horizontal)+float((tr>>shift)&255)*horizontal;
                float bottom=float((bl>>shift)&255)*(1-horizontal)+float((br>>shift)&255)*horizontal;
                return Uint8(top*(1-vertical)+bottom*vertical);
            };
            row[x]=SDL_MapRGB(background->format,mix(16),mix(8),mix(0));
        }
    }
    if (SDL_MUSTLOCK(background)) SDL_UnlockSurface(background);
    surface_generations::seal(background);
}
void ring(int cx,int cy,int radius,int thickness,unsigned hex) {
    if(x21hook::recording(screen)){ExboxGpuCommand c{};const auto r=screen->clip_rect;if(circle_command(c,false,cx,cy,radius,thickness,hex,{r.x,r.y,r.w,r.h},render_density::density(screen)))x21hook::active->push(c);else x21hook::active->packet.unsupported.push_back("circle_bounds");if(!x21hook::audit_cpu())return;}
    x21hook::Pause cpu_reference(x21hook::recording(screen));
    const int d=render_density::density(screen);cx*=d;cy*=d;radius*=d;thickness*=d;
    for(int yy=-radius;yy<=radius;yy++)for(int xx=-radius;xx<=radius;xx++){
      float distance=std::sqrt(float(xx*xx+yy*yy));
      float coverage=std::clamp(float(radius)+.5f-distance,0.f,1.f)*std::clamp(distance-float(radius-thickness)+.5f,0.f,1.f);
      if(coverage>=.999f)physical_rect(cx+xx,cy+yy,1,1,hex);
      else if(coverage>0)physical_shade(cx+xx,cy+yy,1,1,hex,Uint8(255*coverage));
    }
}
void disc(int cx,int cy,int radius,unsigned hex) {
    if(x21hook::recording(screen)){ExboxGpuCommand c{};const auto r=screen->clip_rect;if(circle_command(c,true,cx,cy,radius,0,hex,{r.x,r.y,r.w,r.h},render_density::density(screen)))x21hook::active->push(c);else x21hook::active->packet.unsupported.push_back("circle_bounds");if(!x21hook::audit_cpu())return;}
    x21hook::Pause cpu_reference(x21hook::recording(screen));
    const int d=render_density::density(screen);cx*=d;cy*=d;radius*=d;
    for(int yy=-radius;yy<=radius;yy++){
      float span=std::sqrt(std::max(0.f,float(radius*radius-yy*yy)));int solid=int(span);
      physical_rect(cx-solid,cy+yy,solid*2+1,1,hex);Uint8 alpha=Uint8(255*(span-solid));
      if(alpha){physical_shade(cx-solid-1,cy+yy,1,1,hex,alpha);physical_shade(cx+solid+1,cy+yy,1,1,hex,alpha);}
    }
}
int text_width(const std::string &str, TTF_Font *font) {
    int w=0,h=0;
    if (font && !str.empty()) TTF_SizeUTF8(font,str.c_str(),&w,&h);
    return w;
}
// Shorten with an ellipsis so a long title stays inside its card.
std::string fit(const std::string &str, TTF_Font *font, int max_width) {
    if (text_width(str,font)<=max_width) return str;
    std::string cut=str;
    while (!cut.empty() && text_width(cut+"...",font)>max_width) cut.pop_back();
    return cut+"...";
}
Uint8 text_alpha=255; // fades text and prompts while the Guide animates
int text(const std::string &str, int x, int y, TTF_Font *font, SDL_Color color) {
    if (!font || str.empty()) return 0;
    auto cached=text_cache::acquire(font,str,color);
    SDL_Surface *glyph=cached.image;
    if (!glyph) return 0;
    SDL_SetSurfaceAlphaMod(glyph,text_alpha);
    SDL_Rect dest{x,y,0,0}; SDL_BlitSurface(glyph,nullptr,screen,&dest);
    return render_density::logical_w(glyph);
}
void wrapped_text(const std::string &message, int x, int y, int width, int max_lines,
                  TTF_Font *font, SDL_Color color) {
    std::string line, word;
    int rows=0;
    auto place=[&](const std::string &value) {
        if (value.empty() || rows>=max_lines) return;
        text(fit(value,font,width),x,y+rows*42,font,color);
        ++rows;
    };
    for (std::size_t i=0;i<=message.size();++i) {
        const char ch=i<message.size()?message[i]:' ';
        if (ch!=' ' && ch!='\n') {word+=ch;continue;}
        if (!word.empty()) {
            const std::string candidate=line.empty()?word:line+" "+word;
            if (!line.empty() && text_width(candidate,font)>width) {place(line);line=word;}
            else line=candidate;
            word.clear();
        }
        if (ch=='\n') {place(line);line.clear();}
    }
    place(line);
}
#include "ui/blades_typography.inc.hpp"

SDL_Surface *button_image(xb::Button button) {
    if (button>=xb::Count) return nullptr;
    if (model.button_style==0 && model.era>=1 && button<=xb::Y) {
        const int family=model.era==1?0:model.era==2?1:model.metro_v1()?2:3;
        if (SDL_Surface *art=era_glyphs[family][button]) return art;
    }
    if ((model.button_style==0 || model.button_style==1) && (model.era==2 || model.era==3)) {
        const int family=model.era==2?0:model.metro_v1()?1:2;
        if (SDL_Surface *art=era_controller_glyphs[family][button]) return art;
    }
    if (model.button_style==1 && retail_glyphs[button]) return retail_glyphs[button];
    if (model.button_style==2 && simple_glyphs[button]) return simple_glyphs[button];
    return glyphs[button];
}
void glyph(xb::Button button, int x, int y, int size) {
    SDL_Surface *art=button_image(button);
    if (!art) return;
    SDL_Rect destination{x,y,size,size};
    // The Kinect/Metro bumper, trigger and D-pad art is white (it was drawn for
    // the Guide's dark panels); on the light dashboard legends it gets the same
    // soft drop shadow as the legend text beside it so it stays readable.
    bool era_art=false;
    for (auto &family:era_controller_glyphs) if (family[button]==art) era_art=true;
    if (era_art) {
        const int lift=std::max(1,size/24);
        SDL_Rect under{x+lift,y+lift,size,size};
        SDL_SetSurfaceColorMod(art,0,0,0);
        SDL_SetSurfaceAlphaMod(art,static_cast<Uint8>(text_alpha*0x70/255));
        blit_quality(art,nullptr,screen,&under);
        SDL_SetSurfaceColorMod(art,255,255,255);
    }
    SDL_SetSurfaceAlphaMod(art,text_alpha);
    blit_quality(art,nullptr,screen,&destination);
    SDL_SetSurfaceAlphaMod(art,255);
}
// The Guide's blade names run down their tabs, turned 90 degrees clockwise so
// they read top to bottom, as NXE drew them.
SDL_Surface *rotated_surface(SDL_Surface *flat) {
    if (!flat) return nullptr;
    const int raster_density=render_density::density(flat);
    SDL_Surface *src=SDL_ConvertSurfaceFormat(flat,SDL_PIXELFORMAT_ARGB8888,0);
    SDL_FreeSurface(flat);
    if (!src) return nullptr;
    SDL_Surface *out=SDL_CreateRGBSurfaceWithFormat(0,src->h,src->w,32,SDL_PIXELFORMAT_ARGB8888);
    if (out) {
        SDL_LockSurface(src); SDL_LockSurface(out);
        for (int y=0;y<src->h;y++) for (int x=0;x<src->w;x++) {
            Uint32 p=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(src->pixels)+y*src->pitch)[x];
            reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(out->pixels)+x*out->pitch)[src->h-1-y]=p;
        }
        SDL_UnlockSurface(out); SDL_UnlockSurface(src);
        SDL_SetSurfaceBlendMode(out,SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(src);
    return render_density::tag(out,raster_density);
}
SDL_Surface *rotated_label(TTF_Font *font,const std::string &label,SDL_Color ink) {
    return rotated_surface(font && !label.empty()?TTF_RenderUTF8_Blended(font,label.c_str(),ink):nullptr);
}
// A vertical gradient with rounded corners.
void rounded(int x, int y, int w, int h, int r, unsigned top, unsigned bottom) {
    if(x21hook::rounded(screen,x,y,w,h,r,top,bottom))return; x21hook::Pause gpu_cpu_reference(x21hook::recording(screen));
    const int d=render_density::density(screen);x*=d;y*=d;w*=d;h*=d;r*=d;
    r=std::min(r,std::min(w,h)/2);
    for (int row=0; row<h; ++row) {
        int inset=0;
        int dy=row<r?r-row:(row>=h-r?row-(h-r-1):0);
        if (dy>0) inset=r-int(std::sqrt(float(r*r-(dy-0.5f)*(dy-0.5f))));
        float t=float(row)/float(std::max(1,h-1));
        unsigned c=0;
        for (int shift:{16,8,0}) c|=unsigned(((top>>shift)&255)*(1-t)+((bottom>>shift)&255)*t)<<shift;
        physical_rect(x+inset,y+row,w-2*inset,1,c);
    }
}
// Easing for every transition: quick start, soft landing.
float ease_out(float t) {
    t=std::clamp(t,0.0f,1.0f);
    return 1-(1-t)*(1-t)*(1-t);
}
float progress(Uint32 now, Uint32 since, Uint32 span) {
    return span?std::clamp(float(Sint32(now-since))/float(span),0.0f,1.0f):1.0f;
}
// An off-screen layer: draw anything into it as usual, then put it on screen
// faded and scaled about the centre (Details, the Controller Map, the old
// channel sliding away). The Guide keeps its own layer.
SDL_Surface *fx_layer=nullptr, *fx_target=nullptr;
x21layers::Scope fx_gpu_layer;
bool begin_layer() {
    if (fx_target) return false;
    if (!fx_layer) {
        fx_layer=render_density::create(W,H,SDL_PIXELFORMAT_ARGB8888);
        if (fx_layer) SDL_SetSurfaceBlendMode(fx_layer,SDL_BLENDMODE_BLEND);
    }
    if (!fx_layer) return false;
    const bool gpu_layer=fx_gpu_layer.begin(screen,fx_layer,{0,0,W,H});
    if(!gpu_layer&&x21hook::recording(screen))x21hook::active->packet.unsupported.push_back("effect_layer_unavailable");
    if(!gpu_layer||x21hook::audit_cpu()) {x21hook::Pause audit(gpu_layer);SDL_FillRect(fx_layer,nullptr,0);}
    fx_target=screen;
    screen=fx_layer;
    return true;
}
void end_layer(float alpha, float scale=1) {
    if (!fx_target) return;
    screen=fx_target;
    fx_target=nullptr;
    if(fx_gpu_layer.active()){
        SDL_Rect to=scale>=0.999f?SDL_Rect{0,0,W,H}:SDL_Rect{int(W*(1-scale)/2),int(H*(1-scale)/2),int(W*scale),int(H*scale)};
        fx_gpu_layer.end(to,alpha);if(!x21hook::audit_cpu())return;
    }
    x21hook::Pause audit_layer(x21hook::recording(screen));
    SDL_SetSurfaceAlphaMod(fx_layer,Uint8(255*std::clamp(alpha,0.0f,1.0f)));
    if (scale>=0.999f) {SDL_BlitSurface(fx_layer,nullptr,screen,nullptr); return;}
    SDL_Rect to{int(W*(1-scale)/2),int(H*(1-scale)/2),int(W*scale),int(H*scale)};
    SDL_BlitScaled(fx_layer,nullptr,screen,&to);
}
// Something that fades in and out with a flag, continuing from where it is
// when the flag flips half way.
struct Fade {
    bool on=false;
    Uint32 at=0;
    Uint32 in=200, out=160;
    float value(Uint32 now) const {
        return on?progress(now,at,in):1-progress(now,at,out);
    }
    void track(bool want, Uint32 now) {
        if (want==on) return;
        const float v=value(now);
        on=want;
        at=now-Uint32(want?v*float(in):(1-v)*float(out));
    }
};
Fade installed_theme_fade{false,0,300,300};
// Themes recolour the hub as one linear colour transform (fixed point 1/1024).
struct Tint { int m[9]; };
Tint theme_tint(int) {
    return {{1024,0,0,0,1024,0,0,0,1024}};
}
unsigned tint_rgb(unsigned rgb, const Tint &t) {
    const int r=int((rgb>>16)&255), g=int((rgb>>8)&255), b=int(rgb&255);
    auto ch=[&](int row) {return std::clamp((t.m[row*3]*r+t.m[row*3+1]*g+t.m[row*3+2]*b)>>10,0,255);};
    return unsigned(ch(0))<<16|unsigned(ch(1))<<8|unsigned(ch(2));
}
// A recoloured copy of an image (alpha kept), or null for the green theme.
SDL_Surface *tinted_copy(SDL_Surface *src, const Tint &t) {
    if (!src) return nullptr;
    SDL_Surface *out=SDL_ConvertSurfaceFormat(src,SDL_PIXELFORMAT_ARGB8888,0);
    if (!out) return nullptr;
    SDL_LockSurface(out);
    for (int y=0;y<out->h;y++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(out->pixels)+y*out->pitch);
        for (int x=0;x<out->w;x++) row[x]=(row[x]&0xff000000u)|tint_rgb(row[x]&0xffffffu,t);
    }
    SDL_UnlockSurface(out);
    return render_density::tag(out,render_density::density(src));
}
SDL_Surface *cool_blades_copy(SDL_Surface *src) {
    if (!src) return nullptr;
    SDL_Surface *out=SDL_ConvertSurfaceFormat(src,SDL_PIXELFORMAT_ARGB8888,0);
    if (!out) return nullptr;
    SDL_LockSurface(out);
    for (int y=0;y<out->h;y++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(out->pixels)+y*out->pitch);
        for (int x=0;x<out->w;x++) {
            const Uint32 pixel=row[x];
            const int r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
            const int green=std::max(0,g-b);
            row[x]=(pixel&0xff000000u)|
                (Uint32(std::clamp(r-green/4,0,255))<<16)|
                (Uint32(std::clamp(g-green/8,0,255))<<8)|
                Uint32(std::clamp(b+green*13/12,0,255));
        }
    }
    SDL_UnlockSurface(out);
    return render_density::tag(out,render_density::density(src));
}
// The Ring of Light turned to each of 24 angles, for the loading spinner.
SDL_Surface *rotated(SDL_Surface *src, float degrees, int side) {
    SDL_Surface *in=src?SDL_ConvertSurfaceFormat(src,SDL_PIXELFORMAT_ARGB8888,0):nullptr;
    if (!in) return nullptr;
    SDL_Surface *out=render_density::create(side,side,SDL_PIXELFORMAT_ARGB8888);
    if (out) {
        SDL_LockSurface(in); SDL_LockSurface(out);
        const float a=degrees*3.14159265f/180.0f, c=std::cos(a), sn=std::sin(a);
        const float scale=float(std::max(in->w,in->h))/float(out->w), half=out->w/2.0f;
        for (int y=0;y<out->h;y++) {
            auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(out->pixels)+y*out->pitch);
            for (int x=0;x<out->w;x++) {
                const float dx=(x+0.5f-half)*scale, dy=(y+0.5f-half)*scale;
                const int sx=int(in->w/2.0f+dx*c+dy*sn), sy=int(in->h/2.0f-dx*sn+dy*c);
                row[x]=(sx>=0 && sy>=0 && sx<in->w && sy<in->h)?
                    reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(in->pixels)+sy*in->pitch)[sx]:0;
            }
        }
        SDL_UnlockSurface(out); SDL_UnlockSurface(in);
        SDL_SetSurfaceBlendMode(out,SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(in);
    return out;
}
// A soft round light: white in the middle, clear at the edge.
SDL_Surface *make_glow(int side, unsigned rgb=0xffffff) {
    SDL_Surface *out=render_density::create(side,side,SDL_PIXELFORMAT_ARGB8888);
    if (!out) return nullptr;
    SDL_LockSurface(out);
    for (int y=0;y<out->h;y++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(out->pixels)+y*out->pitch);
        for (int x=0;x<out->w;x++) {
            const float dx=(x+0.5f)/out->w*2-1, dy=(y+0.5f)/out->h*2-1;
            const float d=std::min(1.0f,std::sqrt(dx*dx+dy*dy));
            const float a=(1-d)*(1-d);
            row[x]=(Uint32(a*255)<<24)|rgb;
        }
    }
    SDL_UnlockSurface(out);
    SDL_SetSurfaceBlendMode(out,SDL_BLENDMODE_BLEND);
    return out;
}
// The source icon set has no large Avatar/Friends/Messages glyphs. Draw these
// in its pearly white, lime and dark-outline style instead of reusing the
// controller or disc for unrelated dashboard actions.
void icon_ellipse(int cx,int cy,int rx,int ry,unsigned top,unsigned bottom) {
    const int density=render_density::density(screen);
    if (density>1) {
        for(int py=(cy-ry)*density;py<(cy+ry+1)*density;++py) {
            const float dy=(py+0.5f)/density-0.5f-cy;
            const float t=std::clamp((dy+ry)/float(std::max(1,2*ry)),0.0f,1.0f);
            const float half=rx*std::sqrt(std::max(0.0f,1.0f-dy*dy/float(ry*ry)));
            const auto ch=[&](int shift){return unsigned(((top>>shift)&255)*(1-t)+((bottom>>shift)&255)*t);};
            const int left=int(std::ceil((cx-half)*density-0.5f));
            const int right=int(std::ceil((cx+half+1)*density-0.5f));
            physical_rect(left,py,right-left,1,(ch(16)<<16)|(ch(8)<<8)|ch(0));
        }
        return;
    }

    for (int dy=-ry;dy<=ry;dy+=2) {
        const float t=float(dy+ry)/float(std::max(1,2*ry));
        const int half=int(rx*std::sqrt(std::max(0.0f,1.0f-float(dy*dy)/float(ry*ry))));
        const auto ch=[&](int shift) {return unsigned(float((top>>shift)&255)*(1-t)+float((bottom>>shift)&255)*t);};
        rect(cx-half,cy+dy,half*2+1,2,(ch(16)<<16)|(ch(8)<<8)|ch(0));
    }
}
void icon_line(int x1,int y1,int x2,int y2,unsigned color) {
    const int density=render_density::density(screen);
    if(density>1) {
        const int steps=std::max(std::abs(x2-x1),std::abs(y2-y1))*density;
        for(int i=0;i<=steps;++i)physical_rect(x1*density+(x2-x1)*density*i/std::max(1,steps),
            y1*density+(y2-y1)*density*i/std::max(1,steps),3*density,3*density,color);
        return;
    }

    const int steps=std::max(std::abs(x2-x1),std::abs(y2-y1));
    for (int i=0;i<=steps;i++) rect(x1+(x2-x1)*i/std::max(1,steps),
                                y1+(y2-y1)*i/std::max(1,steps),3,3,color);
}
SDL_Surface *make_social_icon(int type) {
    SDL_Surface *art=render_density::create(256,277,SDL_PIXELFORMAT_ARGB8888);
    if (!art) return nullptr;
    SDL_FillRect(art,nullptr,SDL_MapRGBA(art->format,0,0,0,0));
    SDL_Surface *before=screen;
    screen=art;
    if (type==dash::IconMessages) {
        rounded(23,54,210,128,16,0x263d08,0x344b0c);
        rounded(30,55,196,118,12,0xf5faf0,0xa7d345);
        icon_line(36,65,128,132,0x5a861d);
        icon_line(128,132,219,65,0x5a861d);
        icon_line(39,164,101,113,0xd6eda9);
        icon_line(216,164,155,113,0xd6eda9);
    } else {
        const auto person=[&](int cx,int cy,int scale) {
            icon_ellipse(cx+3,cy+75,scale+8,scale*3/5+8,0x263d08,0x314b0b);
            icon_ellipse(cx,cy+72,scale+3,scale*3/5+1,0xf5faf2,0xa4cf3a);
            icon_ellipse(cx+2,cy+4,scale/2+6,scale/2+9,0x263d08,0x314b0b);
            icon_ellipse(cx,cy,scale/2+3,scale/2+6,0xffffff,0xc7df87);
        };
        if (type==dash::IconFriends) {person(86,68,57);person(170,68,57);}
        else person(128,67,78);
    }
    screen=before;
    // The original six category glyphs include a faint reflection below the
    // solid icon; match that material across the new three glyphs.
    SDL_LockSurface(art);
    const int density=render_density::density(art);
    for (int y=190*density;y<art->h;y++) {
        const int sy=184*density-(y-190*density)*94/87;
        auto *dst=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(art->pixels)+y*art->pitch);
        auto *src=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(art->pixels)+sy*art->pitch);
        const int strength=(277*density-y)*105/(87*density);
        for (int x=0;x<art->w;x++) {
            const Uint32 pixel=src[x];
            dst[x]=(Uint32(((pixel>>24)&255)*strength/255)<<24)|(pixel&0xffffffu);
        }
    }
    SDL_UnlockSurface(art);
    SDL_SetSurfaceBlendMode(art,SDL_BLENDMODE_BLEND);
    return art;
}
std::string game_key(std::string value) {
    std::string result;
    bool gap=false;
    for (unsigned char c:value) {
        if (std::isalnum(c)) {
            if (gap && !result.empty()) result+=' ';
            result+=char(std::tolower(c));
            gap=false;
        } else gap=true;
    }
    static const char *suffixes[]={" xbox series x"," xbox series s"," xbox 360"," xbox one"," ps5"," ps4"," ps3"," xbox"," pc"};
    for (const char *suffix:suffixes) {
        size_t n=strlen(suffix);
        if (result.size()>n && result.compare(result.size()-n,n,suffix)==0) {
            result.resize(result.size()-n);
            break;
        }
    }
    return result;
}
#include "runtime/trailer_player.inc.hpp"
#include "ui/nxe_reflection.inc.hpp"
#include "runtime/custom_scenes.inc.hpp"
bool gpu_home_reflect();
void reflect_cards() {
    if(gpu_home_reflect())return;
    // The NXE floor mirrors the lower edges of the panes, fading toward the viewer.
#ifndef __PROSPERO__
    nxe_reflection::capture_before();
#endif
    if (SDL_MUSTLOCK(screen) && SDL_LockSurface(screen)) return;
    const bool lock_figure=nxe_reflection::figure && SDL_MUSTLOCK(nxe_reflection::figure);
    if (lock_figure && SDL_LockSurface(nxe_reflection::figure)) nxe_reflection::figure=nullptr;
    const int density=render_density::density(screen);
    const float figure_half_pixel=0.5f/density;
    for (int y=852*density; y<999*density; ++y) {
        int source_y=849*density-int((y-852*density)*1.12f);
        if (source_y<0) break;
        float opacity=0.29f*(1.0f-float(y-852*density)/(147.0f*density));
        auto *source=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(screen->pixels)+source_y*screen->pitch);
        auto *target=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(screen->pixels)+y*screen->pitch);
        if(solid_blend::format_ok(screen)) {
            std::array<float,3840> coverage{};const int left=130*density,right=1800*density;
            for(size_t i=0;i<nxe_reflection::pane_count;++i){const auto& pane=nxe_reflection::panes[i];if(source_y>=pane.y*density&&source_y<(pane.y+pane.h)*density){const int lo=std::max(left,pane.x*density),hi=std::min(right,(pane.x+pane.w)*density);if(hi>lo)std::fill(coverage.begin()+lo,coverage.begin()+hi,1.f);}}
            if(nxe_reflection::figure){const auto& box=nxe_reflection::figure_bounds;const int lo=std::max(left,box.x*density),hi=std::min(right,(box.x+box.w)*density);for(int x=lo;x<hi;++x)if(coverage[size_t(x)]==0)coverage[size_t(x)]=nxe_reflection::figure_coverage_at(float(x)/density,float(source_y)/density,figure_half_pixel);}
            reflection_blend::row(source+left,target+left,coverage.data()+left,right-left,opacity,screen->format->Amask);
            continue;
        }
        for (int x=130*density; x<1800*density; ++x) {
            const float coverage=nxe_reflection::coverage_at(float(x)/density,float(source_y)/density);
            if (coverage<=0) continue;
            const float masked_opacity=opacity*coverage;
            Uint8 sr,sg,sb,dr,dg,db;
            if(solid_blend::format_ok(screen)) {
                const Uint32 src=source[x],dst=target[x];Uint32 out=screen->format->Amask;
                for(int shift:{0,8,16})out|=Uint32(Uint8(((dst>>shift)&255)*(1-masked_opacity)+((src>>shift)&255)*masked_opacity))<<shift;
                target[x]=out;
            } else {
                const int bytes=screen->format->BytesPerPixel;Uint32 src=0,dst=0;
                const auto* from=reinterpret_cast<const Uint8*>(source)+x*bytes;auto* to=reinterpret_cast<Uint8*>(target)+x*bytes;
                std::memcpy(&src,from,size_t(bytes));std::memcpy(&dst,to,size_t(bytes));
                SDL_GetRGB(src,screen->format,&sr,&sg,&sb);SDL_GetRGB(dst,screen->format,&dr,&dg,&db);
                const Uint32 out=SDL_MapRGB(screen->format,Uint8(dr*(1-masked_opacity)+sr*masked_opacity),Uint8(dg*(1-masked_opacity)+sg*masked_opacity),Uint8(db*(1-masked_opacity)+sb*masked_opacity));
                std::memcpy(to,&out,size_t(bytes));
            }
        }
    }
    if (lock_figure && nxe_reflection::figure) SDL_UnlockSurface(nxe_reflection::figure);
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
#ifndef __PROSPERO__
    nxe_reflection::capture_after();
#endif
}
void draw_hints(int y, SDL_Color ink) {
    if (model.viewer==dash::Viewer::Pictures && model.viewer_full && !model.guide && !model.info && !model.details) {
        int x=95;
        for (const auto &hint:model.hints()) {
            glyph(hint.button,x,1015,32);x+=41;
            x+=text(hint.label,x,1017,small,{235,235,235,255})+25;
        }
        return;
    }
    if (model.era==0 && model.viewer==dash::Viewer::None && !model.info && !model.details && !model.controller_map) {
        int active=0;while (active<4 && dash::kBladesChannels[active]!=model.channel) active++;
        const auto at=era_chrome::layout(active,model.dashboard_build<5759);
        const auto hints=model.hints();
        auto label=[&](xb::Button button)->std::string {
            for (const auto &hint:hints) if (hint.button==button) return hint.label;
            return {};
        };
        // Original legend geometry: Y/B above X/A; left glyphs precede their
        // labels, right glyphs follow them. Missing actions retain dim slots.
        auto prompt=[&](xb::Button button,int cx,int top,bool right) {
            std::string name=label(button);
            // As the original root legend: Y and B sit unlabelled above X and A,
            // and A reads Select (their actions still work).
            if (button==xb::Y || button==xb::B) name.clear();
            if (button==xb::A && !name.empty()) name="Select";
            const Uint8 saved=text_alpha;
            const bool unlabelled=button==xb::Y || button==xb::B;
            if (unlabelled && blades_disc) {
                // The root's Y / B: a pale disc with a dark letter, as the original.
                SDL_Rect disc{cx-21,top,42,42};SDL_BlitScaled(blades_disc,nullptr,screen,&disc);
                TTF_Font *font=regular?regular:era_text;
                const char *letter=button==xb::Y?"Y":"B";
                blades_type::draw(letter,cx-blades_type::width(letter,font)/2,top+2,font,{40,52,40,225});
            } else {
                if (name.empty()) text_alpha=Uint8(saved*0.30f);
                glyph(button,cx-22,top,44);
                text_alpha=saved;
            }
            if (!name.empty()) {
                TTF_Font *font=blades_caption?blades_caption:era_text;
                blades_type::draw(name,right?cx-35-blades_type::width(name,font):cx+32,top+1,font,ink);
            }
        };
        prompt(xb::X,at.prompt_left,952,false);
        prompt(xb::Y,at.prompt_left+34,907,false);
        prompt(xb::A,at.prompt_right,952,true);
        prompt(xb::B,at.prompt_right-34,907,true);
        return;
    }
    if ((model.era==1 || model.era==2) && !model.guide) {
        // NXE legend (designer captures): 38 px glyphs at y 957, light labels
        // with a soft shadow on every theme; the Guide is the lone orb at the
        // bottom right in its ripple rings, not a labelled prompt.
        static SDL_Surface *ripple=nullptr;
        if (!ripple && (ripple=render_density::create(420,150,SDL_PIXELFORMAT_ARGB8888))) {
            SDL_FillRect(ripple,nullptr,0);
            const float density=float(render_density::density(ripple));
            for (int py=0;py<ripple->h;py++) for (int px=0;px<ripple->w;px++) {
                const float dx=((px+0.5f)/density-0.5f-210)/1.0f,dy=((py+0.5f)/density-0.5f-75)/0.3f,d=std::sqrt(dx*dx+dy*dy);
                float a=0;
                for (int ring=1;ring<=6;ring++) a=std::max(a,1-std::fabs(d-ring*34.0f)/1.6f);
                a*=std::clamp(1-d/215,0.0f,1.0f)*0.38f;
                if (a>0) era_chrome::pixel(ripple,px,py,0xffffff,a);
            }
            SDL_SetSurfaceBlendMode(ripple,SDL_BLENDMODE_BLEND);
        }
        // 2010 (Kinect era) capture: 32 px glyphs from x 240, same baseline.
        const bool kinect=model.era==2;
        int x=kinect?240:148;
        const int size=kinect?33:38;
        bool guide_orb=false;
        for (const auto &hint:model.hints()) {
            if (hint.button==xb::Guide) {guide_orb=true;continue;}
            if (!custom_scene::draw_legend(hint.button,x,957+(38-size)/2,size))
                glyph(hint.button,x,957+(38-size)/2,size);
            x+=size+9;
            const Uint8 saved=text_alpha;
            text_alpha=Uint8(saved*0.55f);text(hint.label,x+1,963,regular,{20,24,26,255});text_alpha=saved;
            x+=text(hint.label,x,962,regular,{238,240,240,255})+36;
        }
        if (guide_orb) {
            if (ripple) {SDL_Rect to{1526,906,420,150};SDL_BlitSurface(ripple,nullptr,screen,&to);}
            if (auto *orb=button_image(xb::Guide)) {SDL_Rect to{1700,910,72,72};blit_quality(orb,nullptr,screen,&to,true);}
        }
        return;
    }
    if (model.era==3 && !model.guide) {
        // Metro legend (2012 capture): the flat 32 px controlp buttons from
        // x 180 at y 993, labels beside them; the Guide is not listed.
        int x=180;
        for (const auto &hint:model.hints()) {
            if (hint.button==xb::Guide) continue;
            const int slot=hint.button==xb::A?0:hint.button==xb::B?1:hint.button==xb::X?2:hint.button==xb::Y?3:-1;
            // Polished is the era's own look; Retail and Simple use the chosen glyphs.
            if (custom_scene::draw_legend(hint.button,x,993,32)) {
                // LegendScene Press owns the image scale and highlight.
            } else if (slot>=0 && metro_legend[slot] && model.button_style==0) {
                SDL_Rect to{x,993,32,32};
                if (text_alpha<255) SDL_SetSurfaceAlphaMod(metro_legend[slot],text_alpha);
                blit_quality(metro_legend[slot],nullptr,screen,&to,true);
                SDL_SetSurfaceAlphaMod(metro_legend[slot],255);
            } else glyph(hint.button,x,993,32);
            x+=36;
            x+=text(hint.label,x,991,regular,ink)+27;
        }
        return;
    }
    int x=model.era==0 && !model.guide?215+64*int(std::find(std::begin(dash::kBladesChannels),std::end(dash::kBladesChannels),model.channel)-std::begin(dash::kBladesChannels)):95;
    if (model.era==0 && !model.guide) x+=model.dashboard_build<5759?0:32;
    TTF_Font *font=model.era==0 && !model.guide?regular:small;
    // More prompts than the row holds (the Music Library's eight): closer, in the small face.
    int gap=56,need=0;
    for (const auto &hint:model.hints()) need+=55+text_width(hint.label,font)+gap;
    if (x+need>W-40) {font=small;gap=34;}
    for (const auto &hint:model.hints()) {
        glyph(hint.button,x,y,44);
        x+=55;
        x+=text(hint.label,x,y+(font==small?10:6),font,ink);
        x+=gap;
    }
}
void save_pins() {
    FILE *file=fopen("/data/nxe/attachments.tmp","w");
    if (!file) return;
    for (const auto &path:model.pins) fprintf(file,"%s\n",path.c_str());
    fclose(file);
    rename("/data/nxe/attachments.tmp",pins_path);
}
void load_pins() {
    std::vector<std::string> lines;
    FILE *file=fopen(pins_path,"r");
    if (file) {
        char path[1024];
        while (fgets(path,sizeof path,file) && lines.size()<64) {
            std::string item(path);
            while (!item.empty() && (item.back()=='\n' || item.back()=='\r')) item.pop_back();
            lines.push_back(item);
        }
        fclose(file);
    }
    model.load_pins(lines);
}
// The 360's interface sounds (huduiskin.xex and shrdres.xzp, converted to
// 48 kHz WAV) on one audio device, mixed in its callback. The device is closed
// before anything is launched so no audio port is held across the handoff.
#include "runtime/sound_effects.inc.hpp"
// The Music Library's songs and the player's controls. The player's list
// changes only with the audio device locked (see music.hpp).
SDL_Surface *song_cover(const std::string &path);
bool song_cover_known(const std::string &path);
bool cover_probe_turn();
void now_playing_toast(const std::string &song, SDL_Surface *cover);
// Covers handed to toasts are copies: the cover cache may drop its own any time.
std::array<SDL_Surface *,6> toast_covers{};
size_t toast_cover_next=0;
SDL_Surface *keep_for_toast(SDL_Surface *cover) {
    if (!cover) return nullptr;
    SDL_Surface *&slot=toast_covers[toast_cover_next++%toast_covers.size()];
    SDL_FreeSurface(slot);
    slot=SDL_ConvertSurfaceFormat(cover,SDL_PIXELFORMAT_ARGB8888,0);
    if (slot) SDL_SetSurfaceBlendMode(slot,SDL_BLENDMODE_BLEND);
    return slot;
}
#include "runtime/music_player.inc.hpp"
// A card's picture: the title's own icon, or, on the Music card while a
// song plays, that song's cover.
SDL_Surface *title_icon(const std::string &path,bool preserve_source);
SDL_Surface *card_picture(const dash::Card &card) {
    if (card.image.rfind("music:",0)==0) return song_cover(card.image.substr(6));
    if (SDL_Surface *own=title_icon(card.image,false)) return own;
    if (card.goal==dash::Goal::ToMusic && model.now_playing>=0)
        if (const music::Song *song=music_player.current_song()) return song_cover(song->path);
    return nullptr;
}
// The 360's notification toast: slides up at the bottom centre, holds, goes.
// One at a time, in order; they wait while the boot, a launch or the dimmed
// screen is showing.
struct Toast { std::string title, text; Uint32 at=0; bool live=false; const sfx::Clip *sound=nullptr; SDL_Surface *icon=nullptr; };
Toast toast;
std::deque<Toast> toast_queue;
void show_toast(const std::string &title, const std::string &text, const sfx::Clip *sound, SDL_Surface *icon=nullptr) {
    model.record_notification(title,text);
    if (model.notification_level==0) return;
    if (toast_queue.size()>=4) toast_queue.pop_front();
    toast_queue.push_back({title,text,0,false,sound,icon});
}
// Songs changing are announced but kept out of the notification history.
void now_playing_toast(const std::string &song, SDL_Surface *cover) {
    if (model.notification_level==0) return;
    if (toast_queue.size()>=4) toast_queue.pop_front();
    toast_queue.push_back({"Now Playing",song,0,false,nullptr,cover});
}
// Keep the source resolution until drawing into the final card rectangle.
std::map<std::string,SDL_Surface *> icon_cache;
size_t icon_cache_bytes=0;
Uint64 library_generation=0;
// Ads keep original source pixels until the final viewport resample. Both
// variants share the existing cache count and 64 MiB ownership limits.
SDL_Surface *title_icon(const std::string &path,bool preserve_source=false) {
    if (path.empty()) return nullptr;
    const std::string key=preserve_source?"original:"+path:path;
    auto it=icon_cache.find(key);
    if (it!=icon_cache.end()) return it->second;
    SDL_Surface *icon=nullptr;
    if (SDL_Surface *raw=load_art(path)) {
        const int longest=std::max(raw->w,raw->h);
        const bool original=preserve_source && size_t(raw->w)*size_t(raw->h)*4<=(64u<<20);
        const int w=original?raw->w:std::max(1,raw->w*std::min(1024*render_density::factor,longest)/longest);
        const int h=original?raw->h:std::max(1,raw->h*std::min(1024*render_density::factor,longest)/longest);
        icon=resample_art(raw,{0,0,raw->w,raw->h},w,h);
        SDL_FreeSurface(raw);
    }
    const size_t bytes=icon?size_t(icon->pitch)*icon->h:0;
    if (icon_cache.size()>=icon_cache_limit || icon_cache_bytes+bytes>(64u<<20)) {
        clear_scaled_art();
        for (auto &e:icon_cache) if (e.second) SDL_FreeSurface(e.second);
        icon_cache.clear();icon_cache_bytes=0;
    }
    if (icon) SDL_SetSurfaceBlendMode(icon,SDL_BLENDMODE_BLEND);
    icon_cache_bytes+=bytes;
    icon_cache[key]=icon;
    return icon;
}
std::string fs_prefix() {
#ifndef __PROSPERO__
    // Desktop builds read a console-shaped folder: NXE_FS_ROOT=/some/dir.
    if (const char *root_dir=getenv("NXE_FS_ROOT")) return root_dir;
#endif
    return "";
}
bool owner_removed_content(std::string_view value) {
    std::string lower(value);
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
    return lower.find("bayonetta")!=std::string::npos;
}
void begin_theme_catalog() {
    std::string path="/data/nxe/themes/catalog.txt";
#ifndef __PROSPERO__
    if(const char* local=getenv("NXE_THEME_CATALOG"))path=local;
#endif
    if(!theme_catalog_job.start([path]{return exbox::themes::parse_manifest(sysdata::read(path,(16u<<20)+1));}))
        log("themes: catalog worker unavailable; keeping Xbox 360 default");
}
void request_installed_theme(int index);
void poll_theme_catalog() {
    if(theme_catalog_reported)return;
    exbox::themes::ManifestResult result;
    if(!theme_catalog_job.take(result))return;
    theme_catalog_reported=true;
    if(!result){log(("themes: catalog refused: "+result.error).c_str());return;}
    installed_theme_catalog=std::move(result.entries);
    installed_theme_catalog.erase(std::remove_if(installed_theme_catalog.begin(),installed_theme_catalog.end(),[](const auto& entry){
        return owner_removed_content(entry.display_name)||owner_removed_content(entry.relative_directory);
    }),installed_theme_catalog.end());
    std::size_t complete=0;for(const auto&entry:installed_theme_catalog)complete+=entry.complete;
    model.health_runtime.themes_complete=int(complete);
    model.health_runtime.themes_partial=int(installed_theme_catalog.size()-complete);
    model.health_runtime.themes_rejected=int(result.rejected);
    // The manifest publishes complete/partial/refused records only. It does
    // not prove missing or changed local files and is not authored-scene evidence.
    model.health_runtime.themes_missing=-1;
    model.health_runtime.themes_changed=-1;
    model.health_runtime.authored_theme_evidence=false;
    std::vector<dash::InstalledTheme> model_entries;model_entries.reserve(installed_theme_catalog.size());
    for(const auto&entry:installed_theme_catalog){unsigned wallpapers=0;for(const auto&wallpaper:entry.wallpapers)wallpapers+=bool(wallpaper);model_entries.push_back({entry.stable_id,entry.display_name,entry.complete,wallpapers});}
    model.set_installed_themes(std::move(model_entries));
    char line[160];std::snprintf(line,sizeof line,"themes: catalog ready entries=%zu complete=%zu partial=%zu rejected=%zu",installed_theme_catalog.size(),complete,installed_theme_catalog.size()-complete,result.rejected);log(line);
    if(!model.installed_theme_id.empty())for(std::size_t i=0;i<model.installed_themes.size();++i)if(model.installed_themes[i].stable_id==model.installed_theme_id){request_installed_theme(int(i));break;}
}
void refresh_games() {
    ++library_generation;
    clear_scaled_art();
    for (auto &entry:icon_cache) if (entry.second) SDL_FreeSurface(entry.second);
    icon_cache.clear();icon_cache_bytes=0;
    model.game_library_companion=sysdata::companion;
    auto games=ps5lib::installed_games(fs_prefix());
    games.erase(std::remove_if(games.begin(),games.end(),[](const dash::App& game){
        return owner_removed_content(game.title)||owner_removed_content(game.title_id)||owner_removed_content(game.executable);
    }),games.end());
    model.set_games(std::move(games));
}
void read_profile() {
    if (user_id<0) return;
    trophies=ps5lib::read_trophies(unsigned(user_id),fs_prefix());
}
void save_settings() {
    const char *path=settings_path;
#ifndef __PROSPERO__
    if (const char *local=getenv("NXE_SETTINGS_PATH")) path=local;
#endif
    if (FILE *f=fopen(path,"w")) {
        fprintf(f,"sounds=%d\ntheme=%d\nera=%d\ndashboard_build=%d\nbutton_style=%d\ndimming=%d\nlibrary_order=%d\nnotifications=%d\npromotions=%d\n%s\n",
                model.sounds?1:0,model.theme,model.era,model.dashboard_build,model.button_style,model.dimming?1:0,model.library_order,model.notification_level,model.promotions?1:0,
                model.saved_promotion_mode_setting().c_str());
        fprintf(f,"video_sort=%d\nvideo_subtitles=%d\n",model.video_order,model.video_subtitles?1:0);
        for (const auto &line:model.saved_picture_settings()) fprintf(f,"%s\n",line.c_str());
        fprintf(f,"clock_24_hour=%d\n",model.clock_24_hour?1:0);
        fprintf(f,"%s\n",model.saved_welcome_setting().c_str());
        fprintf(f,"music_shuffle=%d\nmusic_repeat=%d\nmusic_volume=%d\nmusic_game_scan=%d\nlocal_friend_avatars=%d\n",model.music_shuffle?1:0,model.music_repeat,model.music_volume,model.music_game_scan?1:0,model.local_friend_avatars?1:0);
        for(const auto &line:model.saved_game_frontend_settings())fprintf(f,"%s\n",line.c_str());
        for(const auto&line:model.saved_theme_library_settings())fprintf(f,"%s\n",line.c_str());
        const auto context=model.saved_dashboard_context();if(!context.empty())fprintf(f,"%s\n",context.c_str());
        fclose(f);
    }
}
std::string theme_asset_root() {
#ifndef __PROSPERO__
    if(const char* local=getenv("NXE_THEME_ROOT"))return local;
#endif
    return "/data/nxe/themes/library";
}
void start_requested_theme() {
    if(requested_theme_id.empty()||theme_apply_job.active()||!screen)return;
    const auto found=std::find_if(installed_theme_catalog.begin(),installed_theme_catalog.end(),[](const auto&entry){return entry.stable_id==requested_theme_id;});
    if(found==installed_theme_catalog.end()){show_toast("Theme unavailable","The saved theme is not in this catalog.",&sfx::toast_clip,toast_themes);failed_theme_id=requested_theme_id;failed_theme_slot=requested_theme_slot;requested_theme_id.clear();return;}
    const auto entry=*found;const auto root_path=theme_asset_root();const int width=screen->w,height=screen->h,slot=requested_theme_slot;
    if(!theme_apply_job.start([entry,root_path,width,height,slot]{return exbox::themes::prepare_compatibility_background(entry,root_path,width,height,slot);})){
        failed_theme_id=requested_theme_id;failed_theme_slot=requested_theme_slot;requested_theme_id.clear();
        show_toast("Theme unavailable","The theme worker could not start.",&sfx::toast_clip,toast_themes);
    }
}
void request_installed_theme(int index) {
    if(index<0){
        requested_theme_id.clear();failed_theme_id.clear();failed_theme_slot=-1;model.installed_theme_id.clear();installed_theme_slot=-1;
        if(installed_theme_previous){render_density::free_surface(installed_theme_previous);installed_theme_previous=nullptr;installed_theme_previous_pixels.reset();}
        retire_installed_theme=installed_theme_background!=nullptr;installed_theme_fade.track(false,SDL_GetTicks());
        if(index==-1)model.picture_background_enabled=false;
        save_settings();show_toast(index==-2?"Picture Background":"Xbox 360","Theme changed",&sfx::toast_clip,toast_themes);return;
    }
    if(index>=int(model.installed_themes.size()))return;
    requested_theme_id=model.installed_themes[std::size_t(index)].stable_id;requested_theme_slot=0;failed_theme_id.clear();failed_theme_slot=-1;start_requested_theme();
}
void poll_installed_theme() {
    exbox::themes::PreparedTheme prepared;
    if(theme_apply_job.take(prepared)){
        if(prepared.stable_id!=requested_theme_id||prepared.requested_slot!=requested_theme_slot){start_requested_theme();return;}
        if(!prepared){show_toast("Theme unavailable",prepared.error,&sfx::toast_clip,toast_themes);log(("themes: "+prepared.error).c_str());failed_theme_id=requested_theme_id;failed_theme_slot=requested_theme_slot;requested_theme_id.clear();return;}
        auto pixels=std::make_shared<std::vector<Uint32>>(std::move(prepared.pixels));
        SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels->data(),prepared.width,prepared.height,32,prepared.width*4,SDL_PIXELFORMAT_ARGB8888);
        if(!surface){show_toast("Theme unavailable","Could not publish the prepared background.",&sfx::toast_clip,toast_themes);requested_theme_id.clear();return;}
        surface=render_density::tag(surface,render_density::factor);SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_BLEND);surface_generations::seal(surface);
        if(installed_theme_previous)render_density::free_surface(installed_theme_previous);
        installed_theme_previous=installed_theme_background;installed_theme_previous_pixels=std::move(installed_theme_pixels);
        installed_theme_background=surface;installed_theme_pixels=std::move(pixels);installed_theme_slot=prepared.requested_slot;
        retire_installed_theme=false;installed_theme_fade={false,SDL_GetTicks(),300,300};installed_theme_fade.track(true,SDL_GetTicks());
        model.installed_theme_id=prepared.stable_id;model.picture_background_enabled=false;save_settings();
        show_toast("Theme changed",prepared.display_name,&sfx::done_clip,toast_themes);
        log(("themes: compatibility background published "+prepared.stable_id+" source="+std::to_string(prepared.source_slot+1)+" authored_route=0").c_str());
        requested_theme_id.clear();failed_theme_id.clear();failed_theme_slot=-1;
    }
    start_requested_theme();
}
void poll_theme_preview() {
    std::string desired;
    if(model.viewer==dash::Viewer::Themes){const int index=model.theme_catalog_index_for_row(model.viewer_item);if(index>=0)desired=model.installed_themes[std::size_t(index)].stable_id;}
    const Uint32 now=SDL_GetTicks();
    if(desired!=theme_preview_focus_id){theme_preview_focus_id=desired;theme_preview_focus_at=now;}
    exbox::themes::PreparedTheme prepared;
    if(theme_preview_job.take(prepared)){
        if(prepared&&prepared.stable_id==theme_preview_requested_id&&prepared.stable_id==theme_preview_focus_id){
            auto pixels=std::make_shared<std::vector<Uint32>>(std::move(prepared.pixels));
            SDL_Surface*surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels->data(),prepared.width,prepared.height,32,prepared.width*4,SDL_PIXELFORMAT_ARGB8888);
            if(surface){surface=render_density::tag(surface,render_density::factor);SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_BLEND);surface_generations::seal(surface);if(installed_theme_preview)render_density::free_surface(installed_theme_preview);installed_theme_preview=surface;installed_theme_preview_pixels=std::move(pixels);theme_preview_published_id=prepared.stable_id;}
            else theme_preview_published_id=theme_preview_requested_id;
        }
        else if(prepared.stable_id==theme_preview_requested_id&&prepared.stable_id==theme_preview_focus_id)theme_preview_published_id=theme_preview_requested_id;
        theme_preview_requested_id.clear();
    }
    if(desired.empty()){
        if(installed_theme_preview){render_density::free_surface(installed_theme_preview);installed_theme_preview=nullptr;installed_theme_preview_pixels.reset();}
        theme_preview_published_id.clear();return;
    }
    if(theme_preview_published_id==desired||theme_preview_job.active()||now-theme_preview_focus_at<150)return;
    const auto found=std::find_if(installed_theme_catalog.begin(),installed_theme_catalog.end(),[&](const auto&entry){return entry.stable_id==desired;});
    if(found==installed_theme_catalog.end()){theme_preview_published_id=desired;return;}
    const auto entry=*found;const auto root_path=theme_asset_root();const int width=480*render_density::factor,height=270*render_density::factor;
    theme_preview_requested_id=desired;
    if(!theme_preview_job.start([entry,root_path,width,height]{return exbox::themes::prepare_preview(entry,root_path,width,height);})){theme_preview_requested_id.clear();theme_preview_published_id=desired;}
}
// Lines of a small text file (attachments, recents), without line ends.
std::vector<std::string> read_lines(const char *path, size_t limit) {
    std::vector<std::string> lines;
    if (FILE *file=fopen(path,"r")) {
        char line[1024];
        while (fgets(line,sizeof line,file) && lines.size()<limit) {
            std::string item(line);
            while (!item.empty() && (item.back()=='\n' || item.back()=='\r')) item.pop_back();
            lines.push_back(item);
        }
        fclose(file);
    }
    return lines;
}
void save_recent() {
    FILE *file=fopen("/data/nxe/recent.tmp","w");
    if (!file) return;
    for (const auto &key:model.recent) fprintf(file,"%s\n",key.c_str());
    fclose(file);
    rename("/data/nxe/recent.tmp",recent_path);
}
// Trophies earned since the last look become the 360's achievement toast.
void check_trophies(Uint32 now) {
    trophies_checked=now;
    read_profile();
    if (!trophies.known) return;
    const std::vector<std::string> saved=read_lines(trophies_path,1);
    const ps5lib::Trophies before=ps5lib::load_trophies(saved.empty()?"":saved[0]);
    const ps5lib::Unlock unlock=ps5lib::unlocked(before,trophies);
    if (!unlock.empty()) {look3d::moment(exbox::dashboard_director::AvatarMoment::Achievement,now);show_toast(unlock.title,unlock.text,&sfx::toast_clip,toast_trophy?toast_trophy:trophy_icon);}
    const std::string line=ps5lib::save_trophies(trophies);
    if (!saved.empty() && saved[0]==line) return;
    if (FILE *f=fopen("/data/nxe/trophies.tmp","w")) {
        fprintf(f,"%s\n",line.c_str());
        fclose(f);
        rename("/data/nxe/trophies.tmp",trophies_path);
    }
}
void save_categories() {
    FILE *file=fopen("/data/nxe/categories.tmp","w");
    if (!file) return;
    for (const auto &path:model.game_apps) fprintf(file,"%s\n",path.c_str());
    fclose(file);
    rename("/data/nxe/categories.tmp",categories_path);
}
void load_categories() {
    std::vector<std::string> lines;
    if (FILE *file=fopen(categories_path,"r")) {
        char line[1024];
        while (fgets(line,sizeof line,file) && lines.size()<256) {
            std::string item(line);
            while (!item.empty() && (item.back()=='\n' || item.back()=='\r')) item.pop_back();
            lines.push_back(item);
        }
        fclose(file);
    }
    model.load_categories(lines);
}
// Trailer choices from Details: "<title key>\t<trailer key>", empty for none.
void save_trailer_choices() {
    FILE *file=fopen("/data/nxe/trailers.tmp","w");
    if (!file) return;
    for (const auto &e:model.trailer_choice) fprintf(file,"%s\t%s\n",e.first.c_str(),e.second.c_str());
    fclose(file);
    rename("/data/nxe/trailers.tmp",trailer_choices_path);
}
void load_trailer_choices() {
    model.trailers.clear();
    model.trailer_names.clear();
    // One choice per clip: other names for the same game (aliases) still
    // match automatically but are not offered again in Details.
    std::set<std::string> folders;
    for (const auto &t:trailers) {
        if (!folders.insert(t.folder).second) continue;
        model.trailers.push_back(t.key);
        if (!t.name.empty()) model.trailer_names[t.key]=t.name;
    }
    model.trailer_choice.clear();
    if (FILE *file=fopen(trailer_choices_path,"r")) {
        char line[1200];
        while (fgets(line,sizeof line,file) && model.trailer_choice.size()<512) {
            std::string item(line);
            while (!item.empty() && (item.back()=='\n' || item.back()=='\r')) item.pop_back();
            const size_t tab=item.find('\t');
            if (tab==std::string::npos || tab==0) continue;
            const std::string value=item.substr(tab+1);
            // Keep only choices that still name a trailer on the console.
            if (value.empty() || std::find(model.trailers.begin(),model.trailers.end(),value)!=model.trailers.end())
                model.trailer_choice[item.substr(0,tab)]=value;
        }
        fclose(file);
    }
}
// What each clip has on disk: the grid offers only clips that can play.
void scan_video_files() {
    model.video_info.clear();
    for (const auto &t:trailers) {
        if (model.video_info.count(t.key)) continue;
        dash::VideoInfo info;
        info.ms=int((1000ll*t.frames)/t.fps);
        const std::string base=std::string(trailer_root)+"/packed/"+t.folder;
        struct stat st{};
        info.playable=stat((base+".frames").c_str(),&st)==0 && st.st_size>8;
        if (info.playable) {
            info.bytes=st.st_size;
            // Ex-Box addition: no 360 original; local packed mtime substitutes for download date.
            info.downloaded=std::max(0ll,static_cast<long long>(st.st_mtime));
        }
        if (stat((base+".wav").c_str(),&st)==0) info.bytes+=st.st_size;
        info.subtitles=stat((base+".srt").c_str(),&st)==0 && st.st_size>0;
#ifdef NXE_DIRECT_VIDEO
        if(t.key=="local-direct-test" && direct_trailer_index>=0 && stat(direct_video_path.c_str(),&st)==0) {
            info.playable=true;info.bytes=st.st_size;
        }
#endif
        model.video_info[t.key]=info;
    }
}
// "<key>\t<ms>" per watched video, most recent first.
void save_videos() {
    const std::string tmp=std::string(video_state_path)+".tmp";
    FILE *file=fopen(tmp.c_str(),"w");
    if (!file) return;
    for (const auto &line:model.saved_videos()) fprintf(file,"%s\n",line.c_str());
    fclose(file);
    rename(tmp.c_str(),video_state_path);
}
void load_videos() {
    scan_video_files();
    model.load_videos(read_lines(video_state_path,dash::kMaxVideoRecent));
}
void save_artwork_choices() {
    if (FILE *file=fopen("/data/nxe/artwork.tmp","w")) {
        for (const auto &choice:model.artwork_choice)
            fprintf(file,"%s\t%d\n",choice.first.c_str(),choice.second);
        fclose(file);
        rename("/data/nxe/artwork.tmp",artwork_choices_path);
    }
}
void load_artwork_choices() {
    model.artwork_choice.clear();
    for (const std::string &line:read_lines(artwork_choices_path,512)) {
        const size_t tab=line.find('\t');
        if (tab==std::string::npos) continue;
        const std::string id=line.substr(0,tab);
        const std::string value=line.substr(tab+1);
        if (ps5lib::title_id(id) && (value=="1" || value=="2"))
            model.artwork_choice[id]=value[0]-'0';
    }
}
void load_settings() {
    model.picture_random=unsigned(std::time(nullptr));
    int old_video_order=0, video_sort=-1;
    bool promotion_mode_loaded=false,legacy_promotions_seen=false;
    const char *path=settings_path;
#ifndef __PROSPERO__
    if (const char *local=getenv("NXE_SETTINGS_PATH")) path=local;
#endif
    if (FILE *f=fopen(path,"r")) {
        char line[256];
        while (fgets(line,sizeof line,f)) {
            model.load_picture_setting(line);
            model.load_game_frontend_setting(line);
            model.load_installed_theme_setting(line);
            model.load_dashboard_context(line);
            promotion_mode_loaded=model.load_promotion_mode_setting(line)||promotion_mode_loaded;
            if (std::strncmp(line,"sounds=",7)==0) model.sounds=atoi(line+7)!=0;
            if (std::strncmp(line,"theme=",6)==0) model.load_theme(atoi(line+6));
            if (std::strncmp(line,"era=",4)==0) model.era=std::clamp(atoi(line+4),0,dash::kEraCount-1);
            if (std::strncmp(line,"dashboard_build=",16)==0) model.dashboard_build=atoi(line+16);
            if (std::strncmp(line,"button_style=",13)==0) model.button_style=std::clamp(atoi(line+13),0,2);
            if (std::strncmp(line,"dimming=",8)==0) model.dimming=atoi(line+8)!=0;
            model.load_welcome_setting(line);
            if (std::strncmp(line,"clock_24_hour=",14)==0) model.clock_24_hour=atoi(line+14)!=0;
            if (std::strncmp(line,"library_order=",14)==0) model.library_order=std::clamp(atoi(line+14),0,2);
            if (std::strncmp(line,"notifications=",14)==0) model.notification_level=std::clamp(atoi(line+14),0,3);
            if (std::strncmp(line,"promotions=",11)==0) {model.promotions=atoi(line+11)!=0;legacy_promotions_seen=true;}
            if (std::strncmp(line,"music_shuffle=",14)==0) model.music_shuffle=atoi(line+14)!=0;
            if (std::strncmp(line,"music_repeat=",13)==0) model.music_repeat=std::clamp(atoi(line+13),0,2);
            if (std::strncmp(line,"music_volume=",13)==0) model.music_volume=std::clamp(atoi(line+13),0,10);
            if (std::strncmp(line,"music_game_scan=",16)==0) model.music_game_scan=atoi(line+16)!=0;
            if (std::strncmp(line,"local_friend_avatars=",21)==0) model.local_friend_avatars=atoi(line+21)!=0;
            if (std::strncmp(line,"video_order=",12)==0) old_video_order=atoi(line+12);
            if (std::strncmp(line,"video_sort=",11)==0) video_sort=std::clamp(atoi(line+11),0,dash::kVideoOrderCount-1);
            if (std::strncmp(line,"video_subtitles=",16)==0) model.video_subtitles=atoi(line+16)!=0;
        }
        fclose(f);
    }
    if(!promotion_mode_loaded&&legacy_promotions_seen)model.promotion_mode=model.promotions?0:2;
    if(model.music_volume>0)model.music_volume_restore=model.music_volume;
    sfx::music_mix_volume.store((model.music_volume*256+5)/10,std::memory_order_relaxed);
    model.promotions=model.promotion_mode!=2;
    model.load_video_order(video_sort>=0?video_sort:old_video_order,video_sort<0);
#ifndef __PROSPERO__
    if (const char *preview=getenv("NXE_ERA")) model.era=std::clamp(atoi(preview),0,dash::kEraCount-1);
    if (const char *preview=getenv("NXE_BUTTON_STYLE")) model.button_style=std::clamp(atoi(preview),0,2);
#endif
    bool valid=false;
    for (auto update:dash::dashboard_updates())
        if (update.build==model.dashboard_build && dash::kDashboardFamilies[update.family].era==model.era) valid=true;
    if (!valid) {constexpr int builds[]={6770,9199,13604,17559};model.dashboard_build=builds[model.era];}
}
#include "assets/promotions.inc.hpp"
// The DualSense light bar as the 360 controller's Ring of Light: Xbox green
// while the dashboard is in front, a bright flash as the Guide opens, and
// handed back to the system (reset) before a game starts or the app exits.
namespace light {
std::vector<SDL_GameController *> pads;
int shown=-1;           // 0 reset, 1 green, 2 flash
Uint32 flash_until=0;
#ifdef __PROSPERO__
struct PadColor { uint8_t r, g, b, a; };
using GetHandle=int (*)(int, int, int);
using SetBar=int (*)(int, PadColor *);
using ResetBar=int (*)(int);
GetHandle get_handle=nullptr; SetBar set_bar=nullptr; ResetBar reset_bar=nullptr;
void resolve() {
    static bool tried=false;
    if (tried) return;
    tried=true;
    if (void *h=dlopen("libScePad.sprx",RTLD_LAZY)) {
        get_handle=reinterpret_cast<GetHandle>(dlsym(h,"scePadGetHandle"));
        set_bar=reinterpret_cast<SetBar>(dlsym(h,"scePadSetLightBar"));
        reset_bar=reinterpret_cast<ResetBar>(dlsym(h,"scePadResetLightBar"));
    }
}
#endif
void colour(int state) {
    static const Uint8 rgb[3][3]={{0,0,0},{16,170,16},{170,255,110}};
#ifdef __PROSPERO__
    resolve();
    const int handle=get_handle && user_id>=0?get_handle(user_id,0,0):-1;
    if (handle>=0) {
        if (state==0) {if (reset_bar) reset_bar(handle);}
        else if (set_bar) {PadColor c{rgb[state][0],rgb[state][1],rgb[state][2],0}; set_bar(handle,&c);}
        return;
    }
#endif
    if (state==0) return; // SDL has no reset; the system restores its own colour
    for (SDL_GameController *pad:pads) SDL_GameControllerSetLED(pad,rgb[state][0],rgb[state][1],rgb[state][2]);
}
void update(Uint32 now) {
    const int want=suspended || !running?0:(Sint32(flash_until-now)>0?2:1);
    if (want==shown) return;
    shown=want;
    colour(want);
}
void flash(Uint32 now) {flash_until=now+320;}
void hand_back() {shown=0; colour(0);}
}
#ifdef __PROSPERO__
// Starting an installed game the way PS5 Upload does: sceLncUtilLaunchApp
// with a zeroed parameter, then with none, then sceSystemServiceLaunchApp.
// Resolved at run time so a firmware without one of them still starts.
struct LncAppParam { uint32_t sz; uint32_t user_id; uint32_t app_opt; uint64_t crash_report; uint32_t check_flag; };
#endif
bool launch_title(const dash::App &game) {
#ifdef EXBOX_NATIVE_TITLE
    // Native titles cannot resolve LncUtil: reuse the companion launch wire.
    if (!ps5lib::title_id(game.title_id)) return false;
    return nxe::launch_homebrew({game.title,game.title_id});
#elif defined(__PROSPERO__)
    using Lnc=int (*)(const char *, const char **, LncAppParam *);
    using Sys=int (*)(const char *, const char **, void *);
    Lnc lnc=nullptr; Sys sys=nullptr;
    for (const char *lib:{"libSceSystemService.sprx","libSceLncUtil.sprx"}) {
        if (void *h=dlopen(lib,RTLD_LAZY)) {
            if (!lnc) lnc=reinterpret_cast<Lnc>(dlsym(h,"sceLncUtilLaunchApp"));
            if (!sys) sys=reinterpret_cast<Sys>(dlsym(h,"sceSystemServiceLaunchApp"));
        }
    }
    const char *argv[]={nullptr};
    char line[128];
    if (lnc) {
        LncAppParam param{};
        param.sz=sizeof param;
        param.user_id=uint32_t(user_id);
        int rc=lnc(game.title_id.c_str(),argv,&param);
        std::snprintf(line,sizeof line,"launch %s: LncUtil(param) rc=0x%x",game.title_id.c_str(),unsigned(rc));
        log(line);
        if (rc>=0) return true;
        rc=lnc(game.title_id.c_str(),argv,nullptr);
        std::snprintf(line,sizeof line,"launch %s: LncUtil(null) rc=0x%x",game.title_id.c_str(),unsigned(rc));
        log(line);
        if (rc>=0) return true;
    }
    if (sys) {
        int rc=sys(game.title_id.c_str(),argv,nullptr);
        std::snprintf(line,sizeof line,"launch %s: SystemService rc=0x%x",game.title_id.c_str(),unsigned(rc));
        log(line);
        if (rc>=0) return true;
    }
    if (!lnc && !sys) log("launch: no launcher function found");
    return false;
#else
    (void)game;
    return false;
#endif
}
void refresh_apps() {
    ++library_generation;
    std::vector<dash::App> found;
#ifndef __PROSPERO__
    // Desktop builds: NXE_DEMO_APPS=n shows n sample titles.
    if (const char *demo=getenv("NXE_DEMO_APPS")) {
        static const char *names[]={"Omni","Bayonetta","Gears of War 2","Burnout Paradise","Sonic Unleashed",
            "PS5 Upload","Itemzflow","A Homebrew App With A Very Long Name Indeed","ftpsrv","websrv","Kstuff","EVO Player"};
        int n=std::clamp(atoi(demo),0,12);
        for (int i=0;i<n;i++) found.push_back({names[i],std::string("/data/homebrew/")+names[i]+"/eboot.elf","",""});
        found.erase(std::remove_if(found.begin(),found.end(),[](const dash::App &app){
            return owner_removed_content(app.title)||owner_removed_content(app.executable);
        }),found.end());
        model.set_apps(found);
        return;
    }
#endif
    for (const auto &app:nxe::discover_homebrew()) {
        // The app's own icon sits beside its executable, as on the Home screen.
        std::string icon=app.executable.substr(0,app.executable.rfind('/'))+"/sce_sys/icon0.png";
        struct stat st;
        if (stat(icon.c_str(),&st)!=0) icon.clear();
        found.push_back({app.title,app.executable,"",icon});
    }
    found.erase(std::remove_if(found.begin(),found.end(),[](const dash::App &app){
        return owner_removed_content(app.title)||owner_removed_content(app.executable);
    }),found.end());
    model.set_apps(found);
}
#include "ui/promotion_art.inc.hpp"
#include "ui/nxe_cards.inc.hpp"
void draw_gamercard();
void draw_gamerpic(int x, int y, int w, int h);
// Recolour the background and pane art when the theme changes.
void apply_theme() {
    if (themed_for==model.theme) return;
    themed_for=model.theme;
    clear_scaled_art();
    if (themed_background) {SDL_FreeSurface(themed_background);themed_background=nullptr;}
    for (auto *&c:themed_cards) if (c) {SDL_FreeSurface(c);c=nullptr;}
    for (auto *&c:themed_icons) if (c) {SDL_FreeSurface(c);c=nullptr;}
    if (model.theme<=0) return;
    const Tint tint=theme_tint(model.theme);
    if (background) {
        SDL_Surface *copy=tinted_copy(background,tint);
        if (copy) {themed_background=SDL_ConvertSurfaceFormat(copy,screen->format->format,0); SDL_FreeSurface(copy);}
    }
    for (int i=0;i<5;i++) if (card_art[i]) {
        themed_cards[i]=tinted_copy(card_art[i],tint);
        if (themed_cards[i]) {
            Uint8 a=255; SDL_GetSurfaceAlphaMod(card_art[i],&a);
            SDL_SetSurfaceBlendMode(themed_cards[i],SDL_BLENDMODE_BLEND);
            SDL_SetSurfaceAlphaMod(themed_cards[i],a);
        }
    }
    for (int i=0;i<dash::IconCount;i++) if (card_icons[i] && (themed_icons[i]=tinted_copy(card_icons[i],tint)))
        SDL_SetSurfaceBlendMode(themed_icons[i],SDL_BLENDMODE_BLEND);
    surface_generations::seal(themed_background);for(auto*c:themed_cards)surface_generations::seal(c);for(auto*c:themed_icons)surface_generations::seal(c);
}
// Slow light drifting across the sky, and the glow along the horizon.
void draw_atmosphere(Uint32 now) {
    const float t=float(now%600000u)/1000.0f;
    if (glow) {
        struct Drift { float x, y, w, h, speed, amp, phase; Uint8 alpha; };
        static constexpr Drift drifts[]={
            {420,110,900,330,0.050f,260,0.0f,26},
            {1350,220,760,280,0.037f,320,2.1f,20},
            {900,40,1100,260,0.023f,420,4.2f,16},
        };
        int drift_index=0;
        for (const auto &d:drifts) {
            const float x=d.x+std::sin(t*d.speed*6.2832f+d.phase)*d.amp;
            const float a=float(d.alpha)*(0.75f+0.25f*std::sin(t*0.21f+d.phase));
            SDL_Rect r{int(x-d.w/2),int(d.y),int(d.w),int(d.h)};
            SDL_Surface* cached=atmosphere_glow[size_t(drift_index++)];
            if(cached){SDL_SetSurfaceAlphaMod(cached,Uint8(a));SDL_BlitSurface(cached,nullptr,screen,&r);}
            else {SDL_SetSurfaceAlphaMod(glow,Uint8(a));SDL_BlitScaled(glow,nullptr,screen,&r);}
        }
    }
    if (aura) {
        // Aura_Horizon.png from the NXE dashboard, breathing along the floor line.
        const float a=70+30*std::sin(t*0.6f);
        SDL_Rect r{0,573,W,84};
        SDL_SetSurfaceAlphaMod(aura,Uint8(a));
        SDL_BlitScaled(aura,nullptr,screen,&r);
    }
}
// The later layouts share the same selected card and launch actions as NXE.
#include "ui/era_art.inc.hpp"
#include "ui/blades_hub.inc.hpp"
#include "runtime/blades_scene.inc.hpp"
// The owned picture is shared by NXE and Kinect; other eras keep their own art.
Fade picture_background_fade{false,0,300,300}; // Ex-Box addition: crossfade timing
bool draw_picture_background() {
    // Plain crossfade: themeripple.uxfx's shader semantics are not reproduced.
    Fade &fade=picture_background_fade;
    const bool active=model.picture_background_available() && model.picture_background_enabled &&
                      model.picture_background>0 && picture_backdrop;
    const Uint32 now=SDL_GetTicks();
    fade.track(active,now);
    const float alpha=fade.value(now);
    if (model.picture_background_available() && picture_backdrop && alpha>0) {
        SDL_SetSurfaceBlendMode(picture_backdrop,SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceAlphaMod(picture_backdrop,Uint8(255*alpha));
        SDL_BlitSurface(picture_backdrop,nullptr,screen,nullptr);
        SDL_SetSurfaceAlphaMod(picture_backdrop,255);
    }
    return active || alpha>0;
}
bool draw_installed_theme_background() {
    const Uint32 now=SDL_GetTicks();
    const bool active=installed_theme_background&&!retire_installed_theme;
    installed_theme_fade.track(active,now);const float alpha=installed_theme_background?installed_theme_fade.value(now):0;
    if(installed_theme_previous&&!retire_installed_theme)SDL_BlitSurface(installed_theme_previous,nullptr,screen,nullptr);
    if(installed_theme_background&&alpha>0){SDL_SetSurfaceBlendMode(installed_theme_background,SDL_BLENDMODE_BLEND);SDL_SetSurfaceAlphaMod(installed_theme_background,Uint8(255*std::clamp(alpha,0.0f,1.0f)));SDL_BlitSurface(installed_theme_background,nullptr,screen,nullptr);SDL_SetSurfaceAlphaMod(installed_theme_background,255);}
    if(installed_theme_previous&&alpha>=1){render_density::free_surface(installed_theme_previous);installed_theme_previous=nullptr;installed_theme_previous_pixels.reset();}
    if(installed_theme_background&&retire_installed_theme&&alpha<=0){render_density::free_surface(installed_theme_background);installed_theme_background=nullptr;installed_theme_pixels.reset();retire_installed_theme=false;}
    return active||alpha>0;
}
#include "runtime/editor_dashboard.inc.hpp"
#include "runtime/gpu_home.inc.hpp"
#include "runtime/gpu_overlay.inc.hpp"
#include "ui/kinect_hub.inc.hpp"
#include "ui/metro_hub.inc.hpp"
#include "ui/hub_dispatch.inc.hpp"
void draw_details() {
    shade(0,0,W,H,0x000000,110);
    rect(430,250,1060,580,0x182c21);
    rect(430,250,1060,4,0xd8edc8);
    text(fit(model.details_title,large,920),500,330,large,{255,255,255,255});
    wrapped_text(model.details_text,500,430,920,7,regular,{205,225,201,255});
}
#include "ui/guide_profile.inc.hpp"
#include "runtime/blades_guide_scene.inc.hpp"
#include "runtime/notify_scene.inc.hpp"
// Stretch an image into a box keeping its corners: `b` source pixels on each
// edge become `db` destination pixels, and only the middle bands stretch.
void nine_slice(SDL_Surface *img, int x, int y, int w, int h, int b, int db, Uint8 alpha=255) {
    if (!img || w<=0 || h<=0) return;
    db=std::min(db,std::min(w,h)/2);
    const int sx[4]={0,b,img->w-b,img->w}, sy[4]={0,b,img->h-b,img->h};
    const int dx[4]={x,x+db,x+w-db,x+w}, dy[4]={y,y+db,y+h-db,y+h};
    SDL_SetSurfaceAlphaMod(img,alpha);
    for (int r=0;r<3;r++) for (int c=0;c<3;c++) {
        SDL_Rect from{sx[c],sy[r],sx[c+1]-sx[c],sy[r+1]-sy[r]};
        SDL_Rect to{dx[c],dy[r],dx[c+1]-dx[c],dy[r+1]-dy[r]};
        if (to.w>0 && to.h>0) blit_quality(img,&from,screen,&to);
    }
    SDL_SetSurfaceAlphaMod(img,255);
}
// The Guide, as NXE drew it. Geometry is WebOS NXE's measurement of retail
// 9199 (app/src/guide.ts), authored at 720p and multiplied by 1.5 here.
#include "ui/guide.inc.hpp"
struct MapRow { xb::Button button; const char *xbox, *ps5, *action; };
const MapRow map_rows[]={
    {xb::A,"A","Cross","Select / open"},
    {xb::B,"B","Circle","Back / close"},
    {xb::X,"X","Square","Refresh homebrew"},
    {xb::Y,"Y","Triangle","Details"},
    {xb::LB,"LB","L1","Page left, 5 panes"},
    {xb::RB,"RB","R1","Page right, 5 panes"},
    {xb::LT,"LT","L2","First pane"},
    {xb::RT,"RT","R2","Last pane"},
    {xb::Up,"D-pad","D-pad","Move (hold to repeat)"},
    {xb::None,"Left stick","Left stick","Move (hold to repeat)"},
    {xb::LS,"LS click","L3","With RS: PS5 Home"},
    {xb::RS,"RS click","R3","With LS: PS5 Home"},
    {xb::Start,"Start","Options","Open the Guide"},
    {xb::Guide,"Guide","Touchpad click","Open / close the Guide"},
    {xb::Back,"Back","Create","Taken by the PS5: use B"},
};
bool lit(xb::Button button, Uint32 now) {
    if (button==xb::Up) {
        for (xb::Button d:{xb::Up,xb::Down,xb::Left,xb::Right})
            if ((held&xb::bit(d)) || now-flashed[d]<300) return true;
        return false;
    }
    if (button==xb::None) return stick.held()!=xb::None;
    return (held&xb::bit(button)) || now-flashed[button]<300;
}
#include "ui/controller_and_details.inc.hpp"
// ---- Media: Pictures (PS5 screenshots) and Game Videos (every trailer) ----
std::map<std::string,SDL_Surface *> picture_thumbs, video_thumbs;
packed_thumb::Worker thumbnail_preparation;
std::map<std::string,std::pair<int,int>> video_sizes;
// Picture thumbnails stay while they are used: the least recently drawn goes
// first once kPictureThumbs are held (about 28 MB), so paging back is instant.
constexpr size_t kPictureThumbs=96;
std::vector<std::string> picture_thumb_order; // least recently used first
std::map<std::string,std::pair<int,int>> picture_sizes; // each picture's own size, learnt as it decodes
// Full screen: the picture fitted to the screen, the one it cross-fades from,
// the next one decoded ahead, and (only while zoomed) the original and a
// sharp render of the zoomed view.
SDL_Surface *picture_full=nullptr, *picture_before=nullptr, *picture_ahead=nullptr;
SDL_Surface *picture_source=nullptr, *picture_view=nullptr;
std::string picture_full_path, picture_ahead_path, picture_source_path, picture_view_key;
constexpr int kTileW=360, kTileH=203;
void drop_surface(SDL_Surface *&s) {if (s) SDL_FreeSurface(s); s=nullptr;}
void free_media() {
    thumbnail_preparation.reset();
    video_sizes.clear();
    clear_scaled_art();
    for (auto *cache:{&picture_thumbs,&video_thumbs}) {
        for (auto &e:*cache) if (e.second) SDL_FreeSurface(e.second);
        cache->clear();
    }
    picture_thumb_order.clear();
    for (SDL_Surface **s:{&picture_full,&picture_before,&picture_ahead,&picture_source,&picture_view}) drop_surface(*s);
    picture_full_path.clear(); picture_ahead_path.clear(); picture_source_path.clear(); picture_view_key.clear();
}
// The name of the title a capture folder is named for: an installed game, its
// appmeta (PS5 param.json or PS4 param.sfo) if it is still there, or empty.
std::string capture_title(const std::string &id) {
    for (const auto &g:model.games) if (g.title_id==id && !g.title.empty()) return g.title;
    const std::string p=fs_prefix();
    std::string name=ps5lib::json_title(ps5lib::read_file(p+"/user/appmeta/"+id+"/param.json"));
    if (name.empty()) {
        const std::string sfo=ps5lib::read_file(p+"/system_data/priv/appmeta/"+id+"/param.sfo");
        name=ps5lib::sfo_title(std::vector<unsigned char>(sfo.begin(),sfo.end()));
    }
    if (name.empty() && id.rfind("NPXS",0)==0) name="PlayStation 5"; // a system screen
    return name;
}
void scan_pictures() {
    std::vector<dash::PictureFile> found;
    std::map<std::string,std::string> titles;
    std::set<std::string> paths;
    for (const auto &path:ps5lib::find_captures(fs_prefix())) {
        dash::PictureFile f=dash::picture_file(path);
        if (!f.title_id.empty()) {
            auto it=titles.find(f.title_id);
            if (it==titles.end()) it=titles.emplace(f.title_id,capture_title(f.title_id)).first;
            f.game=it->second;
        }
        if(owner_removed_content(f.game)||owner_removed_content(f.path))continue;
        struct stat st;
        if (sysdata::stat(path,&st)==0) f.bytes=st.st_size;
        paths.insert(path);
        found.push_back(std::move(f));
    }
    // Screenshots never change once taken: keep the thumbnails of those still there.
    for (auto it=picture_thumbs.begin();it!=picture_thumbs.end();) {
        if (paths.count(it->first)) {++it; continue;}
        if (it->second) SDL_FreeSurface(it->second);
        picture_thumb_order.erase(std::remove(picture_thumb_order.begin(),picture_thumb_order.end(),it->first),picture_thumb_order.end());
        it=picture_thumbs.erase(it);
    }
    model.set_pictures(std::move(found));
}
// An image scaled to fit a w x h box (letterboxed on black), or null.
SDL_Surface *fitted(SDL_Surface *raw, int w, int h) {
    if (!raw) return nullptr;
    ++x21hook::cpu_fitted_preparations;
    w*=render_density::factor;h*=render_density::factor;
    SDL_Surface *out=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    if (out) {
        SDL_FillRect(out,nullptr,SDL_MapRGB(out->format,0,0,0));
        const float k=std::min(float(w)/float(raw->w),float(h)/float(raw->h));
        SDL_Rect r{int((w-raw->w*k)/2),int((h-raw->h*k)/2),int(raw->w*k),int(raw->h*k)};
        SDL_SetSurfaceBlendMode(raw,SDL_BLENDMODE_NONE);
        SDL_Surface *scaled=resample_art(raw,{0,0,raw->w,raw->h},r.w,r.h);
        if (scaled) {SDL_BlitSurface(scaled,nullptr,out,&r);SDL_FreeSurface(scaled);}
    }
    SDL_FreeSurface(raw);
    surface_generations::seal(out);
    return render_density::tag(out,render_density::factor);
}
// HDR screenshots (JPEG XR, HDR10) decode with jxrlib in the PS5 build and
// are turned into ordinary colour by hdr::convert. Decoding a 4K capture takes
// a moment, so at most one is decoded per frame and the rest wait their turn.
bool is_jxr(const std::string &path) {
    return path.size()>4 && (path.compare(path.size()-4,4,".jxr")==0 || path.compare(path.size()-4,4,".JXR")==0);
}
constexpr bool kCanDecodeJxr=
#ifdef NXE_JXR
    true;
#else
    false;
#endif
bool can_show(const std::string &path) {return ps5lib::viewable_picture(path) || (kCanDecodeJxr && is_jxr(path));}
unsigned long frame_count=0, jxr_decoded_on=~0ul;
bool jxr_turn() {
    if (jxr_decoded_on==frame_count) return false;
    jxr_decoded_on=frame_count;
    return true;
}
// Song covers (music::cover_art: the owner's own picture, the embedded one, then
// the folder's), decoded at most one per frame, kept at 300 px, 96 at a time.
std::map<std::string,SDL_Surface *> song_covers;
std::vector<std::string> song_cover_order;
unsigned long cover_decoded_on=~0ul;
bool song_cover_known(const std::string &path) {return song_covers.count(path)>0;}
// One quick look a frame (tags and folder read, nothing decoded) for a song
// with a picture, when a group's first song has none (tunes::row_cover).
unsigned long cover_probed_on=~0ul;
bool cover_probe_turn() {
    if (cover_probed_on==frame_count) return false;
    cover_probed_on=frame_count;
    return true;
}
SDL_Surface *song_cover(const std::string &path) {
    auto found=song_covers.find(path);
    if (found!=song_covers.end()) return found->second;
    if (cover_decoded_on==frame_count) return nullptr;   // one decode per frame
    cover_decoded_on=frame_count;
    SDL_Surface *cover=nullptr;
    const std::vector<unsigned char> bytes=music::cover_art(path);
    if (!bytes.empty())
        if (SDL_RWops *rw=SDL_RWFromConstMem(bytes.data(),int(bytes.size())))
            if (SDL_Surface *raw=IMG_Load_RW(rw,1)) {
                SDL_Surface *argb=SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(raw);
                if (argb && (argb->w>300*render_density::factor || argb->h>300*render_density::factor)) {
                    const float k=300.f*render_density::factor/std::max(argb->w,argb->h);
                    if (SDL_Surface *small_copy=resample_art(argb,{0,0,argb->w,argb->h},std::max(1,int(argb->w*k)),std::max(1,int(argb->h*k)))) {
                        SDL_FreeSurface(argb);argb=small_copy;
                    }
                }
                cover=argb;
            }
    song_covers[path]=cover;   // a miss is remembered too
    song_cover_order.push_back(path);
    if (song_cover_order.size()>96) {
        invalidate_scaled_art(song_covers[song_cover_order.front()]);
        SDL_FreeSurface(song_covers[song_cover_order.front()]);
        song_covers.erase(song_cover_order.front());
        song_cover_order.erase(song_cover_order.begin());
    }
    return cover;
}
// The PS5 keeps a 1920x1080 thumbnail of each capture beside the original:
// /user/av_contents/thumbnails/photo/<same path>.jxr
std::string capture_thumbnail(const std::string &path) {
    const std::string mark="/av_contents/photo/";
    const size_t at=path.find(mark);
    if (at==std::string::npos) return path;
    std::string thumb=path.substr(0,at)+"/av_contents/thumbnails/photo/"+path.substr(at+mark.size())+".jxr";
    struct stat st;
    return sysdata::stat(thumb,&st)==0 && st.st_size>0?thumb:path;
}
SDL_Surface *load_jxr(const std::string &path, int w, int h, std::pair<int,int> *size=nullptr, int output_density=render_density::factor) {
#ifdef NXE_JXR
    w*=output_density;h*=output_density;
    int sw=0, sh=0; unsigned *pixels=nullptr;
    const int rc=nxe_jxr_read_101010(sysdata::local_path(path).c_str(),&sw,&sh,&pixels);
    if (rc==NXE_JXR_OK && size) *size={sw,sh};
    if (rc!=NXE_JXR_OK) {
        char line[96];
        std::snprintf(line,sizeof line,"pictures: JPEG XR not decoded (%d)",rc);
        log((std::string(line)+": "+path).c_str());
        return nullptr;
    }
    SDL_Surface *out=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    if (out) {
        SDL_FillRect(out,nullptr,SDL_MapRGB(out->format,0,0,0));
        const float k=std::min(float(w)/float(sw),float(h)/float(sh));
        const int dw=std::max(1,int(sw*k)), dh=std::max(1,int(sh*k));
        auto *dst=static_cast<std::uint32_t *>(out->pixels)+((h-dh)/2)*(out->pitch/4)+(w-dw)/2;
        hdr::convert(pixels,sw,sh,sw,dst,dw,dh,out->pitch/4);
    }
    free(pixels);
    return render_density::tag(out,output_density);
#else
    (void)path; (void)w; (void)h; (void)size; (void)output_density;
    return nullptr;
#endif
}
// A picture decoded and fitted to w x h, noting its own size on the way.
SDL_Surface *decode_picture(const std::string &path, int w, int h) {
    if (ps5lib::viewable_picture(path)) {
        SDL_Surface *raw=IMG_Load(sysdata::local_path(path).c_str());
        if (raw) picture_sizes[path]={raw->w,raw->h};
        return fitted(raw,w,h);
    }
    if (is_jxr(path) && kCanDecodeJxr) {
        std::pair<int,int> size{0,0};
        SDL_Surface *out=load_jxr(path,w,h,&size);
        if (size.first>0) picture_sizes[path]=size;
        return out;
    }
    return nullptr;
}
// The grid's thumbnail, or null while it waits: callers pass a budget of new
// decodes for this frame (a 4K capture takes a while to decode), and the
// least recently used thumbnail makes room for a new one.
SDL_Surface *picture_thumb(const std::string &path, int *budget=nullptr) {
    auto it=picture_thumbs.find(path);
    if (it!=picture_thumbs.end()) {
        auto at=std::find(picture_thumb_order.begin(),picture_thumb_order.end(),path);
        if (at!=picture_thumb_order.end() && at+1!=picture_thumb_order.end()) {
            picture_thumb_order.erase(at);
            picture_thumb_order.push_back(path);
        }
        return it->second;
    }
    if (!can_show(path)) return nullptr; // HDR without jxrlib: the tile says why
    if (budget && *budget<=0) return nullptr;
    if (is_jxr(path) && !jxr_turn()) return nullptr; // its turn comes next frame
    if (budget) --*budget;
    while (picture_thumb_order.size()>=kPictureThumbs) {
        auto old=picture_thumbs.find(picture_thumb_order.front());
        if (old!=picture_thumbs.end()) {if (old->second) SDL_FreeSurface(old->second); picture_thumbs.erase(old);}
        picture_thumb_order.erase(picture_thumb_order.begin());
    }
    SDL_Surface *thumb=nullptr;
    if (ps5lib::viewable_picture(path)) thumb=decode_picture(path,kTileW,kTileH);
    else thumb=load_jxr(capture_thumbnail(path),kTileW,kTileH); // the PS5's own 1080p thumbnail
    picture_thumbs[path]=thumb; // a failure is remembered too
    picture_thumb_order.push_back(path);
    return thumb;
}
// The picture full screen. Moving to another keeps the one before for the
// cross-fade, and takes the one decoded ahead when it is the one wanted.
Uint32 picture_shown_at=0;
int picture_full_rotation=0;
SDL_Surface *picture_original(const std::string &path);
// Quarter turns use a software surface; never change the capture on disk.
SDL_Surface *rotate_picture(SDL_Surface *raw,int turns) {
    if (!raw || !turns) return raw;
    const int density=render_density::density(raw);
    SDL_Surface *src=SDL_ConvertSurfaceFormat(raw,SDL_PIXELFORMAT_ARGB8888,0);
    SDL_FreeSurface(raw);
    if (!src) return nullptr;
    SDL_Surface *dst=SDL_CreateRGBSurfaceWithFormat(0,turns%2?src->h:src->w,turns%2?src->w:src->h,32,SDL_PIXELFORMAT_ARGB8888);
    if (dst) for (int y=0;y<src->h;++y) for (int x=0;x<src->w;++x) {
        const int dx=turns==1?src->h-1-y:turns==2?src->w-1-x:y;
        const int dy=turns==1?x:turns==2?src->h-1-y:src->w-1-x;
        auto *out=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(dst->pixels)+dy*dst->pitch);
        const auto *in=reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(src->pixels)+y*src->pitch);
        out[dx]=in[x];
    }
    SDL_FreeSurface(src);
    return render_density::tag(dst,density);
}
SDL_Surface *picture_screen(const std::string &path, Uint32 now) {
    if (path!=picture_full_path || picture_full_rotation!=model.picture_rotation) {
        drop_surface(picture_before);
        picture_before=picture_full;
        picture_full=nullptr;
        drop_surface(picture_source); picture_source_path.clear();
        drop_surface(picture_view); picture_view_key.clear();
        picture_full_rotation=model.picture_rotation;
        if (picture_full_rotation) {
            if (SDL_Surface *original=picture_original(path))
                picture_full=fitted(SDL_ConvertSurface(original,original->format,0),W,H);
        }
        else if (path==picture_ahead_path) {picture_full=picture_ahead; picture_ahead=nullptr; picture_ahead_path.clear();}
        else picture_full=decode_picture(path,W,H);
        picture_full_path=path;
        picture_shown_at=now;
    }
    return picture_full;
}
// Decode the picture the owner (or the slideshow) will most likely want next,
// while this one is simply being looked at.
void picture_decode_ahead(const std::string &path) {
    if (path.empty() || path==picture_ahead_path || path==picture_full_path || !can_show(path)) return;
    if (is_jxr(path) && !jxr_turn()) return;
    drop_surface(picture_ahead);
    picture_ahead=decode_picture(path,W,H);
    picture_ahead_path=path;
}
// The original, for zooming in: decoded the first time RT is pressed on it.
SDL_Surface *picture_original(const std::string &path) {
    const std::string key=path+":"+std::to_string(model.picture_rotation);
    if (key==picture_source_path) return picture_source;
    drop_surface(picture_source);
    picture_source_path=key;
    SDL_Surface *raw=nullptr;
    if (ps5lib::viewable_picture(path)) raw=IMG_Load(sysdata::local_path(path).c_str());
    else if (is_jxr(path) && kCanDecodeJxr) raw=load_jxr(path,3840,2160,nullptr,1); // HDR captures are at most 4K
    raw=rotate_picture(raw,model.picture_rotation);
    if (raw) {
        // In the screen's format, so a zoomed view can be scaled straight onto it.
        picture_source=SDL_ConvertSurface(raw,screen->format,0);
        SDL_FreeSurface(raw);
        surface_generations::seal(picture_source);
    }
    return picture_source;
}
// Store a normalized copy alongside settings.txt, never a reference to the capture.
std::string picture_background_path(int slot) {
    std::string settings=settings_path;
#ifndef __PROSPERO__
    if (const char *local=getenv("NXE_SETTINGS_PATH")) settings=local;
#endif
    const size_t slash=settings.find_last_of("/\\");
    return (slash==std::string::npos?std::string():settings.substr(0,slash+1))+"picture-background-"+std::to_string(slot)+".bmp";
}
void refresh_picture_background() {
    static int loaded=-1;
    if (loaded==model.picture_background) return;
    loaded=model.picture_background;
    clear_scaled_art();
    drop_surface(picture_backdrop);
    if (loaded) picture_backdrop=fitted(SDL_LoadBMP(picture_background_path(loaded).c_str()),W,H);
}
void apply_picture_background() {
    const std::string path=model.picture_background_pending;
    model.picture_background_pending.clear();
    if (!model.picture_background_available() || path.empty()) return;
    // Preserve the original orientation. Rotation in the viewer is display-only.
    SDL_Surface *copy=decode_picture(path,W,H);
    const int slot=model.picture_background==1?2:1;
    // Write only the inactive slot. A failed write cannot damage the active picture.
    const std::string destination=picture_background_path(slot);
    const bool ok=copy && SDL_SaveBMP(copy,destination.c_str())==0;
    SDL_FreeSurface(copy);
    if (!ok) {
        // Ex-Box addition: no 360 original; decoding or local storage failed.
        model.details=true;model.details_title="Apply as Background";
        model.details_text="This picture could not be saved as the background.";
        return;
    }
    model.picture_background=slot;
    model.picture_background_enabled=true;
    refresh_picture_background();
    save_settings();
}
int trailer_index(const std::string &key) {
    for (size_t i=0;i<trailers.size();++i) if (trailers[i].key==key) return int(i);
    return -1;
}
std::string trailer_name(const std::string &key) {
    auto it=model.trailer_names.find(key);
    if (it!=model.trailer_names.end() && !it->second.empty()) return it->second;
    std::string name=key;
    bool start=true;
    for (char &ch:name) {if (start) ch=char(std::toupper(static_cast<unsigned char>(ch))); start=ch==' ';}
    return name;
}
// Each clip's picture size, learnt from its thumbnail frame.

// A still from a third of the way in (six seconds still showed ratings
// cards), for the grid. Decoding is synchronous, so callers pass a budget of
// new decodes per frame and the rest wait their turn.
// All callers are in the Videos grid. Consume its existing decode budget.
// File scanning and encoded reads never happen on this thread.
unsigned long thumbnail_polled_on=~0ul;
void poll_video_thumbnails(int *budget) {
    if (thumbnail_polled_on==frame_count || (budget && *budget<=0)) return;
    thumbnail_polled_on=frame_count;
    auto ready=thumbnail_preparation.poll();
    if (!ready) return;
    if (budget) --*budget;
    SDL_Surface *thumb=nullptr;
    if (ready->error.empty() && !ready->jpeg.empty()) {
        if (SDL_RWops *rw=SDL_RWFromConstMem(ready->jpeg.data(),int(ready->jpeg.size()))) {
            SDL_Surface *raw=IMG_LoadJPG_RW(rw);
            if (raw) video_sizes[ready->key]={raw->w,raw->h};
            thumb=fitted(raw,kTileW,kTileH);
            SDL_RWclose(rw);
        }
    }
    video_thumbs[ready->key]=thumb; // Preserve null-cache failures until refresh.
}
void reset_video_thumbnails() {
    thumbnail_preparation.reset();
    clear_scaled_art(); // Derived entries borrow the source surface identity.
    for (auto &entry:video_thumbs) if (entry.second) SDL_FreeSurface(entry.second);
    video_thumbs.clear();video_sizes.clear();
}
SDL_Surface *video_thumb(const std::string &key, int *budget=nullptr) {
    poll_video_thumbnails(budget);
    auto it=video_thumbs.find(key);
    if (it!=video_thumbs.end()) return it->second;
    // Pending requests consume no decode budget, so later visible slots can queue.
    // The single decode budget is enforced by poll_video_thumbnails above.
    const int index=trailer_index(key);
    if (index<0) {video_thumbs[key]=nullptr;return nullptr;}
    const auto &clip=trailers[size_t(index)];
    const std::string path=std::string(trailer_root)+"/packed/"+clip.folder+".frames";
    const auto state=thumbnail_preparation.request(key,path,uint32_t(clip.frames));
    if (state==packed_thumb::Enqueue::Unavailable) video_thumbs[key]=nullptr;
    return nullptr;
}
#include "ui/fullscreen_media.inc.hpp"
// My Avatar: the avatar large on a lit stage, turning with Left / Right.
#include "runtime/music_library.inc.hpp"
#include "ui/media_pages.inc.hpp"
#include "ui/library.inc.hpp"
#include "ui/dashboard_picker.inc.hpp"
#include "ui/ads_settings.inc.hpp"
#include "ui/developer_diagnostics.inc.hpp"
#include "ui/compat_health.inc.hpp"
#include "ui/system_settings.inc.hpp"
#include "ui/welcome_setup.inc.hpp"
#include "ui/theme_picker.inc.hpp"
#include "ui/profile_achievements.inc.hpp"
#include "ui/rom_game_guide_preview.inc.hpp"
#include "ui/viewer_dispatch.inc.hpp"
#include "ui/notifications.inc.hpp"
// Presenting a frame. SDL's PS5 driver tiles the window surface, submits the
// flip and then waits for vblank inside SDL_UpdateWindowSurface, which held
// the main loop for about half a frame each time (Omni measured this on the
// console, 2026-10-19). A presenter thread does that wait instead: the main
// loop copies the finished frame into the window surface only while the
// presenter is idle (waiting at most 4 ms), and never waits for vblank itself.
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
namespace native_present {
exbox::presenter::Adapter adapter({exbox_native_open_gpu,exbox_native_upload,exbox_native_present,exbox_native_close});
bool active=false,requested=false;
int initialize(){
 char switch_value[4]{};FILE* f=std::fopen("/data/nxe/exbox/native-presenter.flag","rb");size_t n=0;
 if(f){n=std::fread(switch_value,1,sizeof switch_value,f);std::fclose(f);}
 requested=exbox::presenter::enabled(std::string_view(switch_value,n));
 if(requested&&!exbox_native_set_render_scale(unsigned(render_density::factor)))catchReturnFromMain(46);
 const auto route=adapter.start(requested);
 if(route==exbox::presenter::Route::stop)catchReturnFromMain(46);
 if(route==exbox::presenter::Route::sdl){x21hook::available.store(false);exbox_native_note("presenter route: SDL");return SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER);}
 // SDL VideoOut opens during video initialization, so select dummy first.
 const char* prior=SDL_getenv("SDL_VIDEODRIVER");const bool had_driver=prior!=nullptr;
 const std::string old_driver=prior?prior:"";
 if(SDL_setenv("SDL_VIDEODRIVER","dummy",1)==0 && SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)==0){active=true;exbox_native_note("presenter route: native SDR %dx%d",adapter.width(),adapter.height());return 0;}
 // A startup failure can fall back only after native ownership is released.
 if(!adapter.close())catchReturnFromMain(46);
 SDL_Quit();if(had_driver)SDL_setenv("SDL_VIDEODRIVER",old_driver.c_str(),1);else unsetenv("SDL_VIDEODRIVER");
 x21hook::available.store(false);
 exbox_native_note("presenter route: SDL after native release");
 return SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER);
}
}
#endif

namespace present {
std::shared_ptr<x21hook::OwnedPacket> gpu_pending;
std::mutex lock;
std::condition_variable wake, idle;
std::thread *worker=nullptr;
bool busy=false, stop=false;
unsigned long queued=0;
std::atomic<std::uint64_t> first_present_completed_ms{0};
unsigned long submission_count(){std::lock_guard<std::mutex> guard(lock);return queued;}
unsigned long frames=0, skipped=0, reports=0;
double wait_total_ms=0, wait_max_ms=0;
void run() {
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
    ExboxNative2D* gpu_client=nullptr;
#else
    x21hook::HostAudit gpu_audit;
#endif
    bool first_accepted=false;
    std::unique_lock<std::mutex> guard(lock);
    for (;;) {
        wake.wait(guard,[] {return busy || stop;});
        if (stop) {
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
            if(gpu_client&&!exbox_native2d_destroy(&gpu_client))x21hook::fatal.store(true);
#endif
            return;
        }
        auto packet=gpu_pending;gpu_pending.reset();
        guard.unlock();
        const Uint64 started=SDL_GetPerformanceCounter();
        bool accepted=false;
        if(packet){
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
            if(!native_present::active)x21hook::available.store(false);
            else{
                if(!gpu_client)gpu_client=exbox_native2d_create();
                if(!gpu_client)x21hook::available.store(false);
                else if(!([&](){if(!packet->actors.empty()){std::vector<ExboxNativeAvatar> inputs;for(const auto&actor:packet->actors)inputs.push_back({{actor.words->data(),actor.words->size()},actor.command_index});return exbox_native2d_prepare_actors_frame(gpu_client,packet->commands.data(),packet->commands.size(),packet->source_ids.data(),inputs.data(),inputs.size(),packet->avatar_width,packet->avatar_height,packet->avatar_generation,packet->filter_requests.data(),packet->filter_requests.size());}return exbox_native2d_prepare_frame(gpu_client,packet->commands.data(),packet->commands.size(),packet->source_ids.data(),packet->avatar_words.data(),packet->avatar_words.size(),packet->avatar_width,packet->avatar_height,packet->avatar_generation,packet->filter_requests.data(),packet->filter_requests.size());})()){if(exbox_native2d_retained(gpu_client))x21hook::fatal.store(true);else{x21hook::available.store(false);if(!exbox_native2d_destroy(&gpu_client))x21hook::fatal.store(true);}}
                else if(!(accepted=exbox_native2d_present(gpu_client)))x21hook::fatal.store(true);
            }
#else
            try{accepted=gpu_audit.submit(*packet);}catch(const std::exception&e){log((std::string("GPU host audit refused: ")+e.what()).c_str());x21hook::available.store(false);}
#endif
        }else{
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
        if(native_present::active)accepted=native_present::adapter.submit(display_surface);
        else
#endif
        accepted=SDL_UpdateWindowSurface(window)==0;
        }
        if(accepted && !first_accepted){
            first_accepted=true;
            first_present_completed_ms.store(std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()),std::memory_order_release);
        }
        const double ms=1000.0*double(SDL_GetPerformanceCounter()-started)/double(SDL_GetPerformanceFrequency());
        guard.lock();
        ++frames; wait_total_ms+=ms; wait_max_ms=std::max(wait_max_ms,ms);
        if (diagnostic_logging() && frames%900==0 && reports<8) {
            ++reports;
            char line[160];
            std::snprintf(line,sizeof line,"present: %lu frames, flip wait avg %.1f ms max %.1f ms, %lu frames skipped while busy",
                          frames,wait_total_ms/double(frames),wait_max_ms,skipped);
            log(line);
        }
        busy=false;
        idle.notify_all();
    }
}
void frame() {
    std::unique_lock<std::mutex> guard(lock);
    if (busy) idle.wait_for(guard,std::chrono::milliseconds(4),[] {return !busy;});
    if (busy) {++skipped; return;} // still flipping the last one: the next frame goes instead
    gpu_pending=x21hook::ready;
    if(gpu_pending&&x21hook::audit_cpu()){gpu_pending->reference.resize(size_t(screen->w)*screen->h);for(int y=0;y<screen->h;y++)std::memcpy(gpu_pending->reference.data()+size_t(y)*screen->w,static_cast<uint8_t*>(screen->pixels)+y*screen->pitch,size_t(screen->w)*4);}
    if(!gpu_pending){if (screen->w==display_surface->w && screen->h==display_surface->h) SDL_UpperBlit(screen,nullptr,display_surface,nullptr);else SDL_UpperBlitScaled(screen,nullptr,display_surface,nullptr);}
    if (!worker) worker=new std::thread(run);
    busy=true;++queued;
    wake.notify_one();
}
// Before the window and SDL go away: let the current flip finish, then stop.
void finish() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stop=true;
    }
    wake.notify_one();
    if (worker && worker->joinable()) worker->join();
    delete worker;
    worker=nullptr;
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
    if(x21hook::fatal.load())catchReturnFromMain(45);
    if(native_present::active && !native_present::adapter.close())catchReturnFromMain(45);
#endif
}
}
// The turning Ring of Light, centred on (cx, cy).
void draw_ring(int cx, int cy, int size, Uint32 now, float alpha) {
    SDL_Surface *frame=ring_frames[(now/42u)%24u];
    if (!frame || alpha<=0) {if (alpha>0) guide::spinner(cx,cy); return;}
    SDL_Rect r{cx-size/2,cy-size/2,size,size};
    SDL_SetSurfaceAlphaMod(frame,Uint8(255*std::min(1.0f,alpha)));
    SDL_BlitScaled(frame,nullptr,screen,&r);
}
// Overlays fade (and Details and the Controller Map grow a little) in and out.
Fade info_fade{false,0,220,170}, details_fade{false,0,180,140}, map_fade{false,0,200,150}, viewer_fade{false,0,220,170};
void overlay(Fade &fade, bool shown, Uint32 now, void (*paint)(), float grow) {
    fade.track(shown,now);
    const float v=fade.value(now);
    if (v<=0) return;
    if (v>=1) {paint(); return;}
    if (!begin_layer()) {if (shown) paint(); return;}
    paint();
    const float e=ease_out(v);
    end_layer(e,fade.on?1-grow*(1-e):1-grow*0.5f*(1-e));
}
Uint32 overlay_now=0;
void paint_info() {draw_info();}
void paint_details() {draw_details();}
void paint_map() {draw_controller_map(overlay_now);}
void paint_viewer() {
    draw_viewer();
    if (model.viewer==dash::Viewer::Pictures && !model.viewer_full && model.picture_background_confirm)
        draw_picture_background_confirmation();
}
// Starting a title: black closes in, the ring turns, the name shows.
void draw_launch(Uint32 now) {
    if (launching.live) {
        const float e=ease_out(progress(now,launching.at,kLaunchFade));
        shade(0,0,W,H,0x000000,Uint8(255*e));
        draw_ring(W/2,H/2-40,150,now,e);
        const std::string name=fit(launching.title,regular,1200);
        text(name,(W-text_width(name,regular))/2,H/2+70,regular,{Uint8(220*e),Uint8(232*e),Uint8(220*e),255});
    } else if (return_at && now-return_at<450) {
        // Coming back (or the start failed): black lifts again.
        shade(0,0,W,H,0x000000,Uint8(255*(1-ease_out(progress(now,return_at,450)))));
    }
}
// The 360 starting up: the orb glows in on black inside the turning ring,
// then the dashboard comes up behind it.
void draw_boot(Uint32 now) {
    if (!booting) return;
    const Uint32 t=now-boot_at;
    if (t>=kBoot) {booting=false; return;}
    const float black=t<kBoot-600?1:1-ease_out(float(t-(kBoot-600))/600);
    shade(0,0,W,H,0x000000,Uint8(255*black));
    float orb=t<300?0:ease_out(float(t-300)/800);
    if (t>kBoot-900) orb*=1-progress(now,boot_at+kBoot-900,450);
    if (orb<=0) return;
    const int cy=H/2-50;
    const float grow=0.7f+0.3f*ease_out(progress(now,boot_at+300,900));
    if (glow) {
        const int g=int(620*grow);
        SDL_Rect r{W/2-g/2,cy-g/2,g,g};
        SDL_SetSurfaceAlphaMod(glow,Uint8(70*orb));
        SDL_BlitScaled(glow,nullptr,screen,&r);
    }
    draw_ring(W/2,cy,int(250*grow),now,orb);
    if (SDL_Surface *g=button_image(xb::Guide)) {
        const int side=int(150*grow);
        SDL_Rect r{W/2-side/2,cy-side/2,side,side};
        SDL_SetSurfaceAlphaMod(g,Uint8(255*orb));
        SDL_BlitScaled(g,nullptr,screen,&r);
        SDL_SetSurfaceAlphaMod(g,255);
    }
    const std::string name="Ex-Box 360";
    const float words=orb*progress(now,boot_at+900,500);
    text(name,(W-text_width(name,large))/2,cy+190,large,{Uint8(230*words),Uint8(240*words),Uint8(230*words),255});
}
void draw(Uint32 now, float dt) {
    // Settled opaque video has no Home background to upload. Buffering and
    // viewer entry/exit fades retain the existing CPU fallback until migrated.
    const bool gpu_full_video=model.viewer==dash::Viewer::Videos&&model.viewer_full&&model.video_pending.empty()&&viewer_fade.on&&viewer_fade.value(now)>=1&&active_trailer>=0&&trailer_stream_ready()&&media_fade(now).video>=1;
    const bool gpu_pictures_settled=model.viewer==dash::Viewer::Pictures&&viewer_fade.on&&viewer_fade.value(now)>=1;
    const bool gpu_music_settled=model.viewer==dash::Viewer::Music&&viewer_fade.on&&viewer_fade.value(now)>=1;
    x21hook::ready.reset();x21hook::allow_frame=!x21hook::skip_frame&&x21hook::enabled()&&(model.era>=0&&model.era<=3)&&(model.viewer==dash::Viewer::None||gpu_full_video||gpu_pictures_settled||gpu_music_settled)&&!booting&&!launching.live&&dim_level==0&&(viewer_fade.value(now)==0||gpu_full_video||gpu_pictures_settled||gpu_music_settled);

    ++guide_scene::render_epoch;edited_avatar::refresh();
    ++frame_count;
#ifndef __PROSPERO__
    last_video_rect={};last_video_alpha=0;
#endif
    select_typeface(model.era);
    if (model.channel!=shown_channel) {
        if (shown_channel>=0 && !booting) {slide_from=shown_channel; slide_at=now; slide_position=position;}
        position=float(model.current());
        shown_channel=model.channel;
    }
    overlay_now=now;
    const bool modal=model.viewer!=dash::Viewer::None || model.info || model.details || model.guide || guide::closing || model.controller_map;
    static bool backdrop_valid=false,backdrop_gpu_only=false;
    static std::string backdrop_key;
    const std::string key=std::to_string(model.era)+":"+std::to_string(model.dashboard_build)+":"+std::to_string(model.channel)+":"+
        std::to_string(model.current())+":"+std::to_string(model.theme)+":"+std::to_string(gamerpic)+":"+std::to_string(model.picture_background)+":"+std::to_string(model.picture_background_enabled)+":"+edited_avatar::signature;
    const float picture_fade=picture_background_fade.value(now);
    // A settled full-screen media viewer begins with an opaque full-target fill.
    // Its cached hub stays available for the exit fade; copying it here is hidden work.
    const bool opaque_picture_grid=x21hook::allow_frame&&gpu_pictures_settled&&!model.viewer_full&&model.era>=1&&screen->clip_rect.x==0&&screen->clip_rect.y==0&&screen->clip_rect.w==screen->w&&screen->clip_rect.h==screen->h&&media_skin::opaque_background();
#ifndef __PROSPERO__
    if(const char*trace=getenv("NXE_GPU_ROUTE_TRACE")) {
        static unsigned previous=~0u;
        unsigned flags=(x21hook::allow_frame?1u:0u)|(x21hook::enabled()?2u:0u)|(gpu_pictures_settled?4u:0u)|(opaque_picture_grid?8u:0u)|(look3d::active()?16u:0u)|(edited_avatar::owner_still(avatar_slots::Image::FullBody)?32u:0u)|(model.viewer_full?64u:0u)|(screen->clip_rect.w==screen->w&&screen->clip_rect.h==screen->h?128u:0u);
        if(flags!=previous){previous=flags;if(FILE*f=fopen(trace,"a")){fprintf(f,"ms=%u era=%d viewer=%d flags=%u dim=%g clip=%d,%d,%d,%d bg=%p info=%d details=%d map=%d fade=%g\n",now,model.era,int(model.viewer),flags,double(dim_level),screen->clip_rect.x,screen->clip_rect.y,screen->clip_rect.w,screen->clip_rect.h,(void*)era_background,int(model.info),int(model.details),int(model.controller_map),double(viewer_fade.value(now)));fclose(f);}}
    }
#endif
    const bool covered=opaque_picture_grid||(model.viewer_full&&(model.viewer==dash::Viewer::Videos||model.viewer==dash::Viewer::Pictures)&&viewer_fade.on&&viewer_fade.value(now)>=1);
    if(!covered){
        const bool cached=modal&&hub_backdrop&&backdrop_valid&&key==backdrop_key&&(picture_fade<=0||picture_fade>=1);
        if(cached&&(x21hook::allow_frame?bool(gpu_overlay::backdrop):!backdrop_gpu_only)){
            if(x21hook::allow_frame)x21hook::ready=gpu_overlay::backdrop;
            if(!x21hook::allow_frame||x21hook::audit_cpu())SDL_BlitSurface(hub_backdrop,nullptr,screen,nullptr);
        }else{
            draw_hub(now);
            if(!gpu_home::finish()){backdrop_valid=false;backdrop_gpu_only=false;gpu_overlay::backdrop.reset();x21hook::skip_frame=true;draw(now,dt);x21hook::skip_frame=false;return;}
            gpu_overlay::backdrop=x21hook::allow_frame?x21hook::ready:nullptr;
            backdrop_gpu_only=x21hook::allow_frame&&!x21hook::audit_cpu()&&bool(gpu_overlay::backdrop);
            if(modal&&hub_backdrop&&!backdrop_gpu_only)SDL_BlitSurface(screen,nullptr,hub_backdrop,nullptr);
            backdrop_key=key;
        }
    }
    backdrop_valid=modal;
    if(x21hook::allow_frame&&covered)gpu_overlay::begin_opaque();
    else if(x21hook::allow_frame&&x21hook::ready)gpu_overlay::begin(x21hook::ready);
    overlay(viewer_fade,model.viewer!=dash::Viewer::None,now,paint_viewer,0.04f);
    overlay(info_fade,model.info,now,paint_info,0.06f);
    overlay(details_fade,model.details,now,paint_details,0.05f);
    guide::draw(now,dt);
    overlay(map_fade,model.controller_map,now,paint_map,0.03f);
    bool dark=model.controller_map || model.details || model.info ||
              (model.viewer!=dash::Viewer::None && model.viewer!=dash::Viewer::Avatar && model.viewer!=dash::Viewer::AvatarEdit);
    if (model.era==0) dark=true;
    if (model.era==3 && model.viewer==dash::Viewer::None && era_dark_footer) dark=true;
    // The Guide draws its own prompts under its panel.
    if (!model.guide && !guide::closing && !source_blades::owns_hints() && video_controls_shown(now)) draw_hints(1001,dark?SDL_Color{232,238,226,255}:SDL_Color{39,49,54,255});
    // Toasts wait for the boot, a launch and the dimmed screen.
    if (model.notification_level==0) {toast.live=false;toast_queue.clear();}
    if (!toast.live && !toast_queue.empty() && !booting && !launching.live && !suspended && dim_level<0.05f) {
        toast=toast_queue.front();
        toast_queue.pop_front();
        toast.at=now;
        toast.live=true;
        sfx::play(toast.sound);
    }
    draw_toast(now);
    if (dim_level>0) shade(0,0,W,H,0x000000,Uint8(175*std::min(1.0f,dim_level)));
    draw_launch(now);
    draw_boot(now);
    if(!gpu_overlay::finish(model.guide?1:guide::closing?2:model.details?3:model.controller_map?4:model.info?5:0,guide_scene::cpu_paints)||!gpu_home::finish()){backdrop_valid=false;backdrop_gpu_only=false;gpu_overlay::backdrop.reset();x21hook::skip_frame=true;draw(now,dt);x21hook::skip_frame=false;return;}
    present::frame();
}
// Starting a game or app: the screen fades to black round the turning Ring of
// Light with the transition sound, then the title starts.
void launch_now(Uint32 now) {
    const dash::Effect effect=launching.effect;
    launching.live=false;
    return_at=now;
    // Hand over without holding an audio port, and give the light bar back.
    // The music stops for the game, as on the 360.
    stop_trailer(); tunes::save_session(); tunes::save_activity(); tunes::stop(false); sfx::close();
    light::hand_back();
    bool ok=false;
    std::string key;
    if (effect.command==dash::Command::Launch && effect.app>=0 && effect.app<int(model.apps.size())) {
        const dash::App app=model.apps[size_t(effect.app)];
        key=app.executable;
        ok=nxe::launch_homebrew({app.title,app.executable});
    } else if (effect.command==dash::Command::LaunchTitle && effect.app>=0 && effect.app<int(model.games.size())) {
        const dash::App game=model.games[size_t(effect.app)];
        key=dash::Model::game_key(game);
        ok=launch_title(game);
    } else if (effect.command==dash::Command::LaunchSystem && effect.app>=0 && effect.app<dash::kSystemAppCount) {
        // A PS5 system app, started the same way as a game.
        const dash::SystemApp &sys=dash::kSystemApps[effect.app];
        ok=launch_title({sys.title,"",sys.title_id,""});
    }
    if (ok) {
        // Most recently played first from now on.
        if (!key.empty()) model.played(key);
        save_recent();
        save_settings();
        suspended=true;
        return;
    }
    sfx::open();
    light::shown=-1;
    model.launch_failed(launching.title);
}
void apply(const dash::Effect &effect) {
    switch (effect.command) {
    case dash::Command::LaunchSystem:
    case dash::Command::Launch:
    case dash::Command::LaunchTitle: {
        const bool game=effect.command==dash::Command::LaunchTitle, sys=effect.command==dash::Command::LaunchSystem;
        const int count=sys?dash::kSystemAppCount:int(game?model.games.size():model.apps.size());
        if (effect.app<0 || effect.app>=count) break;
        launching.effect=effect;
        launching.title=sys?dash::kSystemApps[effect.app].title:game?model.games[size_t(effect.app)].title:model.apps[size_t(effect.app)].title;
        launching.at=SDL_GetTicks();
        launching.live=true;
        stop_trailer();
        break;
    }
    case dash::Command::Refresh: {
        refresh_apps(); refresh_games(); read_profile();
        auto count=[](size_t n, const char *one, const char *many) {return std::to_string(n)+" "+(n==1?one:many);};
        const std::string summary=count(model.apps.size(),"homebrew app","homebrew apps")+", "+
            count(model.games.size(),"game","games");
        show_toast("Refresh complete",summary,&sfx::done_clip,toast_downloads);
        break;
    }
    case dash::Command::SavePins: save_pins(); break;
    case dash::Command::SaveCategories: save_categories(); break;
    case dash::Command::SaveTrailers: save_trailer_choices(); break;
    case dash::Command::SaveArtwork: save_artwork_choices(); break;
    case dash::Command::RefreshMedia:
        stop_trailer();
        reset_video_thumbnails();
        load_trailers();
        load_trailer_choices();
        load_videos();
        refresh_games();
        show_toast("Media refreshed","Local covers and trailers reloaded.",&sfx::done_clip,toast_downloads);
        break;
    case dash::Command::RequestCover: {
        if (effect.app<0 || effect.app>=int(model.games.size())) break;
        const auto &game=model.games[size_t(effect.app)];
        const std::string path=fs_prefix()+"/data/nxe/media_requests.tsv";
        if (FILE *file=fopen(path.c_str(),"a")) {
            std::string title=game.title;
            for (char &ch:title) if (ch=='\t' || ch=='\n' || ch=='\r') ch=' ';
            fprintf(file,"%s\t%s\n",game.title_id.c_str(),title.c_str());
            fclose(file);
            show_toast("Cover requested","Run the PC media sync to download it.",&sfx::done_clip,toast_downloads);
        } else show_toast("Cover request failed","Could not write the media queue.",&sfx::toast_clip);
        break;
    }
    case dash::Command::PlayTrailer: {
        const dash::Card card=model.info_card();
        const int match=trailer_for(model.info_target.key,card.title);
        if (match<0) {
            show_toast("No trailer for this game","Choose a staged trailer first.",&sfx::toast_clip);
            break;
        }
        dwell_key="info:"+model.info_target.key;
        dwell_trailer=match;
        dwell_since=SDL_GetTicks()-kTrailerAt;
        start_trailer(match);
        break;
    }
    case dash::Command::ScanStorage:
        model.storage=ps5lib::storage_inventory(model.games,music::default_roots(),fs_prefix(),trailer_root);
        break;
    case dash::Command::ReadNetwork: network_worker.request(model,false);break;
    case dash::Command::TestNetwork: network_worker.request(model,true);break;
    case dash::Command::RecorderAction: recorder_worker.command(model,effect.app,SDL_GetTicks());break;
    case dash::Command::ApplyPictureBackground: apply_picture_background(); break;
    case dash::Command::ApplyInstalledTheme: request_installed_theme(effect.app); break;
    case dash::Command::ScanPictures: scan_pictures(); break;
    case dash::Command::VideoSeek: seek_trailer(Uint32(std::max(0,effect.app))); pause_trailer(model.video_paused); break;
    case dash::Command::VideoPause: pause_trailer(model.video_paused); break;
    case dash::Command::SaveVideos: save_videos(); break;
    case dash::Command::AvatarWave:
        if(prepared_avatar::emote(effect.app>=0?effect.app:0,SDL_GetTicks()))break;
        if(effect.app>0)break;
        if (look3d::active()) look3d::moment(exbox::dashboard_director::AvatarMoment::Selection,SDL_GetTicks()); else avatar::start_wave(SDL_GetTicks());
        break;
    case dash::Command::OpenAvatarEditor: {
        std::string reason;
        if(!editor_dashboard::prepare(edited_avatar::data(),model,std::time(nullptr),reason)) {show_toast("Avatar Editor",reason,&sfx::toast_clip);break;}
        stop_trailer();tunes::save_session();tunes::save_activity();tunes::stop(false);sfx::close();light::hand_back();
        if(nxe::launch_homebrew({"Avatar Editor",editor_dashboard::title_id})) {suspended=true;return_at=SDL_GetTicks();}
        else {editor_dashboard::abandon(edited_avatar::data());sfx::open();show_toast("Avatar Editor","Could not open the installed editor.",&sfx::toast_clip);}
        break;
    }
    case dash::Command::AvatarEditStart: look3d::start_edit(); break;
    case dash::Command::AvatarChange: look3d::change(model.avatar_category,effect.app); break;
    case dash::Command::AvatarRandom: look3d::randomize(SDL_GetTicks()); break;
    case dash::Command::AvatarEmote: look3d::emote(SDL_GetTicks()); break;
    case dash::Command::AvatarSave:
        look3d::keep();
        look3d::moment(exbox::dashboard_director::AvatarMoment::EditorSave,SDL_GetTicks());
        show_toast("Avatar saved","Your avatar is now your gamerpic too.",&sfx::toast_clip,nullptr);
        break;
    case dash::Command::AvatarCancel: look3d::cancel(); break;
    case dash::Command::ScanMusic: tunes::scan(); break;
    case dash::Command::ScanGameMusic:
        save_settings();
        tunes::scan();
        show_toast("Game soundtrack scan","Accessible folders: WAV, MP3, FLAC and OGG. Containers and unsupported audio stay untouched.",&sfx::done_clip,toast_downloads);
        break;
    case dash::Command::MusicBrowse: tunes::browse(); break;
    case dash::Command::MusicPlay:
        if (!tunes::play(effect.app))
            if (const music::Song *song=tunes::row_song(effect.app))
                show_toast("Can't play this song",song->title,&sfx::toast_clip);
        break;
    case dash::Command::MusicPause: music_player.set_paused(model.music_paused); break;
    case dash::Command::MusicNext: tunes::locked([] {music_player.next();}); break;
    case dash::Command::MusicPrevious: tunes::locked([] {music_player.previous();}); break;
    case dash::Command::MusicStop: tunes::stop(); break;
    case dash::Command::MusicEdit: tunes::edit(dash::MusicOp(effect.app),model.music_target); break;
    case dash::Command::MusicShuffle:
    case dash::Command::MusicRepeat: tunes::set_order(); save_settings(); break;
    case dash::Command::MusicVolume:
        sfx::music_mix_volume.store((std::clamp(effect.app,0,10)*256+5)/10,std::memory_order_relaxed);
        save_settings();
        break;
    case dash::Command::GuideMusicRefresh: tunes::guide_refresh(); break;
    case dash::Command::GuideMusicPlay: tunes::guide_play(effect.app); break;
    case dash::Command::SaveRenderScale:
        if(render_preference::save(sysdata::local_path("/data/nxe/render-scale.txt"),effect.app))persisted_render_scale=effect.app;
        else {model.render_scale=persisted_render_scale;show_toast("Display","Could not save resolution. Your previous setting is unchanged.",&sfx::toast_clip);}
        break;
    case dash::Command::SaveSettings: refresh_update_art();save_settings(); break;
    case dash::Command::SaveAdProfiles: save_ad_profiles();load_promotions();save_settings();break;
    case dash::Command::ReloadPromotions: load_promotions();save_settings();break;
    case dash::Command::ExitToPs5: nxe::suspend_relaunch(); running=false; break;
    case dash::Command::NextGamerpic: next_gamerpic(); break;
    case dash::Command::None: break;
    }
}
bool restore_editor_return() {
    if(!edited_avatar::consume())return false;
    look3d::invalidate_preparation();
    const auto now=std::time(nullptr);
    const auto route=editor_dashboard::screen_state(model,now);
    // Rebuild data behind the returned screen, then restore its cursor. This
    // also handles a Guide opened over a media or storage page before editing.
    if(model.viewer==dash::Viewer::Music)tunes::scan();
    if(model.viewer==dash::Viewer::Pictures)scan_pictures();
    if(model.viewer==dash::Viewer::SystemSettings && model.storage_page())
        model.storage=ps5lib::storage_inventory(model.games,music::default_roots(),fs_prefix(),trailer_root);
    if(model.viewer==dash::Viewer::SystemSettings && model.network_page())network_worker.request(model,false);
    (void)editor_dashboard::restore_state(route,model,now);
    refresh_update_art();load_promotions();look3d::moment(exbox::dashboard_director::AvatarMoment::EditorSave,SDL_GetTicks());return true;
}
void exit_to_home() {
    log("L3+R3: exit to PS5 Home");
    nxe::suspend_relaunch();
    running=false;
}
// One press (or repeat) through the model, with the sound the 360 made for it.
void press(xb::Button button) {
    static bool first_navigation=false;
    if(!first_navigation && xb::repeats(button)){first_navigation=true;startup_mark("first navigation dispatched");}
    custom_scene::press(button,SDL_GetTicks());
    const dash::Model before=model;
    const dash::Effect effect=model.press(button);
    if(effect.command==dash::Command::OpenAvatarEditor)model=before; // Return to the exact pre-launch screen.
    const dash::Sound sound=dash::sound_for(before,model,button,effect);
    apply(effect);
    // Details changes announce themselves the way the 360 did.
    const std::string &name=model.info_target.title;
    if (effect.command==dash::Command::SavePins && model.pins.size()!=before.pins.size())
        show_toast(model.pins.size()>before.pins.size()?"Attached to My Xbox":"Detached from My Xbox",name,&sfx::toast_clip);
    if (effect.command==dash::Command::SaveCategories)
        show_toast(model.game_apps.size()>before.game_apps.size()?"Now in Games":"Now in Apps",name,&sfx::toast_clip);
    if (model.theme!=before.theme)
        show_toast("Theme changed",dash::kThemes[std::clamp(model.theme,0,dash::kThemeCount-1)],&sfx::toast_clip,toast_themes);
    if (sound!=dash::Sound::None && !(sound==dash::Sound::BladeSwitch && source_blades::owns_switch_sound())) sfx::play(sound);
    if (model.guide && !before.guide) light::flash(SDL_GetTicks());
}
// A new press of an Xbox button: act on it and start its repeat.
void button_down(xb::Button button, Uint32 now, const char *source) {
    if (button>=xb::Count) return;
    held|=xb::bit(button);
    flashed[button]=now;
    // A message box open: say what arrived and anything that holds it back,
    // so a press that does nothing shows up in the log.
    if (model.details || model.info) {
        char line[200];
        std::snprintf(line,sizeof line,"input: %s -> Xbox %s with %s open%s%s%s%s",source,xb::name(button),model.details?"a message box":"details",
                      launching.live?", launching":"",booting?", booting":"",suspended?", suspended":"",dim_level>0.05f?", dimmed":"");
        log(line);
    }
    const Uint32 idle=now-last_input_at;
    last_input_at=now;
    if (xb::exit_chord(held)) {exit_to_home();return;} // L3 + R3 always leaves
    if (launching.live) return;
    if (booting) {
        // Any button skips ahead to the dashboard.
        if (now-boot_at<kBoot-500) boot_at=now-(kBoot-500);
        return;
    }
    if (suspended) {
        restore_editor_return();
        // Back from a game: sounds, the light and any new trophies.
        suspended=false;
        sfx::open();
        light::shown=-1;
        return_at=now;
        check_trophies(now);
        return;
    }
    // The press that wakes a dimmed screen only wakes it.
    if (dim_level>0.05f || (model.dimming && idle>=idle_limit)) return;
    last_input=std::string(source)+"  ->  Xbox "+xb::name(button);

    if (xb::repeats(button)) repeat.press(button,now); else repeat.clear();
    press(button);
}
void button_up(xb::Button button) {
    if (button>=xb::Count) return;
    held&=~xb::bit(button);
    repeat.release(button);
}
void analog(Uint32 now) {
    xb::Button before=stick.held();
    xb::Button after=stick.update(stick_x,stick_y);
    if (after==before) return;
    if (before!=xb::None) button_up(before);
    if (after!=xb::None) button_down(after,now,"Left stick");
}
void open_controllers() {
    for (int i=0;i<SDL_NumJoysticks();i++)
        if (SDL_IsGameController(i) && !SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i)))
            if (SDL_GameController *pad=SDL_GameControllerOpen(i)) light::pads.push_back(pad);
    light::shown=-1; // colour the new pad too
}
bool native_controller_polling() {
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
    return native_present::active;
#else
    return false;
#endif
}
// SDL's dummy video driver does not own the PS5 event pump. The native
// presenter therefore snapshots the already-open SDL controller each frame
// and feeds the exact same Xbox button/stick graph as ordinary SDL events.
// Event-driven mode remains unchanged and the two sources are never mixed.
void poll_native_controller(Uint32 now) {
    if(!native_controller_polling())return;
    static Uint32 physical=0,next_discovery=0;
    SDL_GameControllerUpdate();
    SDL_GameController* pad=nullptr;
    for(auto* candidate:light::pads)if(candidate&&SDL_GameControllerGetAttached(candidate)){pad=candidate;break;}
    if(!pad){
        if(Sint32(now-next_discovery)>=0){open_controllers();next_discovery=now+1000;}
        for(int i=0;i<xb::Count;++i)if(physical&xb::bit(xb::Button(i)))button_up(xb::Button(i));
        physical=0;stick_x=stick_y=0;analog(now);
        int edge=left_trigger.update(0);if(edge<0)button_up(xb::LT);
        edge=right_trigger.update(0);if(edge<0)button_up(xb::RT);
        return;
    }
    Uint32 current=0;
    for(int index=0;index<=SDL_CONTROLLER_BUTTON_DPAD_RIGHT;++index){
        const xb::Button button=xb::from_sdl_button(index);
        if(button!=xb::None&&SDL_GameControllerGetButton(pad,SDL_GameControllerButton(index)))current|=xb::bit(button);
    }
    const Uint32 changed=physical^current;
    static unsigned transition_reports=0;
    for(int i=0;i<xb::Count;++i){const auto button=xb::Button(i);if(!(changed&xb::bit(button)))continue;if(transition_reports<32){char line[112];std::snprintf(line,sizeof line,"native pad transition: Xbox %s %s",xb::name(button),(current&xb::bit(button))?"down":"up");log(line);++transition_reports;}if(current&xb::bit(button))button_down(button,now,"Native controller poll");else button_up(button);}
    physical=current;
    auto axis=[&](SDL_GameControllerAxis id){return float(SDL_GameControllerGetAxis(pad,id))/32767.0f;};
    stick_x=axis(SDL_CONTROLLER_AXIS_LEFTX);stick_y=axis(SDL_CONTROLLER_AXIS_LEFTY);analog(now);
    int edge=left_trigger.update(axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT));if(edge>0)button_down(xb::LT,now,"Native L2 poll");else if(edge<0)button_up(xb::LT);
    edge=right_trigger.update(axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT));if(edge>0)button_down(xb::RT,now,"Native R2 poll");else if(edge<0)button_up(xb::RT);
}
void handle(const SDL_Event &event, Uint32 now) {
    switch (event.type) {
    case SDL_QUIT: running=false; break;
    case SDL_CONTROLLERDEVICEADDED:
        open_controllers();
        // A pad connected later announces itself, as on the 360.
        if (light::pads.size()>pads_seen && now-boot_at>3000)
            show_toast("Controller connected","Player "+std::to_string(light::pads.size()),&sfx::toast_clip,toast_controller);
        pads_seen=light::pads.size();
        break;
    case SDL_CONTROLLERBUTTONDOWN: {
        if(native_controller_polling())break;
        const int index=event.cbutton.button;
        xb::Button button=xb::from_sdl_button(index);
        if (index>=0 && index<32 && !(logged_sdl_buttons&(1u<<index))) {
            logged_sdl_buttons|=1u<<index;
            char line[96];
            std::snprintf(line,sizeof line,"pad: SDL button %d reads as Xbox %s",index,xb::name(button));
            log(line);
        }
        if (button==xb::None) break;
        char source[48];
        std::snprintf(source,sizeof source,"%s (SDL %d)",xb::ps5_name(button),index);
        button_down(button,now,source);
        break;
    }
    case SDL_CONTROLLERBUTTONUP: if(!native_controller_polling())button_up(xb::from_sdl_button(event.cbutton.button)); break;
    case SDL_CONTROLLERAXISMOTION: {
        if(native_controller_polling())break;
        const float value=float(event.caxis.value)/32767.0f;
        switch (event.caxis.axis) {
        case xb::sdl::kAxisTriggerLeft: {
            int edge=left_trigger.update(value);
            if (edge>0) button_down(xb::LT,now,"L2"); else if (edge<0) button_up(xb::LT);
            break;
        }
        case xb::sdl::kAxisTriggerRight: {
            int edge=right_trigger.update(value);
            if (edge>0) button_down(xb::RT,now,"R2"); else if (edge<0) button_up(xb::RT);
            break;
        }
        case xb::sdl::kAxisLeftX: stick_x=value; analog(now); break;
        case xb::sdl::kAxisLeftY: stick_y=value; analog(now); break;
        default: break;
        }
        break;
    }
    case SDL_KEYDOWN:
        if (!event.key.repeat) button_down(xb::from_key(event.key.keysym.sym),now,"Keyboard");
        break;
    case SDL_KEYUP: button_up(xb::from_key(event.key.keysym.sym)); break;
    default: break;
    }
}
SDL_Surface *trim_button_padding(SDL_Surface *art) {
    if (!art) return nullptr;
    SDL_Surface *rgba=SDL_ConvertSurfaceFormat(art,SDL_PIXELFORMAT_ARGB8888,0);
    if (!rgba) return art;
    int left=rgba->w, top=rgba->h, right=-1, bottom=-1;
    SDL_LockSurface(rgba);
    for (int y=0;y<rgba->h;y++) {
        const auto *row=reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(rgba->pixels)+y*rgba->pitch);
        for (int x=0;x<rgba->w;x++) if ((row[x]>>24)>12) {
            left=std::min(left,x);right=std::max(right,x);
            top=std::min(top,y);bottom=std::max(bottom,y);
        }
    }
    SDL_UnlockSurface(rgba);
    if (right<left || bottom<top) {SDL_FreeSurface(rgba);return art;}
    const int side=std::max(right-left+1,bottom-top+1);
    SDL_Surface *out=SDL_CreateRGBSurfaceWithFormat(0,side,side,32,SDL_PIXELFORMAT_ARGB8888);
    if (out) {
        SDL_FillRect(out,nullptr,0);
        SDL_Rect src{left,top,right-left+1,bottom-top+1};
        SDL_Rect dst{(side-src.w)/2,(side-src.h)/2,src.w,src.h};
        SDL_SetSurfaceBlendMode(rgba,SDL_BLENDMODE_NONE);
        SDL_BlitSurface(rgba,&src,out,&dst);
        SDL_SetSurfaceBlendMode(out,SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(art);
    }
    SDL_FreeSurface(rgba);
    return out?out:art;
}
SDL_Surface *make_metro_symbol(int kind) {
    SDL_Surface *out=render_density::create(256,256,SDL_PIXELFORMAT_ARGB8888);
    if (!out) return nullptr;
    const float density=float(render_density::density(out));
    const float pin[][2]={{40,18},{88,18},{88,30},{78,38},{78,65},{94,78},{94,88},{68,88},{64,118},{60,88},{34,88},{34,78},{50,65},{50,38},{40,30}};
    for (int y=0;y<out->h;y++) {
        auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(out->pixels)+y*out->pitch);
        for (int x=0;x<out->w;x++) {
            float alpha=0;
            for (int sy=0;sy<2;sy++) for (int sx=0;sx<2;sx++) {
                const float px=(x+(sx+0.5f)/2)/(2*density),py=(y+(sy+0.5f)/2)/(2*density);
                auto segment=[&](float ax,float ay,float bx,float by,float width) {
                    const float t=std::clamp(((px-ax)*(bx-ax)+(py-ay)*(by-ay))/((bx-ax)*(bx-ax)+(by-ay)*(by-ay)),0.0f,1.0f);
                    const float dx=px-ax-t*(bx-ax),dy=py-ay-t*(by-ay);
                    return dx*dx+dy*dy<=width*width;
                };
                bool ink=false;
                if (kind==0) {
                    const float rx=(px-64)*0.70710678f+(py-64)*0.70710678f+64;
                    const float ry=-(px-64)*0.70710678f+(py-64)*0.70710678f+64;
                    for (int i=0,j=14;i<15;j=i++)
                        if ((pin[i][1]>ry)!=(pin[j][1]>ry) && rx<(pin[j][0]-pin[i][0])*(ry-pin[i][1])/(pin[j][1]-pin[i][1])+pin[i][0]) ink=!ink;
                } else {
                    const float cx=kind==1?50:64,cy=cx,r=kind==1?29:46;
                    const float distance=std::sqrt((px-cx)*(px-cx)+(py-cy)*(py-cy));
                    ink=std::abs(distance-r)<(kind==1?5:5.5f);
                    if (kind==1) ink=ink||segment(72,72,106,106,6);
                    else ink=ink||segment(64,35,64,64,4)||segment(64,64,90,64,4);
                }
                if (ink) alpha+=0.25f;
            }
            row[x]=(Uint32(255*alpha)<<24)|0x00ffffffu;
        }
    }
    SDL_SetSurfaceBlendMode(out,SDL_BLENDMODE_BLEND);
    return out;
}
int library_art_build=-1;
int library_build() {
    return model.era==0?(model.dashboard_build>=5759?6770:0):
           model.era==1?9199:model.era==2?13604:model.metro_v1()?15574:17559;
}
SDL_Surface *library_art(const char *relative,std::string *resolved=nullptr) {
    const int build=library_build();
    const bool era=build!=0 && build!=9199;
    const std::string base=std::string(root)+"private_library/";
    const std::string era_path="eras/"+std::to_string(build)+"/"+relative;
    SDL_Surface *image=nullptr;
    auto read=[&](const std::string &path) {
        image=load_png(path);
        if (image && resolved) *resolved=path;
    };
    if (resolved) resolved->clear();
    if (era) read(base+era_path);
#ifndef __PROSPERO__
    // Desktop overrides also accept the staged eras/<build>/<module> tree.
    const char *retail=getenv("NXE_RETAIL_LIBRARY");
    if (!image && era && retail) read(std::string(retail)+"/"+era_path);
#endif
    if (!image) read(base+relative);
#ifndef __PROSPERO__
    if (!image) {
        const bool shared=std::strncmp(relative,"shared/",7)==0;
        if (const char *dir=shared?getenv("NXE_SHARED_LIBRARY"):retail)
            read(std::string(dir)+"/"+(shared?relative+7:relative));
    }
#endif
    if (image) SDL_SetSurfaceBlendMode(image,SDL_BLENDMODE_BLEND);
    return image;
}
void load_library_art() {
    // dash.xex dashcomm/music, 6770/9199/13604/15574/17559; names also read by build_era_library.py.
    clear_scaled_art();
    auto replace=[](SDL_Surface *&surface,const char *name) {
        SDL_FreeSurface(surface);surface=library_art(name);
    };
    // arcade/AchievementImg (9199); xam secret/unearned from T04's per-build packs.
    replace(achievement_art[0],"arcade/AchievementImg.png");
    const int trophy_build=notify_scene::selected_build();
    const char *trophy_era=trophy_build<=6770?"blades":trophy_build==9199?"nxe":
        trophy_build==13604?"kinect":trophy_build==15574?"metro1":"metro";
    std::string trophy_root=std::string(root)+"era-skins/";
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) trophy_root=std::string(override)+"/";
#endif
    const std::string trophy_dir=trophy_root+trophy_era+"/notify-"+std::to_string(trophy_build)+"/";
    const char *trophy_names[]={"unearnedAchievement.png","secretAchievement.png"};
    for (int i=1;i<3;++i) {
        SDL_FreeSurface(achievement_art[i]);achievement_art[i]=load_png(trophy_dir+trophy_names[i-1]);
        if (achievement_art[i]) SDL_SetSurfaceBlendMode(achievement_art[i],SDL_BLENDMODE_BLEND);
    }
    // memory/Categories and DeviceSelector, each build's own icons (T02 loader).
    const char *memory[]={"memory/ico_64x_HD.png","dashcomm/ico_96x_USB.png",
        "dashcomm/ico_64x_games.png","memory/ico_64x_profiles.png","memory/ico_64x_Demos.png",
        "memory/ico_64x_Trailers.png","memory/ico_64x_skins.png","shared/ico_64x_Gamertiles.png",
        "memory/ico_64x_AvatarAssets.png","memory/ico_32x_SystemItems.png","memory/ico_64x_music.png",
        "dashcomm/ico_64x_AllDevices.png"};
    for (int i=0;i<12;++i) replace(storage_art[i],memory[i]);
    // NetworkMain/TestingScene ConnectBar; refresh_update_art reloads on era changes.
    const char *network[]={"network/console_64x.png","network/network_64x.png","network/internet_64x.png",
        "network/wireless_64x.png","network/livelogo_hor.png","network/img_Link.png","network/img_unLink.png",
        "network/img_Act.png","network/WirelessStrength0-white_32.png","network/WirelessStrength1-white_32.png",
        "network/WirelessStrength2-white_32.png","network/WirelessStrength3-white_32.png","network/WirelessStrength4-white_32.png"};
    for (int i=0;i<13;++i) replace(network_art[i],network[i]);
    replace(xbox_logo,"dashcomm/xboxLogo.png");
    const char *controls[]={"dashcomm/btn_32x_Play.png","dashcomm/btn_32x_Last.png","dashcomm/btn_32x_Stop.png",
                            "dashcomm/btn_32x_Next.png","dashcomm/btn_32x_Pause.png","dashcomm/btn_32x_Volume.png"};
    for (int i=0;i<6;i++) replace(music_controls[i],controls[i]);
    // dvd/XmfPadPlayback (9199/13604); Metro moves five icons to dashcomm.
    const char *video[]={"dvd/ico_32x_play_black.png","dvd/ico_32x_pause_black.png",
        "dvd/ico_32x_last.png","dvd/ico_32x_RR.png","dvd/ico_32x_stop.png",
        "dvd/ico_32x_FF.png","dvd/ico_32x_next.png"};
    for (int i=0;i<7;i++) replace(video_controls[i],video[i]);
    // pictures/slideshowControls.xur, 9199/13604/15574/17559.
    const char *pictures[]={"pictures/ico_32x_Play_black.png","pictures/ico_32x_pause_black.png",
        "pictures/ico_32x_last.png","pictures/ico_32x_stop.png","pictures/ico_32x_next.png",
        "pictures/ico_32x_Shuffle.png","pictures/ico_32x_shuffle_Off.png",
        "pictures/ico_32x_Loop.png","pictures/ico_32x_Loop_Off.png",
        "pictures/ico_32x_Rotate.png","pictures/ico_32x_RotateCC.png"};
    for (int i=0;i<11;i++) replace(picture_controls[i],pictures[i]);
    replace(music_order_art[0],"music/bt_shuffle.png");   // parallel arrows: in order
    replace(music_order_art[1],"music/bt_normal.png");    // crossed arrows: shuffled
    const char *kinds[]={"music/ico_32x_SongFile.png","music/ico_96x_Music.png","music/ico_32x_Artist.png","music/ico_32x_Genre.png",
                         "music/ico_32x_Playlist.png"};
    for (int i=0;i<5;i++) replace(music_kind_art[i],kinds[i]);
    const char *blades_sources[]={"dashcomm/ico_96x_OpticalMedia.png","dashcomm/ico_64x_games.png",
        "dashcomm/ico_96x_MusicDisk.png","dashcomm/ico_96x_Camera.png","shared/ico_64x_Console.png",
        "dashcomm/ico_96x_Video.png","dashcomm/AvatarSilhouette.png","shared/ico_32x_Friend.png","shared/ico_32x_Mail.png"};
    for (int i=0;i<dash::IconCount;i++) {
        SDL_FreeSurface(blades_icons[i]);
        blades_icons[i]=library_art(blades_sources[i],&blades_icon_paths[i]);
        blades_native[i]=blades_icons[i]!=nullptr;
        if (!blades_native[i] && card_icons[i]) blades_icons[i]=cool_blades_copy(card_icons[i]);
        if (blades_icons[i]) SDL_SetSurfaceBlendMode(blades_icons[i],SDL_BLENDMODE_BLEND);
    }
    library_art_build=library_build();
}
#include "runtime/welcome_resources.inc.hpp"
#include "assets/update_art.inc.hpp"
void load_art() {
    // Touch the fixed output backing before the first interactive frame.
    video_output_backing.prepare(screen->w,screen->h);
    std::string blades_root=std::string(root)+"era-skins/blades";
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) blades_root=std::string(override)+"/blades";
#endif
    era_chrome::prepare(blades_root.c_str(),chrome_startup_deferred);
    xbox_live_logo=load_png(blades_root+"/live-logo.png");
    if (xbox_live_logo) SDL_SetSurfaceBlendMode(xbox_live_logo,SDL_BLENDMODE_BLEND);
    for (const char *name:{"ico_32x_systemSet","ico_32x_parentalControl","ico_32x_Profiles","ico_32x_network","ico_32x_console","ico_64x_controller","ico_64x_DefaultPicture","ico_64x_Themes"})
        if (SDL_Surface *icon=load_png(blades_root+"/system/"+name+".png")) {
            SDL_Surface *argb=SDL_ConvertSurfaceFormat(icon,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(icon);
            if (argb) {SDL_SetSurfaceBlendMode(argb,SDL_BLENDMODE_BLEND);blades_system_icons[name]=argb;}
        }
    // The Blades Guide's own pieces (1888 shared/dashcomm): legend orbs, the
    // community icons, music buttons and the XBOX 360 splash (orb + wordmark).
    for (const char *name:{"A-Button","B-Button","X-Button","Y-Button","disabled-Button","ico_32x_Mail","ico_32x_Friend","ico_32x_Players",
                           "btn_32x_Play","btn_32x_Last","btn_32x_Next","btn_32x_Pause","btn_32x_Stop","splash_360"})
        if (SDL_Surface *art=load_png(blades_root+"/guide/"+name+".png")) {
            SDL_Surface *argb=SDL_ConvertSurfaceFormat(art,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(art);
            if (argb) {SDL_SetSurfaceBlendMode(argb,SDL_BLENDMODE_BLEND);blades_system_icons[std::string("guide/")+name]=argb;}
        }
    if ((nxe_panel=load_png(blades_root+"/../nxe/BackgroundPanel.png"))) SDL_SetSurfaceBlendMode(nxe_panel,SDL_BLENDMODE_BLEND);
    // The Metro Guide's home rows (17559 shared / dashcomm).
    for (const char *name:{"ico_32x_Friend","ico_32x_StartParty","ico_32x_Mail","ico_32x_Beacons","ico_64x_Chat","ico_32x_OpticalMedia","ico_32x_controller"})
        if (SDL_Surface *art=load_png(blades_root+"/../metro/guide/"+name+".png")) {
            SDL_Surface *argb=SDL_ConvertSurfaceFormat(art,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(art);
            if (argb) {SDL_SetSurfaceBlendMode(argb,SDL_BLENDMODE_BLEND);blades_system_icons[std::string("metro/")+name]=argb;}
        }
    // The Kinect home's message / friends counters (13604 shared).
    for (const char *name:{"ico_32x_Mail","ico_32x_Friend"})
        if (SDL_Surface *art=load_png(blades_root+"/../kinect/"+name+".png")) {
            SDL_Surface *argb=SDL_ConvertSurfaceFormat(art,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(art);
            if (argb) {SDL_SetSurfaceBlendMode(argb,SDL_BLENDMODE_BLEND);blades_system_icons[std::string("kinect/")+name]=argb;}
        }
    // The 1888 blades' own row icons (gamesbla, mediabla, live, dashmain, dashcomm).
    for (const char *name:{"ico_32x_message","ico_32x_Friend","ico_32x_marketplace","ico_32x_achievements_white","ico_32x_myGames",
                           "ico_32x_arcade","ico_32x_demos","ico_32x_trailers","ico_32x_MusicDisk","ico_32x_DigitalCamera",
                           "ico_64x_TrailersFolder_white","ico_32x_MCX","ico_32x_console","ico_32x_parentalControl","ico_32x_MU",
                           "ico_32x_network","ico_32x_PC","ico_32x_systemSet","ico_32x_Eject","ico_64x_xboxlive","ico_64x_friend",
                           "ico_64x_AllDevices","ico_64x_HD","ico_64x_music","ico_64x_skins","ico_64x_Trailers","ico_64x_profiles"})
        if (SDL_Surface *art=load_png(blades_root+"/items/"+name+".png")) {
            SDL_Surface *argb=SDL_ConvertSurfaceFormat(art,SDL_PIXELFORMAT_ARGB8888,0);SDL_FreeSurface(art);
            if (argb) {SDL_SetSurfaceBlendMode(argb,SDL_BLENDMODE_BLEND);blades_system_icons[std::string("items/")+name]=argb;}
        }
    blades_star=load_png(blades_root+"/gamercard/Star.png");
    blades_gscore=load_png(blades_root+"/gamercard/GScore.png");
    for (SDL_Surface **glyph:{&blades_star,&blades_gscore}) if (*glyph) {
        // Paletted / grey-alpha originals become ARGB so they scale and tint cleanly.
        SDL_Surface *argb=SDL_ConvertSurfaceFormat(*glyph,SDL_PIXELFORMAT_ARGB8888,0);
        SDL_FreeSurface(*glyph);*glyph=argb;
        if (*glyph) SDL_SetSurfaceBlendMode(*glyph,SDL_BLENDMODE_BLEND);
    }
    // Rep stars as the gamercard draws them: gold with a darker rim when earned,
    // a pale translucent star when not. Drawn once, 4x4 supersampled.
    SDL_FreeSurface(blades_star);blades_star=make_rep_star(false);blades_star_full=make_rep_star(true);
    blades_disc=make_soft_disc();
    const char *blade_names[]={"marketplace","xbox live","games","media","system"};
    for (int family=0;family<3;family++) {
        for (int selected=0;selected<2;selected++) {
            for (int i=0;i<5;i++) {
                auto *flat=blades_type::rail_surface(blade_names[i],blades_rail[family][selected]);
                blades_labels[family][selected][i]=rotated_surface(blades_type::ink_copy(flat));
                SDL_FreeSurface(flat);
            }
        }
    }
#ifndef __PROSPERO__
    blades_type::audit_rails(getenv("NXE_RAIL_AUDIT"));
    blades_type::audit_rails(getenv("NXE_RAIL_REGIME_AUDIT"),true);
#endif
    toast_segments=render_density::create(128,128,SDL_PIXELFORMAT_ARGB8888);
    if (toast_segments) {
        const float density=float(render_density::density(toast_segments));
        for (int y=0;y<toast_segments->h;y++) for (int x=0;x<toast_segments->w;x++) {
            const float dx=(x+0.5f)/density-64,dy=(y+0.5f)/density-64,d=std::sqrt(dx*dx+dy*dy);
            float angle=std::atan2(dy,dx)*180.0f/3.14159265f+90;
            if (angle<0) angle+=360;
            const int quadrant=int(angle/90);
            const float within=angle-quadrant*90;
            const float radial=std::clamp(std::min(d-50.5f,63.5f-d)+0.5f,0.0f,1.0f);
            const float angular=std::clamp(std::min(within-7.5f,82.5f-within)*0.017453293f*d+0.5f,0.0f,1.0f);
            auto *row=reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(toast_segments->pixels)+y*toast_segments->pitch);
            row[x]=(Uint32(255*radial*angular)<<24)|(quadrant==3?0x8fd41c:0x767a7d);
        }
        SDL_SetSurfaceBlendMode(toast_segments,SDL_BLENDMODE_BLEND);
    }
    for (int i=0;i<3;i++) metro_symbols[i]=make_metro_symbol(i);
    {
        const std::string legend_dir=blades_root.substr(0,blades_root.size()-6)+"metro-legend/";
        const char *names[4]={"Legend_A.png","Legend_B.png","Legend_X.png","Legend_Y.png"};
        for (int i=0;i<4;i++) if ((metro_legend[i]=load_png(legend_dir+names[i]))) SDL_SetSurfaceBlendMode(metro_legend[i],SDL_BLENDMODE_BLEND);
    }
    // Xbox button glyphs: one glossy 128px set for every button (shiny/), so the
    // prompts match. Installs without shiny/ fall back to the retail legends,
    // then Kenney's CC0 set.
    struct Source { xb::Button button; const char *shiny, *retail, *open; };
    const Source sources[]={
        {xb::A,"a.png","legend_a.png","a.png"},{xb::B,"b.png","legend_b.png","b.png"},
        {xb::X,"x.png","legend_x.png","x.png"},{xb::Y,"y.png","legend_y.png","y.png"},
        {xb::Guide,"guide.png","legend_menu.png","guide.png"},{xb::Start,"start.png",nullptr,"menu.png"},
        {xb::Back,"back.png",nullptr,"view.png"},{xb::LB,"lb.png",nullptr,"lb.png"},{xb::RB,"rb.png",nullptr,"rb.png"},
        {xb::LT,"lt.png",nullptr,"lt.png"},{xb::RT,"rt.png",nullptr,"rt.png"},{xb::LS,"ls.png",nullptr,"ls.png"},
        {xb::RS,"rs.png",nullptr,"rs.png"},{xb::Up,"dpad.png",nullptr,"dpad.png"},
    };
    for (const auto &s:sources) {
        SDL_Surface *image=nullptr;
        // The Guide button is the user's own glossy Xbox orb when it is there.
        if (s.button==xb::Guide) image=load_png(std::string(root)+"private_art/guide_orb.png");
        if (!image) image=load_png(std::string(root)+"shiny/"+s.shiny);
        if (!image && s.retail) image=load_png(std::string(root)+"private_art/"+s.retail);
        if (!image) image=load_png(std::string(root)+"kenney/"+s.open);
        glyphs[s.button]=image;
        if (s.retail) retail_glyphs[s.button]=trim_button_padding(load_png(std::string(root)+"private_art/"+s.retail));
        simple_glyphs[s.button]=load_png(std::string(root)+"kenney/"+s.open);
    }
    {
        std::string skins=std::string(root)+"era-skins/";
#ifndef __PROSPERO__
        if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) skins=std::string(override)+"/";
#endif
        const char *folders[]={"kinect/guide-13604/","metro1/guide-15574/","metro/guide-17559/"};
        // huduiskin/skin.xur, 13604/15574/17559: PasscodeMessageBox and NuiPasscodeMessageBox.
        // BackHandle/back.png and NuiHudHoverBack/ico_32x_Back.png are navigation arrows.
        struct ControllerSource { xb::Button button; const char *file; };
        const ControllerSource controller_sources[]={
            {xb::LB,"lb_32x.png"},{xb::RB,"rb_32x.png"},{xb::LT,"lt_32x.png"},
            {xb::RT,"rt_32x.png"},{xb::Up,"dpad_32x.png"},
        };
        for (int f=0;f<3;f++) for (const auto &s:controller_sources) {
            SDL_Surface *art=trim_button_padding(load_png(skins+folders[f]+s.file));
            if (art) SDL_SetSurfaceBlendMode(art,SDL_BLENDMODE_BLEND);
            era_controller_glyphs[f][s.button]=art;
        }
    }
    for (xb::Button d:{xb::Down,xb::Left,xb::Right}) glyphs[d]=nullptr;
    constexpr const char *retail_cards[5]={"tray.png","games.png","music.png","photos.png","videos.png"};
    for (int i=0;i<5;i++) {
        SDL_Surface *art=load_png(std::string(root)+"private_art/"+retail_cards[i]);
        bool private_art=art!=nullptr;
        if (!art) art=load_png(std::string(root)+"cards/"+std::to_string(i+1)+".png");
        card_art[i]=art;
        if (art) {
            SDL_SetSurfaceBlendMode(art,SDL_BLENDMODE_BLEND);
            SDL_SetSurfaceAlphaMod(art,private_art?255:72);
        }
    }
    constexpr const char *icon_names[6]={"card_disc.png","card_games.png","card_music.png","card_photos.png","card_settings.png","card_video.png"};
    for (int i=0;i<6;i++) card_icons[i]=load_png(std::string(root)+"private_art/"+icon_names[i]);
    for (int i=dash::IconAvatar;i<dash::IconCount;i++) card_icons[i]=make_social_icon(i);
    load_library_art();
    avatar_art=load_png(std::string(root)+"private_art/avatar.png");
    disc_art=load_png(std::string(root)+"private_art/disc.png");
    theater_popcorn_art=load_png(std::string(root)+"private_art/theater/popcorn.png");
    if(theater_popcorn_art)SDL_SetSurfaceBlendMode(theater_popcorn_art,SDL_BLENDMODE_BLEND);
    for (int i=0;i<64;i++) {
        char name[48]; std::snprintf(name,sizeof name,"private_art/gamerpic_%02d.png",i);
        SDL_Surface *pic=load_png(std::string(root)+name);
        if (!pic) break;
        gamerpics.push_back(pic);
    }
    if (FILE *f=fopen(gamerpic_path,"r")) {int n=-1; if (fscanf(f,"%d",&n)==1 && n>=0) gamerpic=n; fclose(f);}
    if (!gamerpics.empty()) gamerpic%=int(gamerpics.size());
    gamerscore_icon=load_png(std::string(root)+"private_art/gamerscore.png");
    blade_dark=load_png(std::string(root)+"private_art/guide_blade_dark.png");
    trophy_icon=load_png(std::string(root)+"private_art/trophy.png");
    toast_trophy=load_png(std::string(root)+"private_art/toast_trophy.png");
    toast_downloads=load_png(std::string(root)+"private_art/toast_downloads.png");
    toast_controller=load_png(std::string(root)+"private_art/toast_controller.png");
    toast_themes=load_png(std::string(root)+"private_art/toast_themes.png");
    disc_spin=load_png(std::string(root)+"private_art/disc_spin.png");
    blade_grey=load_png(std::string(root)+"private_art/guide_blade_grey.png");
    ring_of_light=load_png(std::string(root)+"private_art/guide_ring.png");
    for (int i=0;i<24;i++) ring_frames[i]=rotated(ring_of_light,float(i)*15.0f,160);
    glow=make_glow(128);
    if(glow){const int widths[]={900,760,1100},heights[]={330,280,260};
        SDL_SetSurfaceBlendMode(glow,SDL_BLENDMODE_NONE);SDL_SetSurfaceAlphaMod(glow,255);
        for(size_t i=0;i<atmosphere_glow.size();++i){auto* frame=render_density::create(widths[i],heights[i]);if(frame){SDL_Rect to{0,0,frame->w,frame->h};SDL_UpperBlitScaled(glow,nullptr,frame,&to);SDL_SetSurfaceBlendMode(frame,SDL_BLENDMODE_BLEND);surface_generations::seal(frame);}atmosphere_glow[i]=frame;}
        SDL_SetSurfaceBlendMode(glow,SDL_BLENDMODE_BLEND);
    }
    kinect_glow=make_glow(128,0x40f4ff);
    // The NXE dashboard's horizon glow (controlp Aura_Horizon.png). Optional.
    aura=load_png(std::string(root)+"private_art/aura_horizon.png");
    if (aura) SDL_SetSurfaceBlendMode(aura,SDL_BLENDMODE_BLEND);
    shade_pixel=SDL_CreateRGBSurfaceWithFormat(0,1,1,32,SDL_PIXELFORMAT_ARGB8888);
    if (shade_pixel) SDL_SetSurfaceBlendMode(shade_pixel,SDL_BLENDMODE_BLEND);
}
void free_art() {
    chrome_startup_job.join();
    // Output wrappers borrow backing pixels, so release the wrapper first.
    if(trailer_scaled){SDL_FreeSurface(trailer_scaled);trailer_scaled=nullptr;}
    scaled_video_opaque=false;video_output_backing.clear();
    for(auto*& frame:atmosphere_glow){SDL_FreeSurface(frame);frame=nullptr;}
    era_chrome::free_all();
    source_blades::clear();
    guide_scene::clear();
    notify_scene::clear();
    SDL_FreeSurface(dashboard_title);dashboard_title=nullptr;
    for (auto &family:blades_labels) {
        for (auto &state:family) {
            for (auto *surface:state) SDL_FreeSurface(surface);
        }
    }
    SDL_FreeSurface(xbox_logo);
    SDL_FreeSurface(xbox_live_logo);
    for (auto &entry:blades_system_icons) SDL_FreeSurface(entry.second);
    blades_system_icons.clear();
    SDL_FreeSurface(blades_star);SDL_FreeSurface(blades_star_full);SDL_FreeSurface(blades_gscore);SDL_FreeSurface(blades_disc);blades_star=blades_star_full=blades_gscore=blades_disc=nullptr;
    for (auto *s:music_controls) SDL_FreeSurface(s);
    for (auto *s:video_controls) SDL_FreeSurface(s);
    for (auto *s:picture_controls) SDL_FreeSurface(s);
    SDL_FreeSurface(picture_backdrop);
    for (auto *s:music_order_art) SDL_FreeSurface(s);
    for (auto *s:music_kind_art) SDL_FreeSurface(s);
    clear_scaled_art();
    for (auto *symbol:metro_symbols) SDL_FreeSurface(symbol);
    for (auto *&legend:metro_legend) {SDL_FreeSurface(legend);legend=nullptr;}
    if (toast_segments) SDL_FreeSurface(toast_segments);
    for (int i=0;i<xb::Count;i++) if (glyphs[i]) SDL_FreeSurface(glyphs[i]);
    for (int i=0;i<xb::Count;i++) {
        if (retail_glyphs[i]) SDL_FreeSurface(retail_glyphs[i]);
        if (simple_glyphs[i]) SDL_FreeSurface(simple_glyphs[i]);
    }
    for (auto &family:era_controller_glyphs) for (auto *&art:family) {SDL_FreeSurface(art);art=nullptr;}
    for (auto *art:card_art) if (art) SDL_FreeSurface(art);
    for (auto *art:era_icons) SDL_FreeSurface(art);
    SDL_FreeSurface(era_panel);SDL_FreeSurface(era_floor);SDL_FreeSurface(era_pin);SDL_FreeSurface(era_recent);
    SDL_FreeSurface(era_background);SDL_FreeSurface(era_aura_floor);
    SDL_FreeSurface(era_banner);
    for (auto *icon:card_icons) if (icon) SDL_FreeSurface(icon);
    for (auto *icon:blades_icons) if (icon) SDL_FreeSurface(icon);
    for (SDL_Surface *s:{avatar_art,disc_art,theater_popcorn_art,shade_pixel,background,gamerscore_icon,blade_dark,blade_grey,ring_of_light,trophy_icon,disc_spin}) if (s) SDL_FreeSurface(s);
    for (SDL_Surface *s:gamerpics) SDL_FreeSurface(s);
    for (SDL_Surface *&s:ring_frames) if (s) {SDL_FreeSurface(s);s=nullptr;}
    for (SDL_Surface *s:{glow,kinect_glow,aura,themed_background,fx_layer,toast_trophy,toast_downloads,toast_controller,toast_themes}) if (s) SDL_FreeSurface(s);
    if(installed_theme_background){render_density::free_surface(installed_theme_background);installed_theme_background=nullptr;installed_theme_pixels.reset();}
    if(installed_theme_previous){render_density::free_surface(installed_theme_previous);installed_theme_previous=nullptr;installed_theme_previous_pixels.reset();}
    if(installed_theme_preview){render_density::free_surface(installed_theme_preview);installed_theme_preview=nullptr;installed_theme_preview_pixels.reset();}
    for (SDL_Surface *s:achievement_art) if (s) SDL_FreeSurface(s);
    for (SDL_Surface *s:storage_art) if (s) SDL_FreeSurface(s);
    for (SDL_Surface *s:network_art) if (s) SDL_FreeSurface(s);
    for (SDL_Surface *s:themed_cards) if (s) SDL_FreeSurface(s);
    for (SDL_Surface *s:themed_icons) if (s) SDL_FreeSurface(s);
}
// The signed-in PS5 profile's name titles the Guide's Home blade.
// Memory's "%s free" for the console's own drive (dashboard data lives on /data).
std::string storage_free() {
    struct statvfs info{};
#ifdef __PROSPERO__
    const char *where="/data";
#else
    const char *where="/";
#endif
    if (statvfs(where,&info)!=0) return "";
    const double bytes=double(info.f_bavail)*double(info.f_frsize);
    char text[48];
    if (bytes>=1e9) std::snprintf(text,sizeof text,"%.1f GB free",bytes/1073741824.0);
    else std::snprintf(text,sizeof text,"%.0f MB free",bytes/1048576.0);
    return text;
}
std::string profile_name() {
#ifdef __PROSPERO__
    (void)sceUserServiceInitialize(nullptr); // already running under SDL's pad driver: harmless
    int user=-1;
    char name[32]{};
    if (sceUserServiceGetInitialUser(&user)==0) user_id=user;
    if (user_id>=0 && sceUserServiceGetUserName(user,name,sizeof name-1)==0 && name[0])
        return name;
    // Launched as a payload there is no initial user; the system's own user
    // list ends with the last signed-in user (after 16 slots), and each home
    // holds that user's name.
    if (FILE *f=fopen(sysdata::local_path("/system_data/priv/home/user_list.dat").c_str(),"rb")) {
        Uint32 slots[18]{};
        const size_t got=fread(slots,4,18,f);fclose(f);
        Uint32 last=got>16?slots[16]:0;
        bool listed=false;
        for (size_t i=0;i<16 && i<got;i++) listed=listed || (slots[i]==last && last && last!=0xffffffffu);
        if (!listed) for (size_t i=0;i<16 && i<got;i++) if (slots[i] && slots[i]!=0xffffffffu) {last=slots[i];listed=true;break;}
        if (listed) {
            if (sysdata::companion) {
                user_id=int(last);
                if (sceUserServiceGetUserName(user_id,name,sizeof name-1)==0 && name[0]) return name;
            }
            char path[64];std::snprintf(path,sizeof path,"/user/home/%x/username.dat",unsigned(last));
            if (FILE *u=fopen(path,"rb")) {
                const size_t n=fread(name,1,sizeof name-1,u);fclose(u);name[n]=0;
                if (name[0]) {std::snprintf(path,sizeof path,"profile from the system's user list: 0x%08X",unsigned(last));log(path);return name;}
            }
        }
    }
    log("profile name unavailable; the Guide says Player");
    return "Player";
#else
    if (const char *uid=getenv("NXE_USER_ID")) user_id=int(std::strtoul(uid,nullptr,16));
    const char *name=getenv("NXE_PLAYER");
    return name && *name?name:"Player";
#endif
}
#ifndef __PROSPERO__
void audit_art_pixels(const char *path) {
    FILE *report=fopen(path,"w");
    if (!report) return;
    fprintf(report,"asset\tsource_width\tsource_height\tloaded_width\tloaded_height\tchanged_pixels\tidentity_resample_changes\n");
    auto check=[&](const std::string &name,SDL_Surface *loaded,const std::string &path) {
        SDL_Surface *source=load_art(path);
        if (!source || !loaded) {fprintf(report,"%s\tMISSING\n",name.c_str());if(source)SDL_FreeSurface(source);return;}
        SDL_Surface *a=SDL_ConvertSurfaceFormat(source,SDL_PIXELFORMAT_ARGB8888,0);
        SDL_Surface *b=SDL_ConvertSurfaceFormat(loaded,SDL_PIXELFORMAT_ARGB8888,0);
        SDL_Surface *identity=resample_art(source,{0,0,source->w,source->h},source->w,source->h);
        long changed=0,identity_changed=0;
        if (a && b && identity) {
            if (a->w!=b->w || a->h!=b->h) changed=-1;
            for (int y=0;y<a->h;y++) {
                const auto *ar=reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(a->pixels)+y*a->pitch);
                const auto *ir=reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(identity->pixels)+y*identity->pitch);
                const auto *br=y<b->h?reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(b->pixels)+y*b->pitch):nullptr;
                for (int x=0;x<a->w;x++) {
                    if (changed>=0 && ar[x]!=br[x]) ++changed;
                    if (ar[x]!=ir[x]) ++identity_changed;
                }
            }
        }
        fprintf(report,"%s\t%d\t%d\t%d\t%d\t%ld\t%ld\n",name.c_str(),source->w,source->h,loaded->w,loaded->h,changed,identity_changed);
        SDL_FreeSurface(a);SDL_FreeSurface(b);SDL_FreeSurface(identity);SDL_FreeSurface(source);
    };
    const char *cards[]={"tray.png","games.png","music.png","photos.png","videos.png"};
    for (int i=0;i<5;i++) check(cards[i],card_art[i],std::string(root)+"private_art/"+cards[i]);
    const char *icons[]={"card_disc.png","card_games.png","card_music.png","card_photos.png","card_settings.png","card_video.png"};
    for (int i=0;i<6;i++) check(icons[i],card_icons[i],std::string(root)+"private_art/"+icons[i]);
    for (int i=0;i<dash::IconCount;i++) if (blades_native[i])
        check("blades-icon-"+std::to_string(i),blades_icons[i],blades_icon_paths[i]);
    for (auto item:{std::pair<xb::Button,const char *>{xb::A,"a.png"},{xb::B,"b.png"},{xb::X,"x.png"},{xb::Y,"y.png"},
                   {xb::Start,"start.png"},{xb::Back,"back.png"},{xb::LB,"lb.png"},{xb::RB,"rb.png"},
                   {xb::LT,"lt.png"},{xb::RT,"rt.png"},{xb::LS,"ls.png"},{xb::RS,"rs.png"},{xb::Up,"dpad.png"}})
        check(item.second,glyphs[item.first],std::string(root)+"shiny/"+item.second);
    for (auto item:{std::pair<const char *,SDL_Surface *>{"guide_orb.png",glyphs[xb::Guide]},
                   {"guide_blade_dark.png",blade_dark},{"guide_blade_grey.png",blade_grey},{"guide_ring.png",ring_of_light},
                   {"gamerscore.png",gamerscore_icon},{"disc_spin.png",disc_spin},{"toast_trophy.png",toast_trophy},
                   {"toast_downloads.png",toast_downloads},{"toast_controller.png",toast_controller},{"toast_themes.png",toast_themes}})
        check(item.first,item.second,std::string(root)+"private_art/"+item.first);
    for (const auto &game:model.games) if (!game.icon.empty()) check(game.title_id,title_icon(game.icon),game.icon);
    std::string directory=std::string(root)+"era-skins/";
    if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) directory=std::string(override)+"/";
    if (model.era==0) {
        const std::string material_dir=directory+"blades/materials/";
        for (int i=0;i<2;i++) check("material-row-"+std::to_string(i),era_chrome::row_material[i],material_dir+(i?"row-focus":"row-normal")+".png");
        check("material-profile",era_chrome::profile_material,material_dir+"profile.png");
        check("material-row-shine",era_chrome::row_shine,material_dir+"row-shine.png");
        int active=0;while (active<4 && dash::kBladesChannels[active]!=model.channel) active++;
        const int early=model.dashboard_build<5759 && active>0?1:0;
        const std::string prefix="tray-"+std::to_string(early?4:5)+"-"+std::to_string(active);
        for (int i=0;i<2;i++) check("material-tray-"+std::to_string(i),era_chrome::tray_material[early][active][i],material_dir+prefix+(i?"-focus":"-normal")+".png");
        check("material-tray-shine",era_chrome::tray_shine[early][active],material_dir+prefix+"-shine.png");
    }
    directory+=std::to_string(model.dashboard_build)+"/";
    for (int i=0;i<dash::IconCount;i++) if (era_icons[i])
        check("category-"+std::to_string(i),era_icons[i],directory+"category-"+std::to_string(i)+".png");
    if (era_background) check("era-background",era_background,directory+"background.jpg");
    if (era_panel) check("era-panel",era_panel,directory+"panel-gradient.png");
    if (era_aura_floor) check("era-aura-floor",era_aura_floor,directory+"aura-floor.png");
    fclose(report);
}
#endif
// NXE_SCRIPT="Up,Right,A,snap:out.bmp" (desktop), or /data/nxe/exbox-script.txt
// on the PS5 (run once, then renamed .done): presses Xbox buttons one per frame,
// saves the frame at each snap, then quits.
std::vector<std::string> script;
size_t script_at=0;
Uint32 script_wait_until=0;
bool script_music_waiting=false;
Uint32 script_music_wait_at=0;
bool script_step(Uint32 now) {
    if (script_wait_until && static_cast<Sint32>(now-script_wait_until)<0) return true;
    script_wait_until=0;
    if (script_at>=script.size()) return false;
    const std::string token=script[script_at++];
    if (token.rfind("era:",0)==0) {model.era=std::clamp(atoi(token.c_str()+4),0,3);if(model.era==3)model.select_metro_tab(1);return true;}
    if (token.rfind("build:",0)==0) {model.dashboard_build=atoi(token.c_str()+6);refresh_update_art();return true;}
    if (token.rfind("channel:",0)==0) {
        model.channel=std::clamp(atoi(token.c_str()+8),0,dash::ChannelCount-1);
        if (model.era==3) {constexpr int tabs[]={2,5,3,6,1,1};model.select_metro_tab(tabs[model.channel]);}
        model.selected[model.channel]=std::clamp(model.current(),0,std::max(0,model.count()-1));return true;
    }
#ifndef __PROSPERO__
    if (token.rfind("video-osd-audit:",0)==0) {
        rect(0,0,W,H,0x303030);draw_video_controls(1,3,61000,180000,false);
        SDL_SaveBMP(screen,token.substr(16).c_str());return true;
    }
    if (token.rfind("picture-osd-audit:",0)==0) {
        rect(0,0,W,H,0x303030);draw_picture_controls(dash::PictureFile{},1,3,false,1.0f);
        SDL_SaveBMP(screen,token.substr(18).c_str());return true;
    }
    if (token.rfind("theater-guests:",0)==0) {theater_layout::guests=std::clamp(atoi(token.c_str()+15),0,theater_layout::kMaxGuests);return true;}
    if (token.rfind("theater-audit:",0)==0) {
        model.video_theater=true;draw_theater_room(now,0.0f);
        SDL_SaveBMP(screen,token.substr(14).c_str());return true;
    }
#endif
    if (token.rfind("xui-frame:",0)==0) {source_blades::seek(float(atof(token.c_str()+10)));return true;}
    if (token.rfind("xui-play:",0)==0) {source_blades::play(token.substr(9));return true;}
    if (token.rfind("xui-loop:",0)==0) {source_blades::loop(float(atof(token.c_str()+9)));return true;}
    if (token.rfind("xui-state:",0)==0) {source_blades::report(token.substr(10).c_str());return true;}
#ifndef __PROSPERO__
    if (token.rfind("reflection-audit:",0)==0) {nxe_reflection::audit_path=token.substr(17);return true;}
#endif
#ifndef __PROSPERO__
    if (token.rfind("pixels:",0)==0) {audit_art_pixels(token.substr(7).c_str());return true;}
#endif
    if (token.rfind("promos:",0)==0) {model.promotions=atoi(token.c_str()+7)!=0;model.selected[model.channel]=std::clamp(model.current(),0,model.count()-1);if(model.era==3)model.metro_focus=model.current()%model.metro_page_size();return true;}
    if (token=="system") {model.system_page=0;model.viewer=dash::Viewer::SystemSettings;model.viewer_item=0;return true;}
    if (token=="music") {apply(model.perform_guide_entry({"Music",dash::GuideAction::Music}));return true;}
    if (token=="videos") {apply(model.perform_guide_entry({"Videos",dash::GuideAction::Videos}));return true;}
    if (token=="pictures") {apply(model.perform_guide_entry({"Pictures",dash::GuideAction::Pictures}));return true;}
#ifndef __PROSPERO__
    if(token=="themes"){model.open_themes();return true;}
    if(token.rfind("settings-audit:",0)==0){
        const std::string page=token.substr(15);
        model.viewer=dash::Viewer::SystemSettings;model.viewer_item=0;model.system_history.clear();
        model.system_page=page=="account"?dash::Model::SystemAccount:
                          page=="console"?dash::Model::SystemConsole:
                          page=="display"?dash::Model::SystemDisplay:
                          page=="audio"?dash::Model::SystemAudio:
                          page=="info"?dash::Model::SystemInfo:
                          page=="music-volume"?dash::Model::SystemMusicVolume:dash::Model::SystemRoot;
        if(model.system_page==dash::Model::SystemMusicVolume)model.viewer_item=std::clamp(model.music_volume,0,10);
        return true;
    }
    if(token.rfind("recorder-audit:",0)==0){
        const std::string state=token.substr(15);
        model.guide=true;model.guide_host=false;model.guide_blade=dash::BladeMedia;model.guide_music_page=0;
        model.recorder_available=true;model.recorder_recording=state!="idle";model.recorder_paused=state=="paused";
        model.recorder_pause_supported=true;model.recorder_seconds=65.0;model.recorder_control=model.recorder_paused?0:model.recorder_recording?1:0;
        const auto rows=model.blade_entries(dash::BladeMedia);for(std::size_t i=0;i<rows.size();++i)if(rows[i].action==dash::GuideAction::RecordScreen){model.guide_item[dash::BladeMedia]=int(i);break;}
        return true;
    }
    if(token.rfind("guide-audit:",0)==0){
        const std::string page=token.substr(12);
        model.guide=true;model.guide_host=false;model.guide_music_page=0;
        model.guide_blade=page=="home"?dash::BladeHome:
                          page=="games"?dash::BladeGames:
                          page=="media"||page=="music"?dash::BladeMedia:dash::BladeSettings;
        if(page=="music")model.guide_music_page=5;
        model.guide_item[model.guide_blade]=0;
        return true;
    }
    if(token.rfind("music-dashboard-audit:",0)==0){
        // Make this deterministic even when the normal asynchronous startup
        // scan is still in flight: it must not publish over the fixture below.
        tunes::scan_job.join();
        const std::string folder=token.substr(22);
        music_songs={{folder+"/album-a/track.mp3","His World","Zebrahead","Sonic the Hedgehog (2006)","Game Soundtrack"},
                     {folder+"/album-b/track.mp3","Still Alive","Lisa Miskovsky","Mirror's Edge","Soundtrack"},
                     {folder+"/album-c/track.mp3","Ezio's Family","Jesper Kyd","Assassin's Creed II","Soundtrack"}};
        std::vector<std::string> paths;for(const auto&song:music_songs)paths.push_back(song.path);
        music_activity.observe(paths);music_activity.played(paths[1]);music_activity.played(paths[1]);music_activity.played(paths[0]);
        model.era=3;model.dashboard_build=17559;model.channel=dash::Media;
        model.select_metro_tab(5);model.metro_focus=0;model.metro_selected[5]=0;
        return true;
    }
#endif
    if (token.rfind("play:",0)==0) {
        if(music_songs.empty() && !script_music_waiting){tunes::scan();script_music_waiting=true;script_music_wait_at=now;}
        if(script_music_waiting && tunes::scan_job.active()){
            if(now-script_music_wait_at>=10000){log("script: music readiness timed out");return false;}
            --script_at;return true;
        }
        script_music_waiting=false;
        apply({dash::Command::MusicPlay,atoi(token.c_str()+5)});return true;
    }
    if (token.rfind("tab:",0)==0) {model.select_metro_tab(atoi(token.c_str()+4));return true;}
    if (token.rfind("theme:",0)==0) {model.load_theme(atoi(token.c_str()+6));return true;}
#ifndef __PROSPERO__
    if(token.rfind("installed-theme:",0)==0){
        const std::string id=token.substr(16);
        for(std::size_t i=0;i<model.installed_themes.size();++i)if(model.installed_themes[i].stable_id==id){request_installed_theme(int(i));return true;}
        --script_at;return true; // catalog publication is asynchronous
    }
#endif
    if (token.rfind("notify:",0)==0) {model.notification_level=std::clamp(atoi(token.c_str()+7),0,3);return true;}
    if (token.rfind("buttons:",0)==0) {model.button_style=std::clamp(atoi(token.c_str()+8),0,2);return true;}
    if (token.rfind("seek:",0)==0 && active_trailer>=0) {trailer_started=now-Uint32(std::max(0,atoi(token.c_str()+5)));active_frame=-1;return true;}
    if (token=="toast") {model.notification_level=2;show_toast("Achievement unlocked","20 G - Welcome to Ex-Box 360",&sfx::toast_clip,toast_trophy);return true;}
    if (token.rfind("state:",0)==0) {
        if (FILE *f=fopen(token.substr(6).c_str(),"w")) {
            const auto card=model.current_card();
            const auto string=[f](const std::string &value) {
                fputc('"',f);
                for (unsigned char c:value) {
                    if (c=='"' || c=='\\') {fputc('\\',f);fputc(c,f);}
                    else if (c<32) fprintf(f,"\\u%04x",unsigned(c));
                    else fputc(c,f);
                }
                fputc('"',f);
            };
            fprintf(f,"{\"era\":%d,\"build\":%d,\"channel\":%d,\"tab\":%d,\"focus\":%d,\"selected\":%d,\"viewer\":%d,\"viewer_item\":%d,\"info\":%d,\"guide\":%d,\"guide_blade\":%d,\"promotions\":%d,\"count\":%d,\"promo_count\":%zu,\"content_focused\":%d,\"card_kind\":%d,\"card_goal\":%d,\"card_promo\":%d,\"active_trailer\":%d,\"active_frame\":%d,\"card_title\":",model.era,model.dashboard_build,model.channel,model.metro_tab,model.metro_focus,model.current(),int(model.viewer),model.viewer_item,model.info,model.guide,model.guide_blade,model.promotions,model.count(),model.promo_cards.size(),model.content_focused(),int(card.kind),int(card.goal),card.promo,active_trailer,active_frame);
            string(card.title);fputs(",\"card_preview\":",f);string(card.preview);
            const int profile=model.current_ad_profile();
            fprintf(f,",\"ad_profile\":%d,\"ad_mode\":%d,\"ad_shuffle_seed\":%u,\"ad_selected_count\":%zu,\"ad_catalog_count\":%zu,\"ad_selected\":[",profile,model.ad_mode[profile],model.ad_shuffle_seed[profile],model.ad_selected[profile].size(),model.ad_catalog.size());
            bool comma=false;for (const auto &id:model.ad_selected[profile]) {if (comma) fputc(',',f);string(id);comma=true;}
            fputs("],\"promo_images\":[",f);comma=false;
            for (const auto &promo:model.promo_cards) {if (comma) fputc(',',f);string(promo.image);comma=true;}
            fprintf(f,"],\"ad_show_all\":%d,\"ad_visible_count\":%d,\"viewer_count\":%d,\"ad_picked_catalog_index\":%d",model.ad_show_all,model.ad_visible_count(),model.viewer_count(),model.viewer==dash::Viewer::AdsSettings?model.ad_catalog_index(model.viewer_item-5):-1);
            fprintf(f,",\"blades_chrome_surfaces\":%d,\"blades_chrome_bytes\":%zu,\"blades_source_labels\":%d,\"blades_source_slots\":%d",era_chrome::retained_chrome_count(),era_chrome::retained_chrome_bytes(),int(era_chrome::has_source_labels(model.dashboard_build,source_blades::active_blade())),era_chrome::converted_slot_count(model.dashboard_build));
            size_t tagged_bytes=0;for(const auto& entry:render_density::surfaces)if(entry.first)tagged_bytes+=size_t(entry.first->pitch)*entry.first->h;
            fprintf(f,",\"render_width\":%d,\"render_height\":%d,\"render_scale\":%d,\"requested_render_scale\":%d,\"tagged_surface_bytes\":%zu",screen->w,screen->h,render_density::factor,persisted_render_scale,tagged_bytes);
            fprintf(f,",\"chrome_backing_failures\":%u,\"chrome_raster_fallbacks\":%u",unsigned(era_chrome::chrome_backing_failures),unsigned(era_chrome::chrome_raster_fallbacks));
            fprintf(f,",\"theme_preview_ready\":%d,\"theme_preview_id\":",installed_theme_preview&&theme_preview_published_id==theme_preview_focus_id);string(theme_preview_published_id);
            fprintf(f,",\"ticks\":%u}\n",SDL_GetTicks());
            fclose(f);
        }
        return true;
    }
#ifndef __PROSPERO__
    if (token.rfind("capture:",0)==0) {
        const std::string path=token.substr(8);
        SDL_SaveBMP(screen,(path+".bmp").c_str());
        if (trailer_image) IMG_SavePNG(trailer_image,(path+"-frame.png").c_str());
        if (FILE *f=fopen((path+"-video.json").c_str(),"w")) {
            fprintf(f,"{\"frame\":%d,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"alpha\":%.3f}\n",active_frame,last_video_rect.x,last_video_rect.y,last_video_rect.w,last_video_rect.h,last_video_alpha);
            fclose(f);
        }
        return true;
    }
#endif
    if (token.rfind("wait:",0)==0) {script_wait_until=now+Uint32(atoi(token.c_str()+5));return true;}
    if (token.rfind("snap:",0)==0) {SDL_SaveBMP(screen,token.substr(5).c_str());return true;}
    if (token.rfind("hold:",0)==0) {
        for (int b=0;b<xb::Count;b++) if (token.substr(5)==xb::name(xb::Button(b))) {held|=xb::bit(xb::Button(b));flashed[b]=now;last_input=std::string(xb::ps5_name(xb::Button(b)))+"  ->  Xbox "+xb::name(xb::Button(b));}
        return true;
    }
    for (int b=0;b<xb::Count;b++) if (token==xb::name(xb::Button(b))) {button_down(xb::Button(b),now,"Script");button_up(xb::Button(b));}
    return true;
}
}
int main() {
    DashboardLogSession log_session;
    startup_epoch=std::chrono::steady_clock::now();
    startup_mark("entry");
    log("Ex-Box 360 starting");
    begin_theme_catalog();
    persisted_render_scale=render_preference::load(sysdata::local_path("/data/nxe/render-scale.txt"));
    render_density::factor=persisted_render_scale;
#ifndef __PROSPERO__
    if(const char* value=getenv("NXE_RENDER_SCALE"))render_density::factor=std::strcmp(value,"2")==0?2:1;
#endif
#ifndef __PROSPERO__
    if (const char *local=getenv("NXE_TRAILER_ROOT")) trailer_root=local;
    if (const char *local=getenv("NXE_VIDEO_STATE_PATH")) video_state_path=local;
#endif
    if (const size_t budget=memory_budget()) {
        char line[96];
        std::snprintf(line,sizeof line,"memory budget: %zu MiB",budget>>20);
        log(line);
        if (budget>=(900u<<20)) icon_cache_limit=320; // ~80 MiB of icons
    }
    struct stat avatar_pack_info{};
    if (stat((std::string(root)+"private_art/avatar/kit360.nxak").c_str(),&avatar_pack_info)==0 &&
        avatar_pack_info.st_size>(400ll<<20)) {
        icon_cache_limit=48; // reserve RAM for the owner's full animation library
        log("large avatar kit: title icon cache capped at 48");
    }
    SDL_SetMainReady();
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
    // Covers font/window failures before the regular presenter cleanup path.
    struct NativeStartupGuard {~NativeStartupGuard(){if(native_present::active && !native_present::adapter.close())catchReturnFromMain(45);}} native_startup_guard;
#endif
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
    if (native_present::initialize() || TTF_Init())
#else
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER) || TTF_Init())
#endif
    {log(SDL_GetError());return 1;}
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
    window=native_present::active?SDL_CreateWindow("Ex-Box native surface",0,0,native_present::adapter.width(),native_present::adapter.height(),SDL_WINDOW_HIDDEN):SDL_CreateWindow("Ex-Box 360",0,0,W*render_density::factor,H*render_density::factor,SDL_WINDOW_SHOWN|SDL_WINDOW_FULLSCREEN_DESKTOP);
#else
    window=SDL_CreateWindow("Ex-Box 360",0,0,W*render_density::factor,H*render_density::factor,SDL_WINDOW_SHOWN|SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
    display_surface=window?SDL_GetWindowSurface(window):nullptr;
#ifdef __PROSPERO__
    if(render_density::factor==2&&display_surface&&(display_surface->w!=3840||display_surface->h!=2160)){
        render_density::factor=1;log("4K output unavailable; retained1080p renderer");
    }
#endif
    screen=display_surface?render_density::create(W,H,display_surface->format->format):nullptr;
    if(!screen&&display_surface&&render_density::factor==2){render_density::factor=1;screen=render_density::create(W,H,display_surface->format->format);log("4K canvas allocation failed; retained1080p renderer");}
    // The finished frame is copied, not blended, onto the window surface.
    if (screen) SDL_SetSurfaceBlendMode(screen,SDL_BLENDMODE_NONE);
    if (!screen) {log(SDL_GetError());return 2;}
    // The editor and dashboard share the same owner-authored launch picture.
    // A wrong-size or unreadable file never replaces the existing first frame.
    {
        std::string splash_path=sysdata::local_path("/data/nxe/loading/current.png");
#ifndef __PROSPERO__
        if(const char *test_path=getenv("NXE_SHARED_SPLASH"))splash_path=test_path;
#endif
        SDL_Surface *splash=IMG_Load(splash_path.c_str());
        if(!splash||splash->w!=W*render_density::factor||splash->h!=H*render_density::factor){
            if(splash)SDL_FreeSurface(splash);
            splash_path=sysdata::local_path(render_density::factor==2?
                "/data/nxe/loading/loading-2160p.png":"/data/nxe/loading/loading-1080p.png");
            splash=IMG_Load(splash_path.c_str());
        }
        if(splash){
            {char note[160];std::snprintf(note,sizeof note,"shared splash source=%dx%d display=%dx%d",splash->w,splash->h,display_surface->w,display_surface->h);log(note);}
            if(splash->w==W*render_density::factor&&splash->h==H*render_density::factor){
                SDL_Rect destination{0,0,display_surface->w,display_surface->h};
                if(display_surface->w*splash->h>display_surface->h*splash->w){
                    destination.w=display_surface->h*splash->w/splash->h;
                    destination.x=(display_surface->w-destination.w)/2;
                }else if(display_surface->w*splash->h<display_surface->h*splash->w){
                    destination.h=display_surface->w*splash->h/splash->w;
                    destination.y=(display_surface->h-destination.h)/2;
                }
                SDL_FillRect(display_surface,nullptr,SDL_MapRGB(display_surface->format,0,0,0));
                if(SDL_BlitScaled(splash,nullptr,display_surface,&destination)==0){
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
                if(native_present::active)native_present::adapter.submit(display_surface);
                else SDL_UpdateWindowSurface(window);
#else
                SDL_UpdateWindowSurface(window);
#endif
                startup_mark("shared editor splash shown");
                }else log("shared splash blit rejected; regular first frame retained");
            }else log("shared splash size rejected; regular first frame retained");
            SDL_FreeSurface(splash);
        }else log(("shared splash unavailable: "+splash_path).c_str());
    }
    hub_backdrop=render_density::create(W,H,screen->format->format);
    if (hub_backdrop) SDL_SetSurfaceBlendMode(hub_backdrop,SDL_BLENDMODE_NONE);
    library_base=render_density::create(W,H,screen->format->format);
    if (library_base) SDL_SetSurfaceBlendMode(library_base,SDL_BLENDMODE_NONE);
    log(render_density::factor==2?"render target:3840x2160; logical1920x1080; native-sized glyphs":"render target:1920x1080");
    {char line[128];std::snprintf(line,sizeof line,"render acceleration: solid=%d pixel=%d scaled=%d",int(solid_blend::compatible(screen->format->format)),int(solid_blend::format_ok(screen)&&pixel_blend::compatible(SDL_PIXELFORMAT_ARGB8888,screen->format->format)),int(solid_blend::format_ok(screen)&&scaled_blend::compatible(SDL_PIXELFORMAT_ARGB8888,screen->format->format)));log(line);}
    begin_chrome_startup();
    prepare_background();startup_mark("SDL surfaces ready");
    // The 360's own typeface, Convection, when a copy is installed (a licensed
    // font file, or XenonCLatin.xtt from the owner's own 360), else Inter.
    auto installed=[](std::initializer_list<const char *> names) -> std::string {
        for (const char *name:names) {
            std::string path=std::string(root)+"private_art/fonts/"+name;
#ifndef __PROSPERO__
            if (const char *dir=getenv("NXE_FONT_DIR")) path=std::string(dir)+"/"+name;
#endif
            struct stat st;
            if (stat(path.c_str(),&st)!=0 || st.st_size<=0) continue;
            // Only a file FreeType can actually open counts.
            if (TTF_Font *probe=TTF_OpenFont(path.c_str(),20)) {TTF_CloseFont(probe); return path;}
            log(("font: cannot open "+path+", trying the next").c_str());
        }
        return "";
    };
    std::string font_path=installed({"Convection.ttf","Convection.otf","Convection-Regular.ttf","Convection-Regular.otf","XenonCLatin.xtt"});
    std::string bold_path=installed({"Convection-Medium.ttf","Convection-Medium.otf","ConvectionMedium.ttf","Convection-Bold.ttf","Convection-Bold.otf"});
    if (!font_path.empty()) {
        log(("font: "+font_path).c_str());
        if (bold_path.empty()) bold_path=font_path;
    } else {
        font_path=std::string(root)+"Inter-Regular.ttf";
        bold_path=std::string(root)+"Inter-SemiBold.ttf";
    }
    regular=TTF_OpenFont(font_path.c_str(),29); large=TTF_OpenFont(font_path.c_str(),49); small=TTF_OpenFont(font_path.c_str(),22);
    era_text=TTF_OpenFont(font_path.c_str(),36);
    kinect_tag=TTF_OpenFont(font_path.c_str(),42); kinect_count=TTF_OpenFont(font_path.c_str(),34);
    blades_body=TTF_OpenFont(font_path.c_str(),40);
    blades_caption=TTF_OpenFont(font_path.c_str(),40);
    blades_heading=TTF_OpenFont(font_path.c_str(),49);
    {const int sizes[5]={62,42,34,29,25};for (int i=0;i<5;i++) {nxe_channel[i]=TTF_OpenFont(font_path.c_str(),sizes[i]);}}
    // Recovered rails use 23 pt, then 21 pt, then 18 pt and selected scale 1.1.
    // Native pixel sizes are reference calibrated for these separate regimes;
    // baked chrome must not receive the source host viewport a second time.
    constexpr int representative_builds[]={1888,5766,6717};
    for (int family=0;family<3;family++) {
        for (int selected=0;selected<2;selected++) {
            const int pixels=blades_rails::native_pixels(representative_builds[family],selected!=0);
            blades_rail[family][selected]=TTF_OpenFont(font_path.c_str(),pixels);
        }
    }
#ifndef __PROSPERO__
    fprintf(stderr,"Blades font: %s; face=%s; body=40 heading=49 rail=23pt:50/55 21pt:49/54 18pt:39/43; body tracking=1 rail tracking=0\n",font_path.c_str(),blades_body?TTF_FontFaceFamilyName(blades_body):"unavailable");
#endif
    semibold=TTF_OpenFont(bold_path.c_str(),29);
    if (!semibold) semibold=TTF_OpenFont(font_path.c_str(),29);
    {
        // The era button art: the disc from shared/, the letter in white on it.
        TTF_Font *letter_font=TTF_OpenFont(font_path.c_str(),64);   // NXE: Convection (Xbox TC)
        // 2010-12 skins print the letter in the dashboard's own Segoe Xbox
        // (XenonSCLatin.xtt, converted to SegoeXbox.ttf); NXE in Convection.
        const std::string segoe_xbox=installed({"SegoeXbox.ttf"});
        TTF_Font *thin_font=TTF_OpenFont((segoe_xbox.empty()?font_path:segoe_xbox).c_str(),60);
        if (!thin_font) thin_font=letter_font;
        // No synthetic bold: like the Blades lettering (docs/BLADES_TYPOGRAPHY.md),
        // the letters use the owner's recovered faces as they are.
        std::string skins=std::string(root)+"era-skins/";
#ifndef __PROSPERO__
        if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) skins=std::string(override)+"/";
#endif
        const char *families[]={"nxe","kinect","metro1","metro"};
        const char *letters="ABXY";
        for (int f=0;f<4;f++) for (int b=0;b<4;b++) {
            SDL_Surface *disc=trim_button_padding(load_png(skins+families[f]+"/buttons/"+letters[b]+"-Button.png"));
            if (!disc) continue;
            SDL_Surface *out=render_density::create(96,96,SDL_PIXELFORMAT_ARGB8888);
            if (out) {
                SDL_FillRect(out,nullptr,0);
                SDL_SetSurfaceBlendMode(disc,SDL_BLENDMODE_BLEND);
                SDL_Rect all{0,0,96,96};
                blit_quality(disc,nullptr,out,&all,false);
                if (letter_font) {
                    // legend_A..Y's XuiText1: NXE prints the letter in 0x0f0f0f on
                    // its glossy orb; the 2010-12 skins in 0xf5f5f5 over a faint
                    // 0x2e000000 drop shadow.
                    const char text_letter[2]={letters[b],0};
                    const SDL_Color ink=f==0?SDL_Color{15,15,15,255}:SDL_Color{245,245,245,255};
                    // Measured on the 1080p Guide captures, the letters read as partly
                    // see-through: NXE's dark one about 65 %, the later white one 80 %.
                    TTF_Font *face=f==0?letter_font:thin_font;
                    if (f>0) if (SDL_Surface *shadow=TTF_RenderUTF8_Blended(face,text_letter,{0,0,0,255})) {
                        SDL_Rect at{(96-render_density::logical_w(shadow))/2+2,(96-render_density::logical_h(shadow))/2+3,render_density::logical_w(shadow),render_density::logical_h(shadow)};
                        SDL_SetSurfaceBlendMode(shadow,SDL_BLENDMODE_BLEND);SDL_SetSurfaceAlphaMod(shadow,0x2e);
                        SDL_BlitSurface(shadow,nullptr,out,&at);SDL_FreeSurface(shadow);
                    }
                    if (SDL_Surface *glyph_text=TTF_RenderUTF8_Blended(face,text_letter,ink)) {
                        SDL_Rect at{(96-render_density::logical_w(glyph_text))/2,(96-render_density::logical_h(glyph_text))/2+1,render_density::logical_w(glyph_text),render_density::logical_h(glyph_text)};
                        SDL_SetSurfaceBlendMode(glyph_text,SDL_BLENDMODE_BLEND);
                        SDL_SetSurfaceAlphaMod(glyph_text,f==0?170:205);
                        SDL_BlitSurface(glyph_text,nullptr,out,&at);
                        SDL_FreeSurface(glyph_text);
                    }
                }
                SDL_SetSurfaceBlendMode(out,SDL_BLENDMODE_BLEND);
                era_glyphs[f][b]=out;
#ifndef __PROSPERO__
                if (getenv("NXE_DUMP_GLYPHS")) IMG_SavePNG(out,(std::string("/tmp/claude-0/cap/eg")+char('0'+f)+letters[b]+".png").c_str());
#endif
            }
            SDL_FreeSurface(disc);
        }
        if (thin_font && thin_font!=letter_font) TTF_CloseFont(thin_font);
        if (letter_font) TTF_CloseFont(letter_font);
    }
    font_sets[0][0]=regular;font_sets[0][1]=large;font_sets[0][2]=small;font_sets[0][3]=semibold;font_sets[0][4]=era_text;
    const std::string modern=installed({"SegoeUI.ttf","SegoeXbox.ttf"});
    const int sizes[]={29,49,22,29,36};
    for (int i=0;i<5;i++) {
        font_sets[1][i]=modern.empty()?nullptr:TTF_OpenFont(modern.c_str(),sizes[i]);
        if (!font_sets[1][i]) font_sets[1][i]=font_sets[0][i];
    }
    const std::string light_path=installed({"SegoeUI-Light.ttf","SegoeUI.ttf","SegoeXbox.ttf"});
    if (!light_path.empty()) metro_tabs_font=TTF_OpenFont(light_path.c_str(),48);
    if (!regular || !large || !small) log(TTF_GetError());
    startup_mark("fonts ready");
    load_art();startup_mark("shared art and video backing ready");
    source_blades::prepare(font_path.c_str());
    guide_scene::font_path=font_path;
    guide_scene::segoe_path=installed({"SegoeXbox.ttf","SegoeUI.ttf"});
    guide::item_font=TTF_OpenFont(bold_path.c_str(),36);
    guide::tab_font=TTF_OpenFont(bold_path.c_str(),32);
    guide::slab_font=TTF_OpenFont(bold_path.c_str(),30);
    guide::prompt_font=TTF_OpenFont(bold_path.c_str(),30);
    guide::clock_font=TTF_OpenFont(bold_path.c_str(),26);
    for (TTF_Font **f:{&guide::item_font,&guide::tab_font,&guide::slab_font,&guide::prompt_font,&guide::clock_font})
        if (!*f) *f=TTF_OpenFont(font_path.c_str(),30);
    model.player=profile_name();
    model.storage_free=storage_free();
    guide::make_labels();
    model.external_avatar_editor=true;
    avatar::load();
    // Kit loading starts after the initial Home submission.
    {
        char line[128];
        std::snprintf(line,sizeof line,"avatar direct memory: %zu MiB live",avkit::direct_memory_live() >> 20);
        log(line);
    }
    // The avatar's head shot joins the gamerpics, first; the PS5 profile
    // picture goes in front of it below.
    if (look3d::active()) look3d::refresh_head_shot();
    else if (SDL_Surface *head=load_png(std::string(root)+"private_art/avatar/avatar_head.png"))
        {gamerpics.insert(gamerpics.begin(),head);look3d::head_shot=head;}
    // The PS5 profile picture joins the gamerpics, first, and is the default.
    if (user_id>=0) {
        if (SDL_Surface *pic=load_png(ps5lib::profile_dir(unsigned(user_id),fs_prefix())+"/avatar.png")) {
            gamerpics.insert(gamerpics.begin(),pic);
            profile_picture=true;
            gamerpic=0;
            if (FILE *f=fopen(gamerpic_path,"r")) {int n=-1; if (fscanf(f,"%d",&n)==1 && n>=0) gamerpic=n%int(gamerpics.size()); fclose(f);}
        }
    }
    read_profile();
    model.promotions=true;
    load_settings();
    exbox_services::rom_guide_companion::apply(model.rom_game_guide_preview,nullptr);
    model.developer_diagnostics_available=developer_diagnostics_requested();
    dashboard_diagnostics.set_enabled(model.developer_diagnostics_available);
    model.health_runtime.diagnostics_available=model.developer_diagnostics_available;
    model.health_runtime.gpu_dashboard_requested=gpu_dashboard_health_requested();
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
    model.health_runtime.native_presenter_requested=native_present::requested;
    model.health_runtime.native_presenter_active=native_present::active;
#endif
    model.health_runtime.gpu_dashboard_available=
        model.health_runtime.gpu_dashboard_requested&&x21hook::available.load();
    model.health_runtime.gpu_fallback=
        (model.health_runtime.native_presenter_requested&&!model.health_runtime.native_presenter_active)||
        (model.health_runtime.gpu_dashboard_requested&&!model.health_runtime.gpu_dashboard_available);
    model.render_scale=persisted_render_scale;model.active_render_scale=render_density::factor;
    load_ad_catalog();
    load_ad_profiles();
    refresh_update_art();
    load_promotions();
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    sfx::open();
    load_trailers();
    load_trailer_choices();
    load_videos();
    load_artwork_choices();
    open_controllers();
    pads_seen=light::pads.size();
    refresh_apps();
    refresh_games();
#ifndef __PROSPERO__
    if (const char *audit=getenv("NXE_ART_AUDIT")) audit_art_pixels(audit);
#endif
    load_pins();
    load_categories();
    model.load_recent(read_lines(recent_path,dash::kMaxRecent));
    restore_editor_return();startup_mark("profile and library setup ready");
    finish_chrome_startup();startup_mark("chrome ready before first frame");
    // The 360 greeted the profile with a toast as it signed in, and then with
    // any achievements earned since it last looked.
    // The sign-in notification uses the Xbox orb inside the segmented disc.
    show_toast(model.player+" signed in",trophies.known?"Trophy level "+std::to_string(trophies.level):"Welcome to Ex-Box 360",
               &sfx::toast_clip);
    check_trophies(SDL_GetTicks());
    booting=false; // Native startup goes directly to Home.
#ifndef __PROSPERO__
    if (const char *boot=getenv("NXE_BOOT")) booting=*boot=='1';
    if (const char *s=getenv("NXE_SCRIPT")) {
        std::string all(s), token;
        for (char c:all+",") if (c==',') {if (!token.empty()) script.push_back(token); token.clear();} else token+=c;
        // Scripted runs start on the dashboard unless NXE_BOOT=1 asks for the boot.
        const char *boot=getenv("NXE_BOOT");
        booting=boot && *boot=='1';
    }
    if (const char *idle=getenv("NXE_IDLE_MS")) idle_limit=Uint32(std::max(1,atoi(idle)));
#else
    // A one-shot test script left by the owner's tools (renamed so it runs once).
    if (FILE *f=fopen("/data/nxe/exbox-script.txt","r")) {
        std::string all,token;char buffer[512];size_t n;
        while ((n=fread(buffer,1,sizeof buffer,f))>0) all.append(buffer,n);
        fclose(f);
        rename("/data/nxe/exbox-script.txt","/data/nxe/exbox-script.done");
        for (char c:all+",") if (c==',' || c=='\n' || c=='\r') {if (!token.empty()) script.push_back(token); token.clear();} else token+=c;
        booting=false;
        log(("test script: "+std::to_string(script.size())+" steps").c_str());
    }
#endif
    boot_at=SDL_GetTicks();
    if (booting) sfx::play(&sfx::boot_clip);
    last_input_at=SDL_GetTicks();
    position=float(model.current());
    shown_channel=model.channel;
    Uint32 previous=SDL_GetTicks();
#ifndef __PROSPERO__
    FILE *frame_audit=nullptr;
    if (const char *path=getenv("NXE_FRAME_AUDIT")) {
        frame_audit=fopen(path,"w");
        if (frame_audit) fprintf(frame_audit,"ticks,processing_ms,video_width,video_height\n");
    }
#endif
    bool startup_started=false;
    bool avatar_completion_reported=false,music_completion_reported=false,presenter_completion_reported=false,prep_completion_reported=false;
    auto record_startup_workers=[&]{
        const auto epoch_ms=std::chrono::duration_cast<std::chrono::milliseconds>(startup_epoch.time_since_epoch()).count();
        auto mark=[&](const char *name,std::uint64_t completed,bool &reported){
            if(completed && !reported){
                reported=true;char line[160];
                std::snprintf(line,sizeof line,"startup: %s completed elapsed_ms=%lld",name,static_cast<long long>(completed)-static_cast<long long>(epoch_ms));log(line);
            }
        };
        mark("avatar worker",look3d::kit_job.completion_ms(),avatar_completion_reported);
        mark("avatar preparation",look3d::preparation_completion_ms(),prep_completion_reported);
        mark("music worker",tunes::scan_job.completion_ms(),music_completion_reported);
        mark("first presenter call",present::first_present_completed_ms.load(std::memory_order_acquire),presenter_completion_reported);
    };
    auto start_background=[&](bool home_drawn,unsigned long submitted_before){
        if(!startup_started && home_drawn && present::submission_count()>submitted_before){
            startup_started=true;startup_mark("initial dashboard queued");
            look3d::load();tunes::scan();
        }
    };
    frame_pacing::Pacer frame_pacer;
    frame_metrics::Window render_metrics;
    auto record_render_work=[&](Uint64 started) {
        const double ms=1000.0*double(SDL_GetPerformanceCounter()-started)/double(SDL_GetPerformanceFrequency());
        dashboard_diagnostics.add_frame(ms);
        if(dashboard_diagnostics.enabled()){
            if(x21hook::ready)dashboard_diagnostics.add_command_batch();
            else if(x21hook::enabled())dashboard_diagnostics.add_fallback();
        }
        if(diagnostic_logging() && render_metrics.add(ms)) { char line[192]; render_metrics.report(line,sizeof line,render_density::factor); log(line); }
    };
    while (running) {
        const Uint64 frame_started=SDL_GetPerformanceCounter();
#if defined(__PROSPERO__) && defined(NXE_NATIVE_PRESENTER)
        if(x21hook::fatal.load()||(native_present::active && native_present::adapter.failed())){log("native presentation failed; stopping without unsafe fallback");running=false;break;}
#endif
        model.health_runtime.gpu_dashboard_available=
            model.health_runtime.gpu_dashboard_requested&&x21hook::available.load();
        model.health_runtime.gpu_fallback=
            (model.health_runtime.native_presenter_requested&&!model.health_runtime.native_presenter_active)||
            (model.health_runtime.gpu_dashboard_requested&&!model.health_runtime.gpu_dashboard_available);
#ifndef __PROSPERO__
        const Uint64 work_started=SDL_GetPerformanceCounter();
        auto record_frame=[&] {
            if (frame_audit) fprintf(frame_audit,"%u,%.4f,%d,%d\n",SDL_GetTicks(),
                1000.0*double(SDL_GetPerformanceCounter()-work_started)/double(SDL_GetPerformanceFrequency()),
                trailer_image?trailer_image->w:0,trailer_image?trailer_image->h:0);
        };
#endif
        Uint32 now=SDL_GetTicks();
        record_startup_workers();
        if(!suspended && !launching.live){
            if(look3d::poll_load())edited_avatar::refresh(true);
            tunes::poll_scan();
        }
        network_worker.tick(model);
        {const auto notice=recorder_worker.tick(model,now);if(!notice.title.empty())show_toast(notice.title,notice.text,notice.error?&sfx::toast_clip:&sfx::done_clip,notice.error?nullptr:toast_downloads);}
        poll_theme_catalog();
        poll_installed_theme();
        poll_theme_preview();
        SDL_Event event;
        while (running && SDL_PollEvent(&event)) handle(event,now);
        poll_native_controller(now);
        if (!running) break;
        const bool waiting=launching.live || booting || suspended || dim_level>0.05f;
        xb::Button again=waiting?xb::None:repeat.poll(now);
        if (again!=xb::None) press(again);
        float dt=std::min(0.1f,float(now-previous)/1000.0f); previous=now;
        position+=(float(model.current())-position)*std::min(1.0f,dt*12.0f);
        if (launching.live && now-launching.at>=kLaunchFade+kLaunchHold) launch_now(now);
        light::update(now);
        if (!suspended) tunes::sync();
        // Ten idle minutes dim the screen (1.5 s in); a press brings it back.
        picture_tick(now);
        const bool watching=(model.viewer==dash::Viewer::Videos && model.viewer_full && !model.video_paused) ||
                            (model.viewer==dash::Viewer::Pictures && model.viewer_full && model.picture_slideshow);
        const bool idle=model.dimming && !suspended && !launching.live && !watching && now-last_input_at>=idle_limit;
        dim_level=idle?std::min(1.0f,dim_level+dt/1.5f):std::max(0.0f,dim_level-dt/0.25f);
        exbox::dashboard_director::Observation director_state;
        director_state.family=model.era==0?exbox::dashboard_director::Family::Blades:model.era==1?exbox::dashboard_director::Family::Nxe:model.era==2?exbox::dashboard_director::Family::Kinect:model.metro_v1()?exbox::dashboard_director::Family::MetroV1:exbox::dashboard_director::Family::MetroV2;
        director_state.build=model.dashboard_build;
        director_state.view=int(model.viewer)*32+(model.viewer==dash::Viewer::None?(model.era==3?16+model.metro_tab:int(model.channel)):0);
        director_state.focus=model.viewer==dash::Viewer::None?model.current():model.viewer_item;
        director_state.guide=model.guide;director_state.notification=toast.live;director_state.playback=model.now_playing>=0;director_state.paused=model.music_paused;
        director_state.launching=launching.live||suspended;director_state.dimmed=dim_level>0.05f;
        const bool director_avatar_visible=model.viewer==dash::Viewer::Avatar||model.viewer==dash::Viewer::Profile||
            (model.viewer==dash::Viewer::None&&(((model.era==1||model.era==2)&&model.channel==dash::MyXbox)||(model.era==3&&model.metro_tab==2)));
        look3d::observe_dashboard(director_state,{true,true,custom_scene::equalizer.ready},now,director_avatar_visible);
        tick_promotion_rotation(now);
        // New trophies show up while the dashboard is open, too.
        if (!suspended && now-trophies_checked>=60000) check_trophies(now);
        if (suspended) {
            // Behind a game: no trailer, no sound; redraw rarely.
            update_trailer(-1);
            dwell_key.clear();
            if (now-suspended_drawn>=500) {suspended_drawn=now; draw(now,dt);}
            SDL_Delay(50);
            continue;
        }
        if (dim_level>0 || launching.live) {
            // Nothing plays on a dimmed screen or while a title starts.
            update_trailer(-1);
            dwell_key.clear();
        } else {
            // What is in front of the player, and whether it has a trailer.
            std::string key, title;
            bool is_title=false;
            if (model.info) {
                const dash::Card c=model.info_card();
                key="info:"+model.info_target.key; title=c.title;
                is_title=c.kind==dash::Kind::App || c.kind==dash::Kind::Game;
            } else if (!model.controller_map && !model.guide) {
                const dash::Card c=model.current_card();
                key=std::to_string(model.channel)+":"+std::to_string(model.current())+":"+c.title;
                title=c.title;
                is_title=model.current_is_title();
            }
            const std::string target=model.info?model.info_target.key:
                (is_title?(model.current_card().game>=0?dash::Model::game_key(model.games[size_t(model.current_card().game)]):
                 model.current_card().app>=0?model.apps[size_t(model.current_card().app)].executable:""):"");
            int match=is_title?trailer_for(target,title):-1;
            if (!model.info && !model.guide && model.content_focused() && !model.current_card().preview.empty())
                match=trailer_index(model.current_card().preview);
            bool at_once=false, full_video=false;
            if (model.viewer!=dash::Viewer::None && !model.guide && !model.info) {
                // Game Videos: the selected tile previews; full screen plays at once.
                match=-1;
                key="viewer";
                if (model.viewer==dash::Viewer::Videos && model.viewer_count()>0) {
                    // By clip, not grid place: sorting moves clips around.
                    const std::string clip=model.video_key();
                    key="video:"+clip+(model.viewer_full?":full":"");
                    match=trailer_index(clip);
                    at_once=full_video=model.viewer_full;
                }
                if (model.viewer==dash::Viewer::Library && model.viewer_count()>0) {
                    const auto cards=model.library_cards();
                    const auto &card=cards[size_t(std::clamp(model.viewer_item,0,int(cards.size())-1))];
                    const std::string target=card.game>=0?dash::Model::game_key(model.games[size_t(card.game)]):model.apps[size_t(card.app)].executable;
                    key="library:"+target;
                    match=trailer_for(target,card.title);
                }
            }
            // Reconcile A+Guide in one input drain before holding a grid preview.
            const bool full_viewer=model.viewer==dash::Viewer::Videos&&model.viewer_full&&!model.info;
            const int full_target=full_viewer?trailer_index(model.video_key()):-1;
            const bool ready_full_session=active_trailer>=0&&active_trailer==full_target&&(trailer_preparing||trailer_stream_ready());
            if(model.guide&&full_viewer&&ready_full_session&&!trailer_guide_suspended&&dwell_key!="video:"+model.video_key()+":full"){
                seek_trailer(Uint32(std::max(0,model.video_elapsed)));pause_trailer(model.video_paused);
            }
            const bool arrived=key!=dwell_key || match!=dwell_trailer;
            if (arrived) {
                dwell_key=key; dwell_trailer=match;
                dwell_since=at_once?now-kTrailerAt:now;
            }
            const bool same_full_video=full_viewer&&ready_full_session;
            const bool guide_hold=model.guide&&same_full_video;
            trailer_hold_end=full_video||guide_hold;
            bool resumed_guide=false;
            if(guide_hold){suspend_trailer_for_guide();model.video_elapsed=int(trailer_elapsed());}
            else {
                resumed_guide=resume_trailer_from_guide(same_full_video);
                if (!full_video && trailer_paused) pause_trailer(false);
                update_trailer(phase(now)==Phase::Play?match:-1);
            }
            if (full_video && match>=0 && active_trailer==match && (trailer_stream_ready()||trailer_preparing)) {
                // Full screen starts at the model's place: a resume, or where
                // the Guide interrupted it. Then the player reports back.
                if (arrived&&!resumed_guide) {seek_trailer(Uint32(std::max(0,model.video_elapsed)));pause_trailer(model.video_paused);}
                model.video_elapsed=int(trailer_elapsed());
                model.video_paused=trailer_paused;
            }
        }
        if (!script.empty()) {
            position=float(model.current());
            const auto submitted_before=startup_started?0:present::submission_count();
        const bool home_drawn=!booting && !suspended && !launching.live;
            draw(now,dt);start_background(home_drawn,submitted_before);
        record_render_work(frame_started);
#ifndef __PROSPERO__
            record_frame();
#endif
            if (!script_step(now)) break;
            SDL_Delay(8);
            continue;
        }
        const auto submitted_before=startup_started?0:present::submission_count();
        const bool home_drawn=!booting && !suspended && !launching.live;
        draw(now,dt);start_background(home_drawn,submitted_before);
        record_render_work(frame_started);
#ifndef __PROSPERO__
        record_frame();
#endif
        // Fractional deadlines retain true60Hz cadence; overruns do not cause catch-up bursts.
        const double clock_scale=1000.0/double(SDL_GetPerformanceFrequency());
        const double wait=frame_pacer.wait_ms(double(frame_started)*clock_scale,double(SDL_GetPerformanceCounter())*clock_scale,dim_level>=1);
        if(wait>0)SDL_Delay(Uint32(std::ceil(wait)));
    }
#ifndef __PROSPERO__
    if (frame_audit) fclose(frame_audit);
#endif
    tunes::scan_job.join();
    look3d::kit_job.join();
    theme_catalog_job.join();
    theme_apply_job.join();
    theme_preview_job.join();
    record_startup_workers();
    stop_trailer();
    trailer_preparation.shutdown();
    thumbnail_preparation.shutdown();
    tunes::save_session();tunes::save_activity();tunes::stop(false);
    sfx::close();
    light::hand_back();
    text_cache::clear();
    free_media();
    prepared_avatar::close();edited_avatar::close();
    avatar::close();
    look3d::close();
    // Shutdown-only owned surface caches; no later draw borrows these pixels.
    for (auto &e:icon_cache) if (e.second) SDL_FreeSurface(e.second);
    icon_cache.clear();icon_cache_bytes=0;
    for (auto &entry:song_covers) SDL_FreeSurface(entry.second);
    song_covers.clear();song_cover_order.clear();
    for (auto *&cover:toast_covers) {SDL_FreeSurface(cover);cover=nullptr;}
    toast_cover_next=0;
    // End shutdown-only surface cache cleanup.
    free_art();
    guide::free_all();
    std::set<TTF_Font *> fonts_to_close;
    for (const auto &family:font_sets) for (auto *font:family) if (font) fonts_to_close.insert(font);
    if (metro_tabs_font) fonts_to_close.insert(metro_tabs_font);
    if (blades_body) fonts_to_close.insert(blades_body);
    if (blades_caption) fonts_to_close.insert(blades_caption);
    if (blades_heading) fonts_to_close.insert(blades_heading);
    for (auto *font:nxe_channel) if (font) fonts_to_close.insert(font);
    for (const auto &family:blades_rail) {
        for (auto *font:family) if (font) fonts_to_close.insert(font);
    }
    blades_type::clear();
    for (auto *font:fonts_to_close) TTF_CloseFont(font);
    present::finish();
    record_startup_workers();
    if (hub_backdrop) SDL_FreeSurface(hub_backdrop);
    if (library_base) SDL_FreeSurface(library_base);
    custom_scene::clear();
    save_settings();
    SDL_FreeSurface(screen);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();
    log("Ex-Box 360 stopped");
    close_dashboard_log();
#ifdef __PROSPERO__
    // A foreground PS5 title must hand control back to ShellCore. Returning
    // from main can send it through the crash reporter instead.
    const int refused=sceSystemServiceLoadExec("exit",nullptr);
    char failure[96];
    std::snprintf(failure,sizeof failure,"system exit refused rc=0x%x",unsigned(refused));
    log_after_close(failure);
    for (;;) sceKernelUsleep(100000);
#else
    return 0;
#endif
}


