"""One-shot T13 source edits; preserve existing newlines and originals locally."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def edit(name, changes):
    p=ROOT/name
    raw=p.read_bytes()
    backup=ROOT/'backups/T13-small-items'/name
    backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists(): backup.write_bytes(raw)
    s=raw.decode('utf-8').replace('\r\n','\n')
    for old,new in changes:
        if old not in s: raise ValueError((name,old[:120]))
        s=s.replace(old,new,1)
    p.write_bytes(s.replace('\n','\r\n' if b'\r\n' in raw else '\n').encode('utf-8'))

edit('dash_model.hpp',[
 ('    OpenSong,       //', '    OpenAlbumInfo, SaveAlbumInfo, // music/1025_EditAlbumInfo.xur\n    OpenSong,       //'),
 ('MusicChooseCover, MusicConfirmDelete };','MusicChooseCover, MusicConfirmDelete, MusicAlbumInfo };'),
 ('    int music_page = MusicLibrary;', '''    // 9199 music/1025_EditAlbumInfo.xur; input grid is an Ex-Box addition.
    std::array<std::string,3> music_album_fields{}, music_album_original{};
    int music_album_field=-1, music_album_key=0;
    std::string music_album_input;
    static constexpr const char *music_album_keys="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .-'!?&()";
    Effect press_album_keyboard(xb::Button button);
    int music_page = MusicLibrary;''')])
edit('dash_model.cpp',[
 ('        out.push_back("Choose Cover");','        {out.push_back("Choose Cover");out.push_back("Edit Album Info");}'),
 ('    case MusicSong: return {"Play Song",', '''    case MusicAlbumInfo: return {"Album: "+music_album_fields[0],"Artist: "+music_album_fields[1],
                                "Genre: "+music_album_fields[2],"Save Changes"}; // 1025_EditAlbumInfo
    case MusicSong: return {"Play Song",'''),
 ('// The pages over the Music Library.','''// Ex-Box addition: no 360 original. A compact controller keyboard for album fields.
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
        music_album_fields[size_t(music_album_field)]=music_album_input;
        music_album_field=-1;return {Command::MusicBrowse,-1};
    case xb::B: music_album_field=-1;break;
    case xb::Guide:
    case xb::Start: open_guide();break;
    default: break;
    }
    return {};
}

// The pages over the Music Library.'''),
 ('Effect Model::press_music_page(xb::Button button)\n{','''Effect Model::press_music_page(xb::Button button)
{
    if (music_page==MusicAlbumInfo && music_album_field>=0) return press_album_keyboard(button);'''),
 ('    switch (music_page)\n    {\n    case MusicSong:\n', '''    switch (music_page)
    {
    case MusicAlbumInfo:
        if (button==xb::B) {music_album_fields=music_album_original;return library();}
        if (button!=xb::A) return {};
        if (viewer_item==3) return edit(MusicOp::SaveAlbumInfo,-1);
        music_album_field=viewer_item;music_album_key=0;
        music_album_input=music_album_fields[size_t(viewer_item)];
        return {};
    case MusicSong:
'''),
 ('                music_library_item = viewer_item;\n                viewer_item = 0;\n                if (music_view == 4)', '''                music_library_item = viewer_item;
                viewer_item = 0;
                if (music_view==1 && music_library_item==3) {
                    music_page=MusicAlbumInfo;music_album_field=-1;
                    return {Command::MusicEdit,int(MusicOp::OpenAlbumInfo)};
                }
                if (music_view == 4)'''),
 ("        // The pages' legends: 1016 / 1028 / 1030", '''        if (music_page==MusicAlbumInfo) {
            if (music_album_field>=0) return {{xb::A,"Select"},{xb::X,"Backspace"},{xb::Y,"Done"},{xb::B,"Cancel"}}; // Ex-Box addition: no 360 original keyboard
            return {{xb::A,"Select"},{xb::B,"Cancel"}}; // 1025_EditAlbumInfo
        }
        // The pages' legends: 1016 / 1028 / 1030''')])
edit('main.cpp',[
 ('#include "music.hpp"','#include "music.hpp"\n#include "music_metadata.hpp"'),
 ('SDL_Surface *achievement_art[3]={};','SDL_Surface *dashboard_title=nullptr; // dash.xex/FFFE07D1, per-era XDBF image\nSDL_Surface *achievement_art[3]={};'),
 ('    notify_scene::clear();','    notify_scene::clear();\n    SDL_FreeSurface(dashboard_title);dashboard_title=nullptr;')])
edit('assets/update_art.inc.hpp',[
 ('    if (artwork_build==model.dashboard_build && !library_changed) return;', '''    if (artwork_build==model.dashboard_build && !library_changed) return;
    // dash.xex/FFFE07D1: extracted by tools/build_small_items.py, never inferred.
    std::string title_root=std::string(root)+"era-skins/";
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) title_root=std::string(override)+"/";
#endif
    SDL_FreeSurface(dashboard_title);
    dashboard_title=load_png(title_root+std::to_string(notify_scene::selected_build())+"/dashboard-title.png");
    if (dashboard_title) SDL_SetSurfaceBlendMode(dashboard_title,SDL_BLENDMODE_BLEND);''')])
edit('ui/guide.inc.hpp',[
 ('    SDL_Surface *icon=nullptr;\n    if (entry.label', '''    SDL_Surface *icon=nullptr;
    if (entry.action==dash::GuideAction::MyXbox && dashboard_title) {
        blit_contained(dashboard_title,{right-h+6,y+5,h-10,h-10});return;
    }
    if (entry.label''')])
edit('runtime/blades_guide_scene.inc.hpp',[
 ('                    SDL_Surface *art=nullptr;','                    SDL_Surface *art=e.action==dash::GuideAction::MyXbox?dashboard_title:nullptr;')])
edit('runtime/music_player.inc.hpp',[
 ('void load_chosen_covers();','void load_chosen_covers();\nvoid load_album_info();'),
 ('    load_chosen_covers();','    load_chosen_covers();\n    load_album_info();')])
edit('runtime/music_library.inc.hpp',[
 ('const char *kChosenCovers="/data/nxe/music-art/chosen.txt";', '''const char *kChosenCovers="/data/nxe/music-art/chosen.txt";
const char *kAlbumInfo="/data/nxe/music-art/album-info.txt";
musicmeta::Edits album_edits;
std::vector<std::string> album_paths;
void apply_album_info() {
    for (auto &song:music_songs) if (auto it=album_edits.find(song.path);it!=album_edits.end()) {
        const auto f=musicmeta::apply({song.album,song.artist,song.genre},it->second);
        song.album=f[0];song.artist=f[1];song.genre=f[2];
    }
}
void load_album_info() {
    const std::string data=ps5lib::read_file(kAlbumInfo,4*1024*1024);
    if (data.empty()) album_edits.clear();
    else if (!musicmeta::decode(data,album_edits)) log("music: invalid album-info sidecar");
    apply_album_info();
}'''),
 ('    case dash::Model::MusicSong:\n','''    case dash::Model::MusicAlbumInfo:
        page_kind="Album";group_name="Edit Album Info";break; // 1025_EditAlbumInfo
    case dash::Model::MusicSong:
'''),
 ('    switch (op) {\n    case Op::OpenSong:', '''    switch (op) {
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
        model.music_album_original=model.music_album_fields;
        browse();break;
    }
    case Op::SaveAlbumInfo: {
        const auto edits=musicmeta::changed(album_edits,album_paths,model.music_album_original,model.music_album_fields);
        mkdir("/data/nxe",0777);mkdir("/data/nxe/music-art",0777);
        if (album_paths.empty() || !musicmeta::save(kAlbumInfo,edits)) {
            model.show("Edit Album Info","The album info could not be saved."); // Ex-Box addition: no 360 original error
            break;
        }
        album_edits=edits;apply_album_info();
        // Re-find the renamed album; sorting can move it or merge it with another.
        model.music_page=dash::Model::MusicLibrary;model.music_group=-1;
        const auto albums=groups(1);
        for (size_t i=0;i<albums.size();i++) for (int song:albums[i].songs)
            if (music_songs[size_t(song)].path==album_paths.front()) model.music_group=int(i);
        model.viewer_item=model.music_library_item;browse();break;
    }
    case Op::OpenSong:''')])
edit('ui/media_pages.inc.hpp',[
 ('    else music_page::draw_skinned();\n}', '''    else music_page::draw_skinned();
    if (model.music_page==dash::Model::MusicAlbumInfo && model.music_album_field>=0) {
        // Ex-Box addition: no 360 original keyboard. The surrounding page uses the era's skin.
        shade(0,0,W,H,0x000000,190);
        constexpr int x=390,y=190,cw=112,ch=66;
        gradient(x-30,y-30,1180,690,0x384339,0x182119);
        const char *fields[]={"Album","Artist","Genre"}; // music/1025_EditAlbumInfo.xur
        text(fields[model.music_album_field],x,y,large,{255,255,255,255});
        text(fit(model.music_album_input+"|",regular,1100),x,y+64,regular,{220,235,210,255});
        const std::string keys=dash::Model::music_album_keys;
        for (size_t i=0;i<keys.size();i++) {
            const int kx=x+int(i%10)*cw,ky=y+132+int(i/10)*ch;
            if (int(i)==model.music_album_key) rect(kx,ky,cw-8,ch-8,0x548d27);
            text(keys[i]==' '?"_":std::string(1,keys[i]),kx+32,ky+8,regular,{255,255,255,255});
        }
    }
}''')])
edit('tests/test_dash.cpp',[
 ('#include "../subtitles.hpp"','#include "../subtitles.hpp"\n#include "../music_metadata.hpp"'),
 ('m.songs = 9; // its options (Play Album, Add to Current Playlist, Choose Cover) and six songs','m.songs = 10; // four options, including Edit Album Info, and six songs'),
 ('m.music_group_actions().size() == 3 && m.music_group_actions()[0] == "Play Album"','m.music_group_actions().size() == 4 && m.music_group_actions()[0] == "Play Album"'),
 ('int main()\n{',r'''static void album_info_edits_and_storage()
{
    Model m;m.viewer=Viewer::Music;m.music_view=1;m.music_group=0;m.songs=6;m.viewer_item=3;
    auto e=m.press(xb::A);
    CHECK(e.command==Command::MusicEdit && e.app==int(MusicOp::OpenAlbumInfo));
    CHECK(m.music_page==Model::MusicAlbumInfo && m.viewer_count()==4 && has_hint(m,xb::B,"Cancel"));
    m.music_album_original=m.music_album_fields={"Album","","Rock"};
    m.press(xb::A);CHECK(m.music_album_field==0);
    m.press(xb::X);m.press(xb::A);CHECK(m.music_album_input=="AlbuA");
    m.press(xb::B);CHECK(m.music_album_fields[0]=="Album");
    m.press(xb::A);m.music_album_input="Renamed";m.press(xb::Y);
    CHECK(m.music_album_fields[0]=="Renamed" && m.music_album_field==-1);
    m.viewer_item=3;e=m.press(xb::A);
    CHECK(e.app==int(MusicOp::SaveAlbumInfo) && m.music_page==Model::MusicAlbumInfo);
    // Failure leaves the editor/draft available for retry; B discards the draft.
    m.show("Edit Album Info","failure");m.press(xb::B);m.press(xb::B);
    CHECK(m.music_page==Model::MusicLibrary && m.music_album_fields==m.music_album_original && m.viewer_item==3);
    m.press(xb::A);m.press(xb::A);m.music_album_input="\xc3\xa9";m.press(xb::X);
    CHECK(m.music_album_input.empty());
    m.music_album_input=std::string(128,'x');m.press(xb::A);CHECK(m.music_album_input.size()==128);
    m.music_album_key=0;m.press(xb::Left);CHECK(m.music_album_key==int(std::string(Model::music_album_keys).size())-1);
    m.press(xb::Right);CHECK(m.music_album_key==0);
    musicmeta::Edits edits;
    const musicmeta::Fields before{"Album","","Rock"},after{"Renamed","",""};
    edits=musicmeta::changed(edits,{"/music/a.mp3","/music/b.mp3"},before,after);
    CHECK(edits.size()==2 && edits.at("/music/a.mp3").mask==5);
    const auto applied=musicmeta::apply({"Album","Different artist","Rock"},edits.at("/music/b.mp3"));
    CHECK(applied[0]=="Renamed" && applied[1]=="Different artist" && applied[2].empty());
    CHECK(musicmeta::changed({}, {"/music/a.mp3"},before,before).empty());
    edits["/music/quote\"\\.mp3"]={musicmeta::Fields{"A\"B","line\nartist",""},7};
    musicmeta::Edits loaded;
    const auto saved=musicmeta::encode(edits);
    CHECK(musicmeta::decode(saved,loaded) && musicmeta::encode(loaded)==saved);
    CHECK(!musicmeta::decode("EXBOX-ALBUM-1\n\"/music/a\" 8 \"x\" \"y\" \"z\"",loaded));
    CHECK(!musicmeta::decode("EXBOX-ALBUM-1\n\"/music/a\" 1 \"truncated",loaded));
    CHECK(musicmeta::encode(loaded)==saved);
}

int main()
{
    album_info_edits_and_storage();''')])
print('T13 source edits applied')

