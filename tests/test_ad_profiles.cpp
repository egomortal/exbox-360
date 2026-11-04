// SPDX-License-Identifier: GPL-3.0-or-later
#include "../dash_model.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace {
constexpr const char *root="/unavailable-exbox-test-root/";
dash::Model model;
std::vector<std::string> read_lines(const char *path,size_t limit) {
    std::ifstream input(path);std::vector<std::string> lines;std::string line;
    while (lines.size()<limit && std::getline(input,line)) lines.push_back(line);
    return lines;
}
#include "../assets/promotions.inc.hpp"
int failures=0,checks=0;
#define CHECK(test) do {checks++;if (!(test)) {failures++;std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#test);}} while (0)
std::vector<std::string> images() {
    std::vector<std::string> result;for (const auto &card:model.promo_cards) result.push_back(card.image);return result;
}
}

int main(int argc,char **argv) {
    if (argc!=3) {std::fprintf(stderr,"Usage: test-ad-profiles preserved-promotion-root temporary-profile-path\n");return 2;}
    setenv("NXE_PROMOTION_ROOT",argv[1],1);setenv("NXE_AD_PROFILE_PATH",argv[2],1);
    load_ad_catalog();CHECK(model.ad_catalog.size()==957);
    CHECK(std::count_if(model.ad_catalog.begin(),model.ad_catalog.end(),[](const dash::AdEntry &ad){return ad.classification=="official-store-artwork";})==14);
    CHECK(std::count_if(model.ad_catalog.begin(),model.ad_catalog.end(),[](const dash::AdEntry &ad){return ad.auto_eligible;})==926);
    CHECK(std::count_if(model.ad_catalog.begin(),model.ad_catalog.end(),[](const dash::AdEntry &ad){return ad.role=="web-button-state-sheet" && !ad.auto_eligible;})==5);
    CHECK(std::count_if(model.ad_catalog.begin(),model.ad_catalog.end(),[](const dash::AdEntry &ad){return ad.preferred_fit=="match_slot_aspect";})==773);
    CHECK(std::count_if(model.ad_catalog.begin(),model.ad_catalog.end(),[](const dash::AdEntry &ad){return ad.caption_source=="original-feed";})==689);
    CHECK(std::count_if(model.ad_catalog.begin(),model.ad_catalog.end(),[](const dash::AdEntry &ad){return ad.caption_source=="archive-filename";})==254);
    CHECK(!ad_identity(std::string(64,'g')));CHECK(ad_identity(std::string(64,'a')));
    std::string unsafe;CHECK(!ad_image_path("../catalog.tsv",promotion_directory(),unsafe));
    const int eras[]={0,0,1,2,3,3},builds[]={1888,6717,9199,13604,15574,17559};
    for (int profile=0;profile<6;profile++) {
        model.era=eras[profile];model.dashboard_build=builds[profile];
        CHECK(model.current_ad_profile()==profile);model.ad_mode[profile]=1;
        model.ad_shuffle_seed[profile]=unsigned(profile+10);
        for (int item=0;item<8;item++) model.ad_selected[profile].push_back(model.ad_catalog[size_t(profile*8+item)].id);
        load_promotions();CHECK(model.promo_cards.size()==8);
        for (int item=0;item<8;item++) CHECK(model.promo_cards[size_t(item)].image==model.ad_catalog[size_t(profile*8+item)].image);
    }
    const auto selections=model.ad_selected;const auto seeds=model.ad_shuffle_seed;const auto modes=model.ad_mode;
    save_ad_profiles();for (auto &selected:model.ad_selected) selected.clear();load_ad_profiles();
    CHECK(model.ad_selected==selections);CHECK(model.ad_mode==modes);CHECK(model.ad_shuffle_seed==seeds);
    model.era=1;model.dashboard_build=9199;const int profile=model.current_ad_profile();
    load_promotions();const auto ordered=images();model.ad_mode[profile]=2;load_promotions();const auto shuffled=images();
    CHECK(shuffled!=ordered);load_promotions();CHECK(images()==shuffled);
    auto sorted=shuffled,expected=ordered;std::sort(sorted.begin(),sorted.end());std::sort(expected.begin(),expected.end());CHECK(sorted==expected);
    model.ad_shuffle_seed[profile]++;load_promotions();CHECK(images()!=shuffled);CHECK(images().front()!=shuffled.front());
    model.ad_mode[profile]=3;load_promotions();CHECK(model.promo_cards.empty());
    save_ad_profiles();load_ad_profiles();CHECK(model.ad_mode[profile]==3);
    model.ad_mode[profile]=2;model.ad_selected[profile].resize(3);model.ad_shuffle_seed[profile]=0xffffffffu;load_promotions();
    const auto before_wrap=images();model.ad_shuffle_seed[profile]=0;load_promotions();CHECK(images().front()!=before_wrap.front());
    model.ad_mode[profile]=0;load_promotions();CHECK(model.promo_cards.size()==3);
    model.ad_mode[profile]=1;model.ad_selected[profile].clear();load_promotions();CHECK(model.promo_cards.empty());
    model.ad_selected[profile].push_back(std::string(64,'a'));save_ad_profiles();load_ad_profiles();
    CHECK(model.ad_selected[profile].size()==1);load_promotions();CHECK(model.promo_cards.empty());
    const auto fragment=std::find_if(model.ad_catalog.begin(),model.ad_catalog.end(),[](const dash::AdEntry &ad){return !ad.auto_eligible;});
    CHECK(fragment!=model.ad_catalog.end());model.ad_selected[profile]={fragment->id};model.ad_mode[profile]=1;load_promotions();
    CHECK(model.promo_cards.size()==1 && model.promo_cards.front().image==fragment->image);
    model.ad_mode[profile]=2;load_promotions();CHECK(model.promo_cards.size()==1 && model.promo_cards.front().image==fragment->image);
    {std::ofstream out(argv[2]);out<<"EXBOX_AD_PROFILES\t1\nmode\t6\t2\nmode\t0\t8\nseed\t0\t4294967296\npick\t0\t"<<std::string(64,'g')<<"\n";}
    load_ad_profiles();CHECK(model.ad_mode[0]==0);CHECK(model.ad_selected[0].empty());CHECK(model.ad_shuffle_seed[0]==0);
    {std::ofstream out(argv[2],std::ios::binary);out<<"EXBOX_AD_PROFILES\t1\r\nmode\t1\t2\r\nseed\t1\t87\r\npick\t1\t"<<model.ad_catalog[0].id<<"\r\n";}
    load_ad_profiles();CHECK(model.ad_mode[1]==2);CHECK(model.ad_shuffle_seed[1]==87);CHECK(model.ad_selected[1]==std::vector<std::string>{model.ad_catalog[0].id});
    for (int era=0;era<6;era++) {
        model.era=eras[era];model.dashboard_build=builds[era];model.ad_mode[era]=2;model.ad_selected[era].clear();
        for (const auto &ad:model.ad_catalog) model.ad_selected[era].push_back(ad.id);
        std::vector<double> times;
        for (int sample=0;sample<25;sample++) {
            const auto start=std::chrono::steady_clock::now();load_promotions();
            times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        }
        CHECK(model.promo_cards.size()==957);std::sort(times.begin(),times.end());
        std::printf("profile %d: 957 selected, rebuild median %.3f ms, p95 %.3f ms (metadata only)\n",era,times[12],times[23]);
    }
    std::printf("%d ad profile checks, %d failures; source images remain unchanged\n",checks,failures);
    return failures?1:0;
}
