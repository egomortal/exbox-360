#include "../modules/dashboard_services/compat_health.hpp"
#include <cstdio>
int main(){int n=0,f=0;
#define C(x) do{++n;if(!(x)){++f;std::printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
 using namespace exbox_services::health;Snapshot s;auto r=rows(s);C(r.size()==9);C(r[0].value.find("Not installed")!=std::string::npos);C(r[3].value.find("Not published")!=std::string::npos);C(r[5].description.find("Compatibility/default fallback")!=std::string::npos);C(r[8].value=="Not published");
 s.ps5x360_installed=true;s.companion_advertised=true;s.xbox_inventory_scanned=true;s.xbox_titles=7;s.continue_playing=3;s.artwork_mapped=4;s.artwork_unmapped=3;s.soundtrack_playable=20;s.soundtrack_albums=2;s.themes_complete=5;s.themes_partial=1;s.themes_rejected=0;s.native_presenter_requested=true;s.native_presenter_active=false;s.gpu_dashboard_requested=true;s.gpu_dashboard_available=false;s.gpu_fallback=true;s.diagnostics_available=true;s.missing_assets=2;r=rows(s);
 C(r[0].value.find("Installed")!=std::string::npos);C(r[1].value.find("7 supplied")!=std::string::npos);C(r[2].value.find("3 saved")!=std::string::npos);C(r[4].value=="4 mapped; 3 need mapping");C(r[6].value.find("fell back")!=std::string::npos);C(r[7].value=="Available");C(r[8].value=="2");
 std::printf("%d checks, %d failed\n",n,f);return f?1:0;
}
