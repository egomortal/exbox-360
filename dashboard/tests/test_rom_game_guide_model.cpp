#include "../dash_model.hpp"
#include "../modules/games_frontend/rom_guide_input.hpp"
#include "../modules/games_frontend/rom_guide_protocol.hpp"
#include <cstdio>
using namespace exbox_rom_guide;
static int checks=0,failures=0;
#define CHECK(x)do{++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
int main(){
 dash::Model dashboard;auto&r=dashboard.rom_game_guide_preview;CHECK(preview_title[0]&&preview_fallback[0]);CHECK(!r.open());auto x=press(r,xb::Guide);CHECK(r.open()&&!x.request_pause);
 for(const auto&row:r.rows())CHECK(!row.enabled&&row.unavailable_reason[0]);press(r,xb::Down);CHECK(r.selected()==1);CHECK(press(r,xb::A).action==Action::None);CHECK(press(r,xb::B).action==Action::Close);
 Capabilities c{true,true,true,true,true,true};Model live(c);x=press(live,xb::Guide);CHECK(x.request_pause);live.acknowledge_pause(true);
 for(int i=0;i<6;++i)press(live,xb::Down);CHECK(live.selected()==0);x=press(live,xb::A);CHECK(x.action==Action::Close&&x.request_resume);
 Ledger l;Request p{protocol_version,"s",4,1,Command::Pause};CHECK(l.submit(p)==Outcome::New);CHECK(l.submit(p)==Outcome::Duplicate);
 CHECK(l.acknowledge({"s",4,1,Ack::Applied})==Outcome::AckApplied);CHECK(l.submit({protocol_version,"s",3,2,Command::Resume})==Outcome::StaleGeneration);
 Request e{protocol_version,"s",4,2,Command::ExitToDashboard};CHECK(l.submit(e)==Outcome::New);CHECK(!l.may_retry(e));CHECK(l.acknowledge({"s",4,2,Ack::Uncertain})==Outcome::AckUncertain);CHECK(!l.may_retry(e));
 CHECK(l.submit({protocol_version,"s",5,1,Command::QueryStatus})==Outcome::New);CHECK(l.acknowledge({"s",4,2,Ack::Applied})==Outcome::StaleGeneration);
 // Owner-facing reachability: the entry is appended only to Games when the
 // PS5X360 title (or supplied 360 inventory) exists; ordinary Guide stays intact.
 dash::Model ui;ui.welcome_visible=false;ui.set_games({{"PS5X360","","PPSA50011",""},{"Astro","","PPSA00001",""}});ui.channel=dash::Games;
 CHECK(ui.has_rom_game_guide_preview());const int last=ui.count()-1;for(int i=0;i<last;++i)ui.press(xb::Right);
 const dash::Card preview=ui.current_card();CHECK(preview.title==preview_title&&preview.goal==dash::Goal::ToRomGameGuidePreview);
 CHECK(ui.press(xb::A).command==dash::Command::None&&ui.viewer==dash::Viewer::RomGameGuidePreview);
 CHECK(ui.rom_game_guide_preview.open()&&ui.viewer_count()==6);CHECK(ui.rom_game_guide_preview.rows()[1].unavailable_reason[0]);
 const auto disabled_hints=ui.hints();CHECK(disabled_hints.size()==1&&disabled_hints[0].button==xb::B&&disabled_hints[0].label=="Close");
 ui.press(xb::Down);CHECK(ui.viewer_item==1);CHECK(ui.press(xb::A).command==dash::Command::None&&ui.viewer==dash::Viewer::RomGameGuidePreview);
 ui.press(xb::Guide);CHECK(ui.viewer==dash::Viewer::None&&!ui.rom_game_guide_preview.open());
 ui.press(xb::Guide);CHECK(ui.guide);ui.press(xb::B);CHECK(!ui.guide); // normal Guide behavior is unchanged
 ui.selected[dash::Games]=last;ui.press(xb::A);CHECK(ui.viewer==dash::Viewer::RomGameGuidePreview);ui.press(xb::B);CHECK(ui.viewer==dash::Viewer::None);
 dash::Model absent;absent.set_games({{"Astro","","PPSA00001",""}});absent.channel=dash::Games;CHECK(!absent.has_rom_game_guide_preview()&&absent.count()==1);
 std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
