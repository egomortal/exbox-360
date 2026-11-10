// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
namespace tunes {
template <class F> void locked(F f) {
    if (sfx::device) SDL_LockAudioDevice(sfx::device);
    f();
    if (sfx::device) SDL_UnlockAudioDevice(sfx::device);
}
// The Music Library's rows for the chosen view: songs A-Z, or albums /
// artists / genres, or the songs of the one opened. A song row holds one song.
// Other rows (the pages over the library, runtime/music_library.inc.hpp):
// an option ("Play Album") has a label and its place as `action`; a song of
// the current playlist a label, a second line and its path; a picture of
// Choose Cover a label, a second line and the picture's path.
struct Row {
    std::string name, detail; std::vector<int> songs; bool group=false;
    std::string label, sub, cover_path, picture; int action=-1;
};
std::vector<Row> rows;
std::string group_name;
std::string page_kind; // what the heading names when a page is open ("Song", "Playlist" ...)
constexpr const char *kActivityPath="/data/nxe/music-activity-v1.txt";
constexpr const char *kSessionPath="/data/nxe/music-session-v1.txt";
bool activity_loaded=false;
void load_activity(){if(activity_loaded)return;activity_loaded=true;const auto data=ps5lib::read_file(kActivityPath,(1u<<20)+1);if(!data.empty()&&!music_activity.decode(data))log("music: invalid activity history ignored");}
void save_activity(){const auto data=music_activity.encode();if(data.empty())return;mkdir("/data/nxe",0777);const std::string temp=std::string(kActivityPath)+".tmp";if(FILE*f=fopen(temp.c_str(),"wb")){const bool wrote=fwrite(data.data(),1,data.size(),f)==data.size();const bool closed=fclose(f)==0;if(wrote&&closed)rename(temp.c_str(),kActivityPath);else remove(temp.c_str());}}
void save_session(){const auto&list=music_player.list();if(list.empty())return;std::ostringstream out;const music::Song*current=music_player.current_song();out<<"EXBOX_MUSIC_SESSION_1 "<<list.size()<<' '<<std::quoted(current?current->path:std::string{})<<'\n';for(const auto&song:list)out<<std::quoted(song.path)<<'\n';const auto data=out.str();if(data.size()>(8u<<20))return;mkdir("/data/nxe",0777);const std::string temp=std::string(kSessionPath)+".tmp";if(FILE*f=fopen(temp.c_str(),"wb")){const bool wrote=fwrite(data.data(),1,data.size(),f)==data.size();const bool closed=fclose(f)==0;if(wrote&&closed)rename(temp.c_str(),kSessionPath);else remove(temp.c_str());}}
void restore_session(){static bool attempted=false;if(attempted)return;attempted=true;const auto data=ps5lib::read_file(kSessionPath,(8u<<20)+1);if(data.empty()||data.size()>(8u<<20))return;std::istringstream in(data);std::string magic,current;std::size_t count=0;if(!(in>>magic>>count>>std::quoted(current))||magic!="EXBOX_MUSIC_SESSION_1"||count>music::kMaxSongs)return;std::map<std::string,music::Song>known;for(const auto&song:music_songs)known.emplace(song.path,song);std::vector<music::Song>queue;int at=0;for(std::size_t i=0;i<count;++i){std::string path;if(!(in>>std::quoted(path))||path.size()>4096)return;const auto found=known.find(path);if(found==known.end())continue;if(path==current)at=int(queue.size());queue.push_back(found->second);}in>>std::ws;if(!in.eof()||queue.empty())return;locked([&]{music_player.play(queue,std::clamp(at,0,int(queue.size())-1));music_player.set_paused(true);});model.music_paused=true;model.now_playing=music_player.current();}
std::vector<Row> playlist_rows();   // the saved playlists (music_library.inc.hpp)
void browse_page();                 // a page's rows (music_library.inc.hpp)
SDL_Surface *picture_row_thumb(const std::string &path);
void load_chosen_covers();
void load_album_info();
std::vector<Row> groups(int view) {
    std::map<std::string,Row> by_key;
    for (int i=0;i<int(music_songs.size());++i) {
        const music::Song &song=music_songs[size_t(i)];
        std::string name=view==1?song.album:view==3?song.genre:song.artist;
        if (name.empty() && view==1) {
            // Untagged songs: their folder is the album, unless it is a library root.
            const size_t slash=song.path.rfind('/');
            const std::string folder=slash==std::string::npos?"":song.path.substr(0,slash);
            bool root_folder=false;
            for (const auto &r:music::default_roots()) if (folder==r) root_folder=true;
            if (!root_folder && !folder.empty()) name=folder.substr(folder.rfind('/')+1);
        }
        if (name.empty()) name=view==1?"Unknown Album":view==3?"Unknown Genre":"Unknown Artist";
        Row &row=by_key[music::folded(name)];
        if (row.songs.empty()) {row.name=name;row.group=true;row.detail=view==1?song.artist:"";}
        else if (view==1 && row.detail!=song.artist) row.detail="Various Artists";
        row.songs.push_back(i);
    }
    std::vector<Row> out;
    for (auto &entry:by_key) {
        Row &row=entry.second;
        const std::string count=std::to_string(row.songs.size())+(row.songs.size()==1?" song":" songs");
        row.detail=row.detail.empty()?count:row.detail+"  \xc2\xb7  "+count;
        out.push_back(std::move(row));
    }
    return out;
}
void browse() {
    rows.clear();group_name.clear();page_kind.clear();
    if (model.music_page!=dash::Model::MusicLibrary) {browse_page();return;}
    const int view=std::clamp(model.music_view,0,dash::kMusicViews-1);
    auto single=[](int i) {Row r;r.songs={i};return r;};
    if (view==0 || view>=5) {
        std::vector<int> order(music_songs.size());
        for (size_t i=0;i<order.size();++i) order[i]=int(i);
        if(view>=5){std::vector<std::string>paths;paths.reserve(music_songs.size());for(const auto&song:music_songs)paths.push_back(song.path);order=view==5?music_activity.most_played(paths):music_activity.recently_added(paths);}
        else std::stable_sort(order.begin(),order.end(),[](int a,int b) {return music::folded(music_songs[size_t(a)].title)<music::folded(music_songs[size_t(b)].title);});
        for (int i:order) rows.push_back(single(i));
    } else {
        std::vector<Row> list=view==4?playlist_rows():groups(view);
        if (model.music_group>=int(list.size())) model.music_group=-1;
        if (model.music_group<0) rows=std::move(list);
        else {
            Row &group=list[size_t(model.music_group)];
            group_name=group.name;
            // The group's options come first (1005_MediaContainerOptions).
            const auto actions=model.music_group_actions();
            for (size_t a=0;a<actions.size();a++) {Row r;r.label=actions[a];r.action=int(a);rows.push_back(r);}
            std::vector<int> order=group.songs; // album: file order (track numbers lead the names)
            if (view==2) std::stable_sort(order.begin(),order.end(),[](int a,int b) {
                return music::folded(music_songs[size_t(a)].album)<music::folded(music_songs[size_t(b)].album);});
            if (view==3) std::stable_sort(order.begin(),order.end(),[](int a,int b) { // a genre: by artist, then album
                const music::Song &x=music_songs[size_t(a)],&y=music_songs[size_t(b)];
                const std::string fx=music::folded(x.artist),fy=music::folded(y.artist);
                return fx!=fy?fx<fy:music::folded(x.album)<music::folded(y.album);});
            if (view==4) order=group.songs; // a saved playlist keeps its own order
            for (int i:order) rows.push_back(single(i));
        }
    }
    model.songs=int(rows.size());
    model.viewer_item=std::clamp(model.viewer_item,0,std::max(0,model.songs-1));
}
// A row's picture: a song's own cover; an album, artist or genre shows its
// first song's, or (when that has none) the first of its other songs that
// has one. song_cover() decodes at most one a frame; the other songs are
// looked at one a frame (cover_probe_turn: the file is read, nothing is
// decoded, and nothing goes into the cover cache), and what is found is
// remembered, so a big group of songs without pictures cannot churn the cache.
struct CoverSearch {size_t next=1; int found=-1;};
std::map<std::string,CoverSearch> cover_searches; // by view and group name; a scan forgets them
SDL_Surface *row_cover(const Row &row) {
    if (!row.picture.empty()) return picture_row_thumb(row.picture);
    if (row.songs.empty()) return row.cover_path.empty()?nullptr:song_cover(row.cover_path);
    const std::string &lead=music_songs[size_t(row.songs[0])].path;
    const bool known=song_cover_known(lead);
    if (SDL_Surface *cover=song_cover(lead)) return cover;
    if (!row.group || !song_cover_known(lead) || !known) return nullptr; // the first song's turn comes first
    CoverSearch &search=cover_searches[std::to_string(model.music_view)+"/"+row.name];
    if (search.found>=0) return song_cover(music_songs[size_t(row.songs[size_t(search.found)])].path);
    if (search.next<row.songs.size() && cover_probe_turn()) {
        const size_t k=search.next++;
        if (!music::cover_art(music_songs[size_t(row.songs[k])].path).empty()) search.found=int(k);
    }
    return nullptr;
}
// Shuffle and repeat, as the model holds them, given to the player.
void set_order() {
    locked([] {
        music_player.set_shuffle(model.music_shuffle,SDL_GetTicks()*2654435761u+1u);
        music_player.set_repeat(model.music_repeat==1?music::Repeat::All:model.music_repeat==2?music::Repeat::One:music::Repeat::Off);
    });
}
const music::Song *row_song(int row) {
    if (row<0 || row>=int(rows.size()) || rows[size_t(row)].group || rows[size_t(row)].songs.empty()) return nullptr;
    return &music_songs[size_t(rows[size_t(row)].songs[0])];
}
// The Guide's compact browser shares the indexed library and player. It does
// not open the full Music page, and it stores only song indices in the model.
void guide_refresh() {
    model.guide_music_rows.clear();
    auto add=[](std::string label,std::string detail,std::vector<int> songs) {
        model.guide_music_rows.push_back({std::move(label),std::move(detail),std::move(songs)});
    };
    if (model.guide_music_page==1) {
        for (auto &row:groups(1)) add(row.name,row.detail,std::move(row.songs));
    } else if (model.guide_music_page==2 || model.guide_music_page==4) {
        std::vector<int> order(music_songs.size());for(size_t i=0;i<order.size();++i)order[i]=int(i);
        std::stable_sort(order.begin(),order.end(),[](int a,int b){return music::folded(music_songs[size_t(a)].title)<music::folded(music_songs[size_t(b)].title);});
        const std::string query=music::folded(model.guide_music_query);
        for(int i:order) {
            const auto &song=music_songs[size_t(i)];
            const std::string hay=music::folded(song.title+" "+song.artist+" "+song.album);
            if(model.guide_music_page==4 && !query.empty() && hay.find(query)==std::string::npos)continue;
            add(song.title,song.artist.empty()?song.album:song.artist,{i});
        }
    } else if (model.guide_music_page==3) {
        std::map<std::string,int> by_path;for(size_t i=0;i<music_songs.size();++i)by_path[music_songs[i].path]=int(i);
        for(const auto &song:music_player.list())if(auto found=by_path.find(song.path);found!=by_path.end())add(song.title,song.artist.empty()?song.album:song.artist,{found->second});
    }
}
bool guide_play(int row) {
    if(row<0 || row>=int(model.guide_music_rows.size()))return false;
    std::vector<music::Song> queue;
    for(int i:model.guide_music_rows[size_t(row)].songs)if(i>=0 && i<int(music_songs.size()))queue.push_back(music_songs[size_t(i)]);
    if(queue.empty())return false;
    locked([&]{music_player.play(queue,0);});
    model.now_playing=music_player.current();model.music_paused=false;return true;
}
exbox::StartupJob<music_index::Result> scan_job;
Uint32 scan_started=0;
void scan() {
    if(scan_job.active())return; // Coalesce requests; never block an input handler.
    const auto roots=music::default_roots();
    const auto game_roots=model.music_game_scan?music::game_roots():std::vector<std::string>{};
    std::string cache="/data/nxe/music-index-v1.txt";
#ifndef __PROSPERO__
    if(const char* override_path=getenv("NXE_MUSIC_INDEX_PATH"))if(*override_path)cache=override_path;
#endif
    scan_started=SDL_GetTicks();
    if(!scan_job.start([roots,game_roots,cache]{return music_index::scan(roots,cache,game_roots);}))
        log("startup: music worker unavailable; keeping current library");
}
void poll_scan() {
    // Editing pages borrow song indices; launching/suspended views must not publish.
    if(suspended||launching.live||model.music_page!=dash::Model::MusicLibrary)return;
    music_index::Result result;if(!scan_job.take(result))return;
    if(!result.ok){log("startup: music scan failed; keeping current library");return;}
    const Uint32 publish_started=SDL_GetTicks();
    music_songs=std::move(result.songs);
    int game_tracks=0;
    std::set<std::string> game_albums;
    for(const auto& song:music_songs)
        if(song.genre=="Game Soundtrack"){++game_tracks;if(!song.album.empty())game_albums.insert(song.album);}
    model.health_runtime.soundtrack_playable=game_tracks;
    model.health_runtime.soundtrack_albums=int(game_albums.size());
    // This playable-song index does not publish unsupported/container counts.
    model.health_runtime.soundtrack_recognized_unsupported=-1;
    model.health_runtime.soundtrack_containers=-1;
    load_activity();{std::vector<std::string>paths;paths.reserve(music_songs.size());for(const auto&song:music_songs)paths.push_back(song.path);music_activity.observe(paths);save_activity();}
    load_chosen_covers();load_album_info();cover_searches.clear();browse();
    restore_session();
    if(diagnostic_logging()){char line[240];std::snprintf(line,sizeof line,"startup: music ready songs=%zu cache_hits=%zu misses=%zu scan_ms=%llu completed_steady_ms=%llu publish_ms=%u total_ms=%u cache_saved=%d",music_songs.size(),result.hits,result.misses,static_cast<unsigned long long>(result.scan_ms),static_cast<unsigned long long>(scan_job.completion_ms()),unsigned(SDL_GetTicks()-publish_started),unsigned(SDL_GetTicks()-scan_started),int(result.cache_written));log(line);}
}
// Play the listed songs from that row on. False when none of them would play.
bool play(int row) {
    if (row<0 || row>=int(rows.size()) || rows[size_t(row)].group || rows[size_t(row)].songs.empty()) return false;
    std::vector<music::Song> queue;
    int at=0;
    for (int i=0;i<int(rows.size());i++) {
        const Row &r=rows[size_t(i)];
        if (r.group || r.songs.empty()) continue;
        if (i==row) at=int(queue.size());
        queue.push_back(music_songs[size_t(r.songs[0])]);
    }
    sfx::open();
    set_order();
    locked([&] {music_player.play(queue,at);});
    music_player.pump();
    if (!music_player.active()) {model.now_playing=-1; return false;}
    invalidate_trailer_audio_for_music(); // the music takes over
    return true;
}
void stop(bool forget_session=true) {
    locked([] {music_player.stop();});
    model.now_playing=-1;
    model.music_paused=false;
    if(forget_session) remove(kSessionPath);
}
// Every frame: decode ahead, and tell the model what is playing.
void sync() {
    // A full-screen game video holds the music while it plays with its own sound.
    const bool hold=model.viewer==dash::Viewer::Videos && model.viewer_full;
    music_player.set_paused(model.music_paused || hold);
    music_player.pump();
    const music::Song *song=music_player.current_song();
    static std::string activity_song;const std::string activity_path=song?song->path:"";if(!activity_path.empty()&&activity_path!=activity_song){music_activity.played(activity_path);save_activity();activity_song=activity_path;}else if(activity_path.empty())activity_song.clear();
    model.now_playing=music_player.current();
    // The current playlist, for Now Playing and Edit Current Playlist: their
    // rows follow it as it changes (a song ends, an edit lands).
    model.music_queue=model.now_playing>=0?int(music_player.list().size()):0;
    static std::string queue_seen;
    if (model.viewer==dash::Viewer::Music && (model.music_page==dash::Model::MusicNowPlaying || model.music_page==dash::Model::MusicEditPlaylist)) {
        std::string stamp=std::to_string(model.now_playing)+"|"+std::to_string(model.music_page);
        for (const auto &s:music_player.list()) stamp+="|"+s.path;
        if (stamp!=queue_seen) {queue_seen=stamp;browse();}
    } else queue_seen.clear();
    model.now_playing_title=!song?"":song->artist.empty()?song->title:song->title+" - "+song->artist;
    if (model.now_playing<0) model.music_paused=false;
    // A new song while the owner is elsewhere: say so, with its cover, as the
    // 360 did. Wait (a frame or two) until the cover has been looked up.
    static std::string announced;
    const std::string path=song?song->path:"";
    if (path!=announced && !path.empty()) {
        song_cover(path);
        if (song_cover_known(path)) {
            announced=path;
            log(("music: now playing "+model.now_playing_title+(song_cover(path)?" (cover)":"")).c_str());
            if (model.viewer!=dash::Viewer::Music)
                now_playing_toast(model.now_playing_title,keep_for_toast(song_cover(path)));
        }
    } else if (path.empty()) announced.clear();
    sync_trailer_audio_with_music();
}
}
