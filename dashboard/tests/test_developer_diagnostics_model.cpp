// SPDX-License-Identifier: GPL-3.0-or-later
#include "../dash_model.hpp"
#include <cstdio>

static int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);}}while(0)

int main(){
    dash::Model model;model.viewer=dash::Viewer::SystemSettings;model.system_page=dash::Model::SystemRoot;model.viewer_item=0;
    auto rows=model.system_entries();CHECK(!rows.empty()&&rows.back().label=="Compatibility & Health");
    model.developer_diagnostics_available=true;rows=model.system_entries();CHECK(rows.back().label=="Developer Diagnostics");
    model.viewer_item=int(rows.size())-1;model.press(xb::A);CHECK(model.viewer==dash::Viewer::SystemSettings&&model.system_page==dash::Model::SystemDiagnostics);CHECK(model.system_entries().empty()&&model.system_page_title()=="Developer Diagnostics");
    model.press(xb::B);CHECK(model.system_page==dash::Model::SystemRoot&&model.viewer_item==int(rows.size())-1);
    std::printf("%d checks, %d failed\n",checks,failures);return failures?1:0;
}
