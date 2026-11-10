#include "../dash_model.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace dash;
static int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)

static int row_with(const std::vector<GuideEntry> &rows,const std::string &text)
{
    const auto at=std::find_if(rows.begin(),rows.end(),[&](const GuideEntry &row){return row.label.find(text)!=std::string::npos;});
    return at==rows.end()?-1:int(at-rows.begin());
}

int main()
{
    for(int era=0;era<4;++era){
        Model m;m.era=era;m.dashboard_build=era==0?6717:era==1?9199:era==2?13604:17559;
        m.set_games({{"Fallout 4","","CUSA03450","/user/appmeta/CUSA03450/icon0.png"}});
        m.set_apps({{"Omni","/data/homebrew/Omni/eboot.elf"},{"Fallout Community Launcher","/data/homebrew/Fallout/eboot.elf"}});
        m.open_guide();

        const auto home=m.blade_entries(BladeHome);
        const int play=row_with(home,"Play Fallout 4");
        const int exbox=row_with(home,"Ex-Box Menu");
        CHECK(play>=0 && home[std::size_t(play)].action==GuideAction::LaunchTitle);
        CHECK(exbox>=0 && exbox<7); // visible without scrolling on every Guide skin

        m.guide_item[BladeHome]=exbox;
        m.press(xb::A);
        CHECK(m.guide && m.guide_exbox_page && m.guide_blade==BladeHome);
        const auto menu=m.blade_entries(BladeHome);
        CHECK(row_with(menu,"Games & Homebrew")>=0);
        CHECK(row_with(menu,"Music")==-1); // belongs to Media, not the shortcut page
        const int before=m.guide_item[BladeHome];m.press(xb::Right);
        CHECK(m.guide_item[BladeHome]==before && m.guide_blade==BladeHome);
        m.press(xb::B);
        CHECK(m.guide && !m.guide_exbox_page && m.guide_item[BladeHome]==exbox);

        const auto games=m.blade_entries(BladeGames);
        CHECK(row_with(games,"Fallout 4")>=0);
        // Metro's combined Games & Apps page also shows unclassified homebrew;
        // older eras preserve their separate Games and Apps blades.
        CHECK((row_with(games,"Fallout Community Launcher")>=0)==(era==3));
        CHECK(row_with(games,"Refresh Games & Homebrew")>=0);

        const auto media=m.blade_entries(BladeMedia);
        CHECK(row_with(media,"Music")>=0 && row_with(media,"Picture")>=0 && row_with(media,"Video")>=0);
        CHECK(row_with(media,"Record Screen")>=0);
        CHECK(row_with(media,"Windows Media Center")==-1);
        const auto settings=m.blade_entries(BladeSettings);
        CHECK(row_with(settings,"Themes")>=0 && row_with(settings,"Controller Map")>=0);
        CHECK(row_with(settings,"System Settings")>=0 && row_with(settings,"Avatar Settings")>=0);
        CHECK(row_with(settings,"Family Settings")==-1 && row_with(settings,"Account Management")==-1);
    }

    Model homebrew_only;homebrew_only.era=3;homebrew_only.dashboard_build=17559;
    homebrew_only.set_apps({{"Omni","/data/homebrew/Omni/eboot.elf"},{"Fallout New Vegas","/data/homebrew/FalloutNV/eboot.elf"}});
    CHECK(homebrew_only.preferred_guide_play().label=="Play Fallout New Vegas");

    std::printf("%d checks, %d failed\n",checks,failures);
    return failures?1:0;
}
