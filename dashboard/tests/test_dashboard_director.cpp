#include "../modules/dashboard_director/dashboard_director.hpp"
#include "../modules/dashboard_director/friends_stage.hpp"
#include <cstdio>
int main(){using namespace exbox::dashboard_director;int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
 Director d;Capabilities caps{true,true,true};Observation s;auto p=d.observe(s,caps);CHECK(p.count==1&&p.actions[0].event==Event::ViewEnter);
 s.focus=1;s.guide=true;s.notification=true;s.playback=true;p=d.observe(s,caps);CHECK(p.count==4);CHECK(p.actions[1].marker=="ClosedToHalf");CHECK(p.actions[2].marker=="TransTo");CHECK(p.actions[3].marker=="Normal");
 s.guide=false;s.notification=false;s.paused=true;p=d.observe(s,caps);CHECK(p.count==3);CHECK(p.actions[0].marker=="HalfToClosed");CHECK(p.actions[1].marker=="TransFrom");CHECK(p.actions[2].marker=="NormalDisable");
 p=d.observe(s,caps);CHECK(p.count==0);
 Director unsupported;unsupported.observe({},{});s={};s.guide=true;p=unsupported.observe(s,{});CHECK(p.count==1&&p.actions[0].evidence==Evidence::Unsupported&&p.actions[0].marker.empty());
 Director f;std::array<bool,2> both{true,true};CHECK(f.idle_fidget(1000,true,both).empty());const auto a=f.idle_fidget(22000,true,both);CHECK(a=="look"||a=="shift");const auto b=f.idle_fidget(70000,true,both);CHECK(b=="look"||b=="shift");CHECK(a!=b);CHECK(f.idle_fidget(71000,false,both).empty());
 std::array<bool,2> none{false,false};Director n;CHECK(n.idle_fidget(1,true,none).empty());CHECK(n.idle_fidget(30000,true,none).empty());
 std::array<bool,2> only_look{true,false};Director one;CHECK(one.idle_fidget(1,true,only_look).empty());CHECK(one.idle_fidget(30000,true,only_look)=="look");CHECK(one.idle_fidget(80000,true,only_look).empty());
 using exbox::friends_stage::Status;CHECK(exbox::friends_stage::route(Family::MetroV1,true).status==Status::Ready);CHECK(exbox::friends_stage::route(Family::MetroV1,true).bounds.h==590);const auto owner=exbox::friends_stage::route(Family::MetroV2,true).bounds;CHECK(owner.x+owner.w/2>=824&&owner.x+owner.w/2<=856);CHECK(exbox::friends_stage::route(Family::Nxe,true).status==Status::Blocked);CHECK(!exbox::friends_stage::route(Family::Nxe,true).reason.empty());CHECK(exbox::friends_stage::route(Family::Blades,false).status==Status::Hidden);
 AvatarCapabilities clips{true,true,true,true,false};CHECK(avatar_cue(AvatarMoment::Startup,clips).clip=="idle");CHECK(avatar_cue(AvatarMoment::Return,clips).clip=="idle");CHECK(avatar_cue(AvatarMoment::EnterStage,clips).clip=="idle");CHECK(avatar_cue(AvatarMoment::Selection,clips).clip=="wave");CHECK(avatar_cue(AvatarMoment::Achievement,clips).clip=="dance");CHECK(avatar_cue(AvatarMoment::EditorSave,clips).clip=="dance");CHECK(!avatar_cue(AvatarMoment::FocusLook,clips));clips.focus_look_bound=true;CHECK(avatar_cue(AvatarMoment::FocusLook,clips).clip=="look");clips.wave=false;CHECK(!avatar_cue(AvatarMoment::Selection,clips));
 PromotionClock rotation;CHECK(!rotation.tick(1000,true,false));CHECK(!rotation.tick(50000,true,false));CHECK(!rotation.tick(91000,true,true));CHECK(!rotation.tick(95000,true,false));CHECK(rotation.tick(96000,true,false));CHECK(!rotation.tick(96001,false,false));CHECK(!rotation.tick(200000,true,false));
 std::printf("%d checks, %d failed\n",checks,failures);return failures?1:0;}
