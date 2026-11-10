#include "../dash_model.hpp"
#include <cstdio>
#include <set>
#include <deque>
using namespace dash;
int checks=0,fail=0;
#define CHECK(x) do{++checks;if(!(x)){++fail;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
int main(){
 for(int build:{1888,6717,9199,13604,15574,17559}) for(int count:{0,1,30}) {
  const int era=build<7000?0:build<10000?1:build<15000?2:3;
  Model m;m.welcome_visible=false;m.era=era;m.dashboard_build=build;m.viewer=Viewer::Music;m.songs=count;m.now_playing=0;
  const Model before=m;const auto up=m.press(xb::Up);CHECK(sound_for(before,m,xb::Up,up)==Sound::Focus);CHECK(m.music_tabs_focused);CHECK(m.viewer_item==0);
  // Pure D-pad graph: no shortcuts, direct selection assignment, or A needed.
  std::set<std::tuple<bool,bool,bool,int,int>> seen; std::deque<Model> todo{m};
  while(!todo.empty()) {auto at=todo.front();todo.pop_front();
    if(!seen.insert({at.music_tabs_focused,at.music_random_focused,at.music_game_scan_focused,at.music_view,at.viewer_item}).second)continue;
    for(auto key:{xb::Up,xb::Down,xb::Left,xb::Right}){auto next=at;next.press(key);todo.push_back(next);}
  }
  for(int v=0;v<kMusicViews;++v){CHECK(seen.count({true,false,false,v,0})==1);for(int r=0;r<count;++r)CHECK(seen.count({false,false,false,v,r})==1);}
  CHECK(seen.count({true,true,false,kMusicViews-1,0})==1);CHECK(seen.count({true,false,true,kMusicViews-1,0})==1);
  std::set<int> reached{m.music_view};
  for(int i=1;i<kMusicViews;++i){
   auto e=m.press(xb::Right);CHECK(e.command==Command::MusicBrowse);CHECK(m.music_tabs_focused);CHECK(m.music_group==-1);CHECK(m.viewer_item==0);reached.insert(m.music_view);
  }
  CHECK(reached.size()==kMusicViews);m.press(xb::Right);CHECK(m.music_random_focused);
  m.press(xb::Right);CHECK(m.music_game_scan_focused);auto game=m.press(xb::A);CHECK(game.command==Command::ScanGameMusic);CHECK(m.music_game_scan);
  m.press(xb::Right);CHECK(m.music_view==0);CHECK(!m.music_random_focused&&!m.music_game_scan_focused);
  m.press(xb::Left);CHECK(m.music_game_scan_focused);m.press(xb::Down);CHECK(!m.music_tabs_focused);CHECK(m.viewer_item==0);
  // A selector acceptance never activates the first song/group.
  m.press(xb::Up);auto e=m.press(xb::A);CHECK(!m.music_tabs_focused);CHECK(m.music_page==Model::MusicLibrary);CHECK(e.command==Command::None);
  // Track skip remains available only in the list, never on the selector.
  e=m.press(xb::Right);CHECK(e.command==Command::MusicNext);
  e=m.press(xb::Left);CHECK(e.command==Command::MusicPrevious);
  m.press(xb::Up);m.press(xb::RT);CHECK(m.music_view==1);CHECK(m.music_tabs_focused);m.press(xb::LT);CHECK(m.music_view==0);
  m.press(xb::B);CHECK(m.viewer==Viewer::None);CHECK(!m.music_tabs_focused);
  m.viewer=Viewer::Music;m.music_group=2;m.viewer_item=0;m.press(xb::Up);CHECK(m.music_tabs_focused);
  m.press(xb::Right);CHECK(m.music_group==-1);m.press(xb::Y);CHECK(!m.music_tabs_focused);CHECK(m.music_page==Model::MusicNowPlaying);
  m.press(xb::B);CHECK(m.music_page==Model::MusicLibrary);
  // A programmatic Now Playing/Guide entry cannot expose stale tab hints.
  m.music_tabs_focused=true;m.music_page=Model::MusicNowPlaying;
  for(const auto &h:m.hints())CHECK(h.label!="Next View" && h.label!="Previous View");
  m.press(xb::B);CHECK(m.music_page==Model::MusicLibrary);CHECK(!m.music_tabs_focused);
  m.music_view=1;m.music_group=2;m.viewer_item=0;m.press(xb::Up);m.press(xb::B);
  CHECK(m.viewer==Viewer::Music);CHECK(m.music_group==-1);CHECK(m.viewer_item==2);CHECK(!m.music_tabs_focused);
  m.music_tabs_focused=true;m.perform_guide_entry({"Music Library",GuideAction::Music});
  CHECK(!m.music_tabs_focused);CHECK(m.music_page==Model::MusicLibrary);
  if(count>1){m.viewer_item=2;m.press(xb::Up);CHECK(m.viewer_item==1);CHECK(!m.music_tabs_focused);m.press(xb::Up);CHECK(!m.music_tabs_focused);m.press(xb::Up);CHECK(m.music_tabs_focused);}
 }
 for(int era=0;era<4;++era){
  Model m;m.welcome_visible=false;m.era=era;m.viewer=Viewer::Videos;
  for(int i=0;i<17;++i)m.trailers.push_back("video"+std::to_string(i));
  std::set<int> seen;std::deque<Model> todo{m};
  while(!todo.empty()){auto at=todo.front();todo.pop_front();if(!seen.insert(at.viewer_item).second)continue;
    for(auto key:{xb::Up,xb::Down,xb::Left,xb::Right}){auto next=at;next.press(key);todo.push_back(next);}}
  CHECK(seen.size()==17);
 }
 // Existing dashboard pivots must remain reachable without shoulder shortcuts.
 for(int build:{15574,17559}) {
  Model m;m.welcome_visible=false;m.era=3;m.dashboard_build=build;
  m.select_metro_tab(1);m.press(xb::Up);CHECK(m.metro_focus==-1);
  for(int i=0;i<9;++i){m.press(xb::Left);}CHECK(m.metro_tab==0);
  std::set<int> tabs;
  for(int i=0;i<8;++i){tabs.insert(m.metro_tab);m.press(xb::Down);CHECK(m.metro_focus>=0);m.press(xb::Up);CHECK(m.metro_focus==-1);m.press(xb::Right);}
  CHECK(tabs.size()==8);
  // Content has a seamless horizontal path between adjacent dashboard tabs;
  // Up remains the direct rail shortcut. No shoulder button is required.
  for(int tab=1;tab<7;++tab){
   m.select_metro_tab(tab);CHECK(m.metro_focus>=0);const int before=m.metro_tab;
   for(int step=0;step<32 && m.metro_tab==before;++step)m.press(xb::Right);
   CHECK(m.metro_tab==before+1);CHECK(m.metro_focus>=0);
   m.press(xb::Up);CHECK(m.metro_focus==-1);
   m.press(xb::Down);CHECK(m.metro_focus>=0);
   const int after=m.metro_tab;
   for(int step=0;step<32 && m.metro_tab==after;++step)m.press(xb::Left);
   CHECK(m.metro_tab==after-1);CHECK(m.metro_focus>=0);
  }
 }
 // Settings rows use their existing vertical focus; walk the entire root
 // and console submenu using D-pad only, then activate the selected row.
 for(int build:{9199,13604,15574,17559}) {
  Model m;m.welcome_visible=false;m.era=build<10000?1:build<15000?2:3;m.dashboard_build=build;m.viewer=Viewer::SystemSettings;
  for(int page:{Model::SystemRoot,Model::SystemConsole,Model::SystemDisplay,Model::SystemAudio,Model::SystemResolution}){
   m.system_page=page;m.viewer_item=0;const int n=int(m.system_entries().size());
   for(int r=0;r<n;++r){CHECK(m.viewer_item==r);m.press(xb::Down);}
   for(int r=std::max(0,n-1);r>=0;--r){CHECK(m.viewer_item==r);m.press(xb::Up);}
  }
 }
 std::printf("%d checks, %d failures\n",checks,fail);return fail?1:0;
}



