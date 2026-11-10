// SPDX-License-Identifier: GPL-3.0-or-later
#include "../dash_model.hpp"
#include <cstdio>
#include <string>

static int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);}}while(0)

int main(){
    dash::Model model;model.era=1;model.dashboard_build=9199;model.open_guide();model.press(xb::RS);model.guide_blade=dash::BladeSettings;
    const auto rows=model.blade_entries(dash::BladeSettings);int row=-1;
    for(std::size_t i=0;i<rows.size();++i)if(rows[i].action==dash::GuideAction::CyclePromotionMode)row=int(i);
    CHECK(row>=0&&rows[std::size_t(row)].label=="Promotion Rotation: Authentic");
    model.guide_item[dash::BladeSettings]=row;auto effect=model.press(xb::A);
    CHECK(effect.command==dash::Command::ReloadPromotions&&model.promotion_mode==1&&model.promotions);
    CHECK(model.blade_entries(dash::BladeSettings)[std::size_t(row)].label=="Promotion Rotation: Local Personalized");
    effect=model.press(xb::A);CHECK(effect.command==dash::Command::ReloadPromotions&&model.promotion_mode==2&&!model.promotions);
    effect=model.press(xb::A);CHECK(effect.command==dash::Command::ReloadPromotions&&model.promotion_mode==0&&model.promotions);
    CHECK(model.saved_promotion_mode_setting()=="promotion_mode=0");
    CHECK(model.load_promotion_mode_setting("promotion_mode=2\r\n")&&model.promotion_mode==2&&!model.promotions);
    CHECK(!model.load_promotion_mode_setting("promotion_mode=3\n")&&model.promotion_mode==2);
    CHECK(!model.load_promotion_mode_setting("promotion_mode=1\nextra")&&model.promotion_mode==2);
    model.press(xb::Down);model.press(xb::Up);CHECK(model.guide_item[dash::BladeSettings]==row);
    std::printf("%d checks, %d failed\n",checks,failures);return failures?1:0;
}
