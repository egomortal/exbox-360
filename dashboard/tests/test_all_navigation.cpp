#include "../dash_model.hpp"

#include <cstdio>
#include <deque>
#include <set>
#include <string>
#include <tuple>

using namespace dash;
static int checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);} } while(0)

static std::set<int> viewer_rows(Model seed)
{
    std::set<int> seen;std::deque<Model> work{seed};
    while(!work.empty()){
        Model at=work.front();work.pop_front();
        if(at.viewer!=seed.viewer||!seen.insert(at.viewer_item).second)continue;
        for(auto key:{xb::Up,xb::Down,xb::Left,xb::Right}){Model next=at;next.press(key);work.push_back(std::move(next));}
    }
    return seen;
}

static void media_grids_are_spatial_and_connected()
{
    for(int era=0;era<4;++era){
        Model videos;videos.era=era;videos.viewer=Viewer::Videos;
        for(int i=0;i<10;++i)videos.trailers.push_back("video-"+std::to_string(i));
        CHECK(viewer_rows(videos).size()==10);
        videos.viewer_item=3;videos.press(xb::Right);CHECK(videos.viewer_item==3);
        videos.viewer_item=4;videos.press(xb::Left);CHECK(videos.viewer_item==4);
        videos.viewer_item=0;videos.press(xb::Down);CHECK(videos.viewer_item==4);
        videos.viewer_item=9;videos.press(xb::Up);CHECK(videos.viewer_item==5);

        Model pictures;pictures.era=era;pictures.viewer=Viewer::Pictures;std::vector<PictureFile> files;
        for(int i=0;i<10;++i)files.push_back({"shot-"+std::to_string(i)+".png","","","20261105_"+std::to_string(i),0});
        pictures.set_pictures(std::move(files));
        CHECK(viewer_rows(pictures).size()==10);
        pictures.viewer_item=7;pictures.press(xb::Right);CHECK(pictures.viewer_item==7);
        pictures.viewer_item=8;pictures.press(xb::Left);CHECK(pictures.viewer_item==8);
    }
}

static void every_vertical_viewer_row_is_dpad_reachable()
{
    Model profile;profile.viewer=Viewer::Profile;CHECK(viewer_rows(profile).size()==profile.profile_entries().size());

    Model themes;themes.viewer=Viewer::Themes;themes.picture_background=0;
    themes.set_installed_themes({{"theme-1111111111111111","Alpha",true,4},{"theme-2222222222222222","Beta",false,1}});
    CHECK(viewer_rows(themes).size()==std::size_t(themes.theme_count()));

    Model achievements;achievements.viewer=Viewer::Achievements;achievements.achievement_game="one";
    achievements.achievement_source["one"]={true,{{"A","First","",TrophyGrade::Bronze,true,false},{"B","Second","",TrophyGrade::Silver,false,false}}};
    CHECK(viewer_rows(achievements).size()==std::size_t(achievements.achievement_count()));

    Model guide_preview;guide_preview.viewer=Viewer::RomGameGuidePreview;
    guide_preview.rom_game_guide_preview.press(exbox_rom_guide::Button::Guide);
    CHECK(viewer_rows(guide_preview).size()==guide_preview.rom_game_guide_preview.rows().size());
}

static void guide_rows_and_horizontal_groups_are_dpad_reachable()
{
    for(int build:{1888,6717,9199,13604,15574,17559}){
        Model base;base.era=build<7000?0:build<10000?1:build<15000?2:3;base.dashboard_build=build;base.open_guide();
        for(int blade=0;blade<BladeCount;++blade){
            Model m=base;m.guide_blade=blade;m.guide_item[blade]=0;
            const int n=int(m.blade_entries(blade).size());std::set<int> rows;std::set<std::pair<int,int>> seen;std::deque<Model> work{m};
            while(!work.empty()&&seen.size()<128){Model at=work.front();work.pop_front();const auto state=std::make_pair(at.guide_blade,at.guide_item[at.guide_blade]);if(!seen.insert(state).second)continue;if(at.guide_blade==blade)rows.insert(at.guide_item[blade]);for(auto key:{xb::Up,xb::Down,xb::Left,xb::Right}){Model next=at;next.press(key);work.push_back(std::move(next));}}
            CHECK(rows.size()==std::size_t(std::max(1,n)));
        }
        base.guide_blade=BladeMedia;base.media_player=1;base.now_playing=0;base.now_playing_title="Song";
        const auto entries=base.blade_entries(BladeMedia);std::set<int> controls;
        for(std::size_t i=0;i<entries.size();++i){const auto a=entries[i].action;if(a==GuideAction::MusicPause||a==GuideAction::MusicPrevious||a==GuideAction::MusicStop||a==GuideAction::MusicNext||a==GuideAction::MusicShuffle||a==GuideAction::MusicVolume)controls.insert(int(i));}
        if(!controls.empty()){
            base.guide_item[BladeMedia]=*controls.begin();std::set<int> reached;
            for(std::size_t i=0;i<controls.size();++i){reached.insert(base.guide_item[BladeMedia]);base.press(xb::Right);}
            CHECK(reached==controls);
        }
    }
}

static void system_and_picker_rows_are_dpad_reachable()
{
    for(int build:{9199,13604,15574,17559}){
        Model settings;settings.era=build<10000?1:build<15000?2:3;settings.dashboard_build=build;settings.viewer=Viewer::SystemSettings;
        for(int page=Model::SystemRoot;page<=Model::SystemDiagnostics;++page){
            settings.system_page=page;settings.viewer_item=0;const int n=int(settings.system_entries().size());
            if(n)CHECK(viewer_rows(settings).size()==std::size_t(n));
            else {settings.press(xb::B);CHECK(settings.system_page==Model::SystemRoot||!settings.system_history.empty());}
        }
    }
    Model picker;picker.viewer=Viewer::DashboardEras;CHECK(viewer_rows(picker).size()==5);
    picker.viewer=Viewer::DashboardUpdates;picker.picker_family=1;picker.viewer_item=0;
    CHECK(viewer_rows(picker).size()==updates_for(1,picker.picker_previews).size());
}

int main()
{
    media_grids_are_spatial_and_connected();
    every_vertical_viewer_row_is_dpad_reachable();
    guide_rows_and_horizontal_groups_are_dpad_reachable();
    system_and_picker_rows_are_dpad_reachable();
    std::printf("%d checks, %d failed\n",checks,failures);return failures?1:0;
}
