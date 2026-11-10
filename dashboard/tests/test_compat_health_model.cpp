// SPDX-License-Identifier: GPL-3.0-or-later
#include "../dash_model.hpp"
#include <cstdio>

static int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);}}while(0)

int main(){
    dash::Model model;
    model.viewer=dash::Viewer::SystemSettings;
    model.system_page=dash::Model::SystemRoot;
    auto root=model.system_entries();
    int health=-1;
    for(int i=0;i<int(root.size());++i)if(root[std::size_t(i)].label=="Compatibility & Health")health=i;
    CHECK(health>=0);
    model.viewer_item=health;
    model.press(xb::A);
    CHECK(model.system_page==dash::Model::SystemHealth);
    CHECK(model.system_page_title()=="Compatibility & Health");
    CHECK(model.system_entries().size()==9);
    auto hints=model.hints();
    CHECK(hints.size()==1&&hints[0].button==xb::B&&hints[0].label=="Back");
    model.press(xb::Down);
    CHECK(model.viewer_item==1);
    model.press(xb::A);
    CHECK(model.system_page==dash::Model::SystemHealth&&!model.details);
    auto rows=model.compat_health_rows();
    CHECK(rows[3].value.find("Not published")!=std::string::npos);
    CHECK(rows[8].value=="Not published");
    model.press(xb::B);
    CHECK(model.system_page==dash::Model::SystemRoot&&model.viewer_item==health);

    model.health_runtime.soundtrack_playable=12;
    model.health_runtime.soundtrack_albums=2;
    model.health_runtime.themes_complete=3;
    model.health_runtime.themes_partial=1;
    model.health_runtime.gpu_dashboard_requested=true;
    model.health_runtime.gpu_dashboard_available=false;
    model.health_runtime.gpu_fallback=true;
    model.developer_diagnostics_available=true;
    rows=model.compat_health_rows();
    CHECK(rows[3].value=="12 playable; 2 game albums");
    CHECK(rows[5].value=="3 complete; 1 partial");
    CHECK(rows[6].value.find("fallback")!=std::string::npos);
    CHECK(rows[7].value=="Available");
    std::printf("%d checks, %d failed\n",checks,failures);
    return failures?1:0;
}
