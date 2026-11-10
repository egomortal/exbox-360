// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/dashboard_services/dashboard_diagnostics.hpp"
#include "../modules/dashboard_services/promotion_scheduler.hpp"
#include "../modules/dashboard_services/rom_guide_companion.hpp"
#include "../modules/dashboard_services/theme_scene_capabilities.hpp"

#include <cstdio>
#include <string>
#include <vector>

static int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);}}while(0)

int main(){
    using namespace exbox_services;
    {
        using namespace promotions;
        Context c;c.era=3;c.page=1;c.now_utc=1000;c.current_game_id="game:sonic06";c.recent_game_ids={"game:halo3"};c.rotation_generation=4;
        std::vector<Candidate> ads={{"authentic",true,true,1u<<3,1u<<1,"",0,0,1},{"sonic",true,false,1u<<3,1u<<1,"game:sonic06",0,0,0},{"expired",true,true,1u<<3,1u<<1,"",1,900,50},{"foreign",false,true,1u<<3,1u<<1,"",0,0,100}};
        CHECK(!choose(ads,Mode::Off,c));auto authentic=choose(ads,Mode::Authentic,c);CHECK(authentic&&authentic.stable_id=="authentic"&&!authentic.personalized);
        auto local=choose(ads,Mode::Local,c);CHECK(local&&local.stable_id=="sonic"&&local.personalized);
        auto no_repeat=choose(ads,Mode::Authentic,c,"authentic");CHECK(no_repeat&&no_repeat.stable_id=="authentic");
        ads.push_back({"authentic-two",true,true,1u<<3,1u<<1,"",0,0,0});no_repeat=choose(ads,Mode::Authentic,c,"authentic");CHECK(no_repeat&&no_repeat.stable_id=="authentic-two");
        c.page=2;CHECK(!choose(ads,Mode::Authentic,c));std::vector<Candidate> oversized(257);CHECK(!choose(oversized,Mode::Local,c));c.recent_game_ids.resize(33);CHECK(!choose(ads,Mode::Local,c));
    }
    {
        using namespace theme_scene;const std::uint32_t visual=bit(Role::Background)|bit(Role::Floor)|bit(Role::Foreground)|bit(Role::ChannelBanner);
        Evidence evidence;evidence.present_roles=visual;evidence.owner_assets_verified=true;
        auto d=admit(RequestMode::AuthoredExact,visual,evidence,true);CHECK(d.status==Status::DefaultBackground&&d.applied_roles==0);
        evidence.route_table_proven=evidence.z_order_proven=evidence.crop_sampling_proven=true;d=admit(RequestMode::AuthoredExact,visual,evidence,true);CHECK(d.status==Status::ExactScene&&d.applied_roles==visual);
        d=admit(RequestMode::AuthoredExact,visual|bit(Role::Banner),evidence,true);CHECK(d.status==Status::DefaultBackground&&d.applied_roles==0);
        const auto lighting=visual|bit(Role::AvatarDirectionalLight);evidence.present_roles=lighting;d=admit(RequestMode::AuthoredExact,lighting,evidence,true);CHECK(d.status==Status::DefaultBackground);
        evidence.lighting_format_proven=true;d=admit(RequestMode::AuthoredExact,lighting,evidence,true);CHECK(d.status==Status::ExactScene);
        d=admit(RequestMode::Compatibility,visual,Evidence{},true);CHECK(d.status==Status::CompatibilityWallpaper&&d.applied_roles==0);
        d=admit(RequestMode::Compatibility,visual,Evidence{},false);CHECK(d.status==Status::Unavailable);
        d=admit(RequestMode::AuthoredExact,1u<<31,evidence,true);CHECK(d.status==Status::DefaultBackground&&d.applied_roles==0);
    }
    {
        using namespace rom_guide_companion;Advertisement ad;CHECK(!negotiate(ad).accepted);CHECK(!negotiate(static_cast<const AdvertisementAdapter*>(nullptr)).accepted);ad.host_kind="ps5x360-cooperative-v1";ad.session_id="session-1";ad.generation=1;ad.exclusive_overlay_owner=true;ad.request_acknowledgements=true;ad.request_deduplication=true;ad.pause_resume=true;ad.return_dashboard=true;ad.controller_map=true;ad.music=true;ad.settings=true;
        auto n=negotiate(ad);CHECK(n.accepted&&n.capabilities.cooperative_overlay&&n.capabilities.pause_resume);CHECK(!n.capabilities.return_dashboard);
        ad.safe_exit_ack=true;n=negotiate(ad);CHECK(n.accepted&&n.capabilities.return_dashboard&&n.capabilities.controller_map&&n.capabilities.music&&n.capabilities.settings);
        ad.exclusive_overlay_owner=false;CHECK(!negotiate(ad).accepted);ad.exclusive_overlay_owner=true;ad.session_id="bad session";CHECK(!negotiate(ad).accepted);
        ad.session_id="session-1";ad.version=99;CHECK(!negotiate(ad).accepted);
        struct Source final:AdvertisementAdapter{Advertisement value;bool present=true;bool read(Advertisement&out)const override{if(!present)return false;out=value;return true;}} source;
        exbox_rom_guide::Model model({true,true,true,true,true,true});auto absent=apply(model,nullptr);CHECK(!absent.accepted&&!model.capabilities().cooperative_overlay);
        source.value=ad;source.value.version=advertisement_version;source.value.safe_exit_ack=true;auto applied=apply(model,&source);CHECK(applied.accepted&&model.capabilities().return_dashboard);
        source.present=false;applied=apply(model,&source);CHECK(!applied.accepted&&!model.capabilities().cooperative_overlay);
    }
    {
        using namespace diagnostics;Metrics metrics;metrics.add_frame(20);metrics.add_upload(100);CHECK(!metrics.snapshot().enabled&&metrics.snapshot().frame_samples==0);
        metrics.set_enabled(true);for(int i=1;i<=100;++i)metrics.add_frame(double(i)/10.0);metrics.add_frame(20);metrics.add_upload(4096);metrics.add_command_batch();metrics.add_fallback();metrics.set_gpu_bytes(200ull*1024*1024);metrics.set_gpu_bytes(241ull*1024*1024);metrics.add_missing_asset();metrics.add_unsupported_format();
        auto s=metrics.snapshot();CHECK(s.enabled&&s.frame_samples==101&&s.p50_ms>=5&&s.p50_ms<=5.2&&s.p99_ms==10&&s.max_ms==20);CHECK(s.frames_over_16_667==1&&s.uploads==1&&s.upload_bytes==4096&&s.command_batches==1&&s.fallbacks==1);CHECK(s.gpu_peak_bytes==241ull*1024*1024&&s.over_budget&&s.missing_assets==1&&s.unsupported_formats==1);CHECK(s.uploads_published&&s.gpu_memory_published&&s.missing_assets_published&&s.unsupported_formats_published);
        for(int i=0;i<300;++i)metrics.add_frame(1);CHECK(metrics.snapshot().frame_samples==frame_capacity);metrics.set_enabled(false);s=metrics.snapshot();CHECK(!s.enabled&&s.frame_samples==0&&s.uploads==0);
    }
    std::printf("%d checks, %d failed\n",checks,failures);return failures?1:0;
}
