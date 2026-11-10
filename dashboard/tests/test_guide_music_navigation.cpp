#include "../dash_model.hpp"
#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>
using namespace dash;
static int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
static bool control(GuideAction a){return a==GuideAction::MusicPause||a==GuideAction::MusicPrevious||a==GuideAction::MusicStop||a==GuideAction::MusicNext||a==GuideAction::MusicShuffle||a==GuideAction::MusicVolume;}
int main(){
 for(int build:{9199,13604,15574,17559})for(int order:{1,2}){
  Model m;m.era=build<10000?1:build<15000?2:3;m.dashboard_build=build;m.now_playing=0;m.now_playing_title="His World";m.media_player=order;m.open_guide();m.guide_blade=BladeMedia;
  const auto rows=m.blade_entries(BladeMedia);std::vector<int> icons;for(std::size_t i=0;i<rows.size();++i)if(control(rows[i].action))icons.push_back(int(i));CHECK(icons.size()==6);
  m.guide_item[BladeMedia]=icons.front();std::set<int> reached{m.guide_item[BladeMedia]};
  for(std::size_t i=1;i<icons.size();++i){m.press(xb::Right);reached.insert(m.guide_item[BladeMedia]);CHECK(m.guide_blade==BladeMedia);}
  CHECK(reached==std::set<int>(icons.begin(),icons.end()));
  for(std::size_t i=1;i<icons.size();++i)m.press(xb::Left);CHECK(m.guide_item[BladeMedia]==icons.front());
  if(icons.front()>0){m.press(xb::Up);CHECK(m.guide_item[BladeMedia]==icons.front()-1);m.press(xb::Down);CHECK(m.guide_item[BladeMedia]==icons.front());}
  m.guide_item[BladeMedia]=icons.back();if(icons.back()+1<int(rows.size())){m.press(xb::Down);CHECK(m.guide_item[BladeMedia]==icons.back()+1);m.press(xb::Up);CHECK(m.guide_item[BladeMedia]==icons.back());}
  m.press(xb::Guide);CHECK(!m.guide);
 }
 std::printf("%d checks, %d failed\n",checks,failures);return failures?1:0;
}
