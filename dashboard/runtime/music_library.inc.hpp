// Included inside main.cpp's private namespace, after the picture thumbnails
// and toasts; see docs/CODE_MAP.md.
//
// The Music Library's pages and what they do, after the 360's music scenes
// in the owner's files (music/1003_IndividualDevice: the Saved Playlists
// view; 1005_MediaContainerOptions and 1016_IndividualSongMain: Play, Add to
// Current Playlist; 1028_NowPlaying; 1030_EditPlaylist: Save Playlist, Clear
// All, Done, each song's Move Up / Move Down / Delete; Strings.xus for the
// words). Choose Cover is Ex-Box's own: any photo or screenshot on the PS5
// becomes a song's (or an album's) cover.
//
// Saved playlists are ordinary .m3u files in /data/nxe/playlists (a song's
// path a line), so they can be copied in and out by FTP too. Chosen covers
// are /data/nxe/music-art/chosen.txt (music::read_chosen_covers).
namespace tunes {
const char *kPlaylistDir="/data/nxe/playlists";
const char *kChosenCovers="/data/nxe/music-art/chosen.txt";
const char *kAlbumInfo="/data/nxe/music-art/album-info.txt";
musicmeta::Edits album_edits;
bool album_info_valid=true;
std::vector<std::string> album_paths;
void apply_album_info() {
    for (auto &song:music_songs) if (auto it=album_edits.find(song.path);it!=album_edits.end()) {
        const auto f=musicmeta::apply({song.album,song.artist,song.genre},it->second);
        song.album=f[0];song.artist=f[1];song.genre=f[2];
    }
}
void load_album_info() {
    const std::string data=ps5lib::read_file(kAlbumInfo,4*1024*1024+1);
    album_info_valid=data.size()<=4*1024*1024;
    if (data.empty()) album_edits.clear();
    else if (album_info_valid) album_info_valid=musicmeta::decode(data,album_edits);
    if (!album_info_valid) log("music: invalid album-info sidecar; edits disabled");
    apply_album_info();
}

// ---- Saved playlists
struct SavedPlaylist {std::string name, file; std::vector<std::string> paths;};
std::vector<SavedPlaylist> saved_playlists() {
    std::vector<SavedPlaylist> out;
    DIR *d=opendir(kPlaylistDir);
    if (!d) return out;
    while (dirent *e=readdir(d)) {
        const std::string file=e->d_name;
        if (file.size()<5 || music::extension(file)!="m3u") continue;
        SavedPlaylist p;
        p.name=file.substr(0,file.size()-4);
        p.file=std::string(kPlaylistDir)+"/"+file;
        if (FILE *f=fopen(p.file.c_str(),"rb")) {
            char line[4096];
            while (fgets(line,sizeof line,f)) {
                std::string l=line;
                while (!l.empty() && (l.back()=='\n' || l.back()=='\r')) l.pop_back();
                if (!l.empty() && l[0]!='#') p.paths.push_back(l);
            }
            fclose(f);
        }
        out.push_back(std::move(p));
    }
    closedir(d);
    std::sort(out.begin(),out.end(),[](const SavedPlaylist &a,const SavedPlaylist &b) {return music::folded(a.name)<music::folded(b.name);});
    return out;
}
// A saved playlist as a library group: the songs of it the library has.
std::vector<Row> playlist_rows() {
    std::map<std::string,int> by_path;
    for (int i=0;i<int(music_songs.size());i++) by_path[music_songs[size_t(i)].path]=i;
    std::vector<Row> out;
    for (const auto &p:saved_playlists()) {
        Row row;row.name=p.name;row.group=true;
        for (const auto &path:p.paths) if (auto found=by_path.find(path);found!=by_path.end()) row.songs.push_back(found->second);
        row.detail=std::to_string(row.songs.size())+(row.songs.size()==1?" song":" songs");
        out.push_back(std::move(row));
    }
    return out;
}
// "Playlist 1", "Playlist 2" ...: the first name not taken (no keyboard yet).
std::string next_playlist_name() {
    std::set<std::string> taken;
    for (const auto &p:saved_playlists()) taken.insert(music::folded(p.name));
    for (int n=1;;n++) {
        const std::string name="Playlist "+std::to_string(n);
        if (!taken.count(music::folded(name))) return name;
    }
}

// ---- Chosen covers
void load_chosen_covers() {music::read_chosen_covers(kChosenCovers);}
void forget_covers(const std::vector<std::string> &paths) {
    for (const auto &path:paths) {
        auto it=song_covers.find(path);
        if (it==song_covers.end()) continue;
        if (it->second) SDL_FreeSurface(it->second);
        song_covers.erase(it);
        song_cover_order.erase(std::remove(song_cover_order.begin(),song_cover_order.end(),path),song_cover_order.end());
    }
    cover_searches.clear();
}
// One picture decoded a frame: a list of 4K screenshots must not stall the page.
SDL_Surface *picture_row_thumb(const std::string &path) {
    static unsigned long budget_frame=~0ul;
    static int budget=0;
    if (budget_frame!=frame_count) {budget_frame=frame_count;budget=1;}
    return picture_thumb(path,&budget);
}

// ---- The pages
int page_song=-1;                     // the song page's song (an index into music_songs)
std::vector<int> page_list;           // the library list it was chosen from (song indices)
int page_list_at=0;
std::vector<int> cover_songs;         // what Choose Cover is choosing for
std::vector<std::string> cover_pictures;
std::vector<dash::PictureFile> cover_files; // their names and dates, as Pictures shows them
// The songs of the opened group (the rows after its options).
std::vector<int> group_songs() {
    std::vector<int> out;
    for (const Row &r:rows) if (!r.group && !r.songs.empty()) out.push_back(r.songs[0]);
    return out;
}
Row queue_row(const music::Song &song) {
    Row r;
    r.label=song.title;
    r.sub=song.artist.empty()?song.album:song.artist;
    r.cover_path=song.path;
    return r;
}
void browse_page() {
    const auto fixed=model.music_page_rows();
    for (size_t a=0;a<fixed.size();a++) {Row r;r.label=fixed[a];r.action=int(a);rows.push_back(r);}
    switch (model.music_page) {
    case dash::Model::MusicAlbumInfo:
        page_kind="Album";group_name="Edit Album Info";break; // 1025_EditAlbumInfo
    case dash::Model::MusicSong:
        page_kind="Song";
        if (page_song>=0 && page_song<int(music_songs.size())) group_name=music_songs[size_t(page_song)].title;
        break;
    case dash::Model::MusicNowPlaying:
    case dash::Model::MusicEditPlaylist:
        page_kind=""; // the heading itself says which
        group_name=model.music_page==dash::Model::MusicNowPlaying?"Now Playing":"Edit Current Playlist";
        if (model.now_playing>=0) for (const auto &song:music_player.list()) rows.push_back(queue_row(song));
        break;
    case dash::Model::MusicChooseCover:
        page_kind="Choose Cover";
        group_name=cover_songs.size()==1?music_songs[size_t(cover_songs[0])].title:
                   !cover_songs.empty()?music_songs[size_t(cover_songs[0])].album:"";
        for (const auto &f:cover_files) {
            Row r;
            r.label=f.label();
            r.sub=dash::picture_date(f.taken);
            r.picture=f.path;
            rows.push_back(r);
        }
        break;
    case dash::Model::MusicConfirmDelete:
        page_kind="Delete Saved Playlist";
        // Strings.xus: "Are you sure you want to delete this playlist? ..."
        group_name="Are you sure you want to delete this playlist?";
        break;
    default: break;
    }
}
// The pictures Choose Cover offers: the PS5's photos and screenshots that
// decode as covers (JPEG / PNG), newest first.
void find_cover_pictures() {
    scan_pictures();
    cover_pictures.clear();cover_files.clear();
    std::vector<dash::PictureFile> files=model.picture_files;
    std::stable_sort(files.begin(),files.end(),[](const dash::PictureFile &a,const dash::PictureFile &b) {return a.taken>b.taken;});
    for (const auto &f:files) if (ps5lib::viewable_picture(f.path)) {cover_pictures.push_back(f.path);cover_files.push_back(f);}
    model.music_covers=int(cover_pictures.size());
}
// What a MusicEdit effect asks for.
void edit(dash::MusicOp op, int target) {
    using Op=dash::MusicOp;
    auto queue_of=[](const std::vector<int> &songs) {
        std::vector<music::Song> q;
        for (int i:songs) if (i>=0 && i<int(music_songs.size())) q.push_back(music_songs[size_t(i)]);
        return q;
    };
    auto start=[](const std::vector<music::Song> &q,int at) {
        if (q.empty()) return;
        sfx::open();
        set_order();
        locked([&] {music_player.play(q,at);});
        music_player.pump();
        invalidate_trailer_audio_for_music(); // the music takes over
    };
    auto add=[](const std::vector<music::Song> &q) {
        if (q.empty()) return;
        sfx::open();
        set_order();
        locked([&] {music_player.append(q);});
        music_player.pump();
        show_toast("Add to Current Playlist",q.size()==1?q[0].title:std::to_string(q.size())+" songs",&sfx::toast_clip);
    };
    switch (op) {
    case Op::OpenAlbumInfo: {
        album_paths.clear();
        const auto songs=group_songs();
        model.music_album_fields={};
        bool first=true;
        for (int i:songs) if (i>=0 && i<int(music_songs.size())) {
            const auto &song=music_songs[size_t(i)];album_paths.push_back(song.path);
            const musicmeta::Fields fields{song.album,song.artist,song.genre};
            if (first) {model.music_album_fields=fields;first=false;}
            else for (size_t f=0;f<3;f++) if (model.music_album_fields[f]!=fields[f]) model.music_album_fields[f].clear();
        }
        model.music_album_original=model.music_album_fields;model.music_album_changed=0;
        browse();break;
    }
    case Op::SaveAlbumInfo: {
        const auto edits=musicmeta::changed(album_edits,album_paths,model.music_album_original,model.music_album_fields,model.music_album_changed);
        mkdir("/data/nxe",0777);mkdir("/data/nxe/music-art",0777);
        if (!album_info_valid || album_paths.empty() || !musicmeta::save(kAlbumInfo,edits)) {
            model.album_save_result(false);
            break;
        }
        album_edits=edits;apply_album_info();
        // Re-find the renamed album; sorting can move it or merge it with another.
        model.album_save_result(true);model.music_group=-1;
        const auto albums=groups(1);
        for (size_t i=0;i<albums.size();i++) for (int song:albums[i].songs)
            if (music_songs[size_t(song)].path==album_paths.front()) model.music_group=int(i);
        model.viewer_item=model.music_library_item;browse();break;
    }
    case Op::OpenSong:
        page_song=-1;page_list.clear();page_list_at=0;
        // The rows are still the library's: remember the song and its list.
        for (int i=0;i<int(rows.size());i++) {
            const Row &r=rows[size_t(i)];
            if (r.group || r.songs.empty()) continue;
            if (i==target) {page_song=r.songs[0];page_list_at=int(page_list.size());}
            page_list.push_back(r.songs[0]);
        }
        browse();
        break;
    case Op::RandomAlbum: {
        // Reuse the library's exact album identities/order; no scan or artwork I/O.
        const auto albums=groups(1);
        std::vector<std::string> keys;keys.reserve(albums.size());
        for(const auto& album:albums)keys.push_back(music::folded(album.name));
        static random_album::Picker picker(SDL_GetPerformanceCounter());
        const int chosen=picker.choose(keys);
        if(chosen<0){show_toast("Random album","No albums available",&sfx::toast_clip);break;}
        const auto queue=queue_of(albums[size_t(chosen)].songs);
        if(queue.empty())break;
        sfx::open();set_order();
        locked([&]{music_player.play(queue,0);});music_player.pump();
        if(!music_player.active()){
            model.now_playing=-1;
            show_toast("Random album","This album could not be played",&sfx::toast_clip);
            break;
        }
        picker.played(keys[size_t(chosen)]);
        model.music_paused=false;
        invalidate_trailer_audio_for_music();
        show_toast("Random album",albums[size_t(chosen)].name,&sfx::toast_clip);
        break;
    }
    case Op::PlayGroup: start(queue_of(group_songs()),0); break;
    case Op::AddGroup: add(queue_of(group_songs())); break;
    case Op::PlaySong: browse(); start(queue_of(page_list),page_list_at); break;
    case Op::AddSong: browse(); if (page_song>=0) add(queue_of({page_song})); break;
    case Op::OpenCover:
        cover_songs=model.music_cover_from==dash::Model::MusicSong?std::vector<int>{page_song}:group_songs();
        cover_songs.erase(std::remove(cover_songs.begin(),cover_songs.end(),-1),cover_songs.end());
        find_cover_pictures();
        browse();
        break;
    case Op::SetCover:
    case Op::ClearCover: {
        std::vector<std::string> paths;
        for (int i:cover_songs) if (i>=0 && i<int(music_songs.size())) paths.push_back(music_songs[size_t(i)].path);
        const bool set=op==Op::SetCover && target>=0 && target<int(cover_pictures.size());
        for (const auto &path:paths) {
            if (set) music::chosen_covers()[path]=cover_pictures[size_t(target)];
            else music::chosen_covers().erase(path);
        }
        mkdir("/data/nxe/music-art",0777);
        if (!music::write_chosen_covers(kChosenCovers)) log("music: could not save the chosen covers");
        forget_covers(paths);
        browse();
        if (set) show_toast("Cover chosen",paths.size()==1?music_songs[size_t(cover_songs[0])].title:std::to_string(paths.size())+" songs",&sfx::toast_clip); // Ex-Box addition
        break;
    }
    case Op::Jump: locked([&] {music_player.jump(target);}); break;
    case Op::Remove: locked([&] {music_player.remove(target);}); music_player.pump(); browse(); break;
    case Op::MoveUp: locked([&] {music_player.move(target,target-1);}); music_player.pump(); browse(); break;
    case Op::MoveDown: locked([&] {music_player.move(target,target+1);}); music_player.pump(); browse(); break;
    case Op::SavePlaylist: {
        if (music_player.list().empty()) break;
        mkdir(kPlaylistDir,0777);
        const std::string name=next_playlist_name();
        const std::string file=std::string(kPlaylistDir)+"/"+name+".m3u";
        bool ok=false;
        if (FILE *f=fopen(file.c_str(),"wb")) {
            fputs("#EXTM3U\n",f);
            for (const auto &song:music_player.list()) fprintf(f,"%s\n",song.path.c_str());
            ok=fclose(f)==0;
        }
        log(("music: "+std::string(ok?"saved ":"could not save ")+file).c_str());
        show_toast(ok?"Save Playlist":"Can't Save Playlist",ok?name:"The playlist could not be written.",&sfx::toast_clip); // music/Strings.xus
        break;
    }
    case Op::ClearPlaylist: stop(); browse(); break;
    case Op::DeletePlaylist: {
        const auto lists=saved_playlists();
        if (target>=0 && target<int(lists.size())) {
            const bool ok=std::remove(lists[size_t(target)].file.c_str())==0;
            log(("music: "+std::string(ok?"deleted ":"could not delete ")+lists[size_t(target)].file).c_str());
        }
        browse();
        break;
    }
    }
}
}
