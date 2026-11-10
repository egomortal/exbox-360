#include "../dash_model.hpp"
#include <cstdio>
int main(){int checks=0,failed=0;
 auto check=[&](bool yes){++checks;if(!yes)++failed;};
 for(unsigned build:{14719u,17559u}){
  dash::Model m;m.era=3;m.dashboard_build=build;m.select_metro_tab(2);m.metro_focus=0;
  m.press(xb::Right);check(m.metro_focus==dash::kMetroSocialOwner);
  for(int actor=1;actor<=3;++actor){m.press(xb::Right);check(m.metro_focus==dash::kMetroSocialFriend0+actor-1);auto action=m.press(xb::A);check(action.command==dash::Command::AvatarWave&&action.app==actor);check(m.viewer==dash::Viewer::None);}
  m.press(xb::Right);check(m.metro_tab==3);
  m.select_metro_tab(2);m.metro_focus=dash::kMetroSocialOwner;m.press(xb::A);check(m.viewer==dash::Viewer::Profile);
 }
 std::printf("Social avatar controls %d/%d PASS\n",checks-failed,checks);return failed?1:0;
}
