#pragma once
#include <string>
#include <vector>

namespace dash {
struct DashboardFamily { const char *name; int era, preferred; };
inline constexpr DashboardFamily kDashboardFamilies[] = {
    {"Blades",0,6770},{"NXE",1,9199},{"Kinect",2,13604},
    {"Metro V1",3,15574},{"Metro V2",3,17559}
};
struct DashboardUpdate { int family, build; bool preview=false; };
inline const std::vector<DashboardUpdate> &dashboard_updates() {
    static const std::vector<DashboardUpdate> updates={
        {0,1888,false},
        {0,2241,false},
        {0,2255,false},
        {0,2258,false},
        {0,2858,false},
        {0,2868,false},
        {0,4532,false},
        {0,4548,false},
        {0,4552,false},
        {0,5759,false},
        {0,5766,false},
        {0,5787,false},
        {0,6683,false},
        {0,6690,false},
        {0,6717,false},
        {0,6770,false},
        {1,7258,true},
        {1,7356,true},
        {1,7357,false},
        {1,7363,false},
        {1,7371,false},
        {1,8495,false},
        {1,8496,true},
        {1,8498,false},
        {1,8507,false},
        {1,8955,true},
        {1,9199,false},
        {2,11025,true},
        {2,11027,true},
        {2,11658,true},
        {2,12209,true},
        {2,12275,true},
        {2,12293,true},
        {2,12365,true},
        {2,12387,true},
        {2,12413,true},
        {2,12416,true},
        {2,12565,true},
        {2,12567,true},
        {2,12581,true},
        {2,12583,true},
        {2,12595,true},
        {2,12603,true},
        {2,12606,true},
        {2,12607,true},
        {2,12609,false},
        {2,12611,false},
        {2,12625,false},
        {2,13140,true},
        {2,13141,true},
        {2,13142,true},
        {2,13145,true},
        {2,13146,false},
        {2,13599,false},
        {2,13604,false},
        {3,14074,true},
        {3,14385,true},
        {3,14448,true},
        {3,14474,true},
        {3,14686,true},
        {3,14692,true},
        {3,14696,true},
        {3,14699,false},
        {3,14717,false},
        {3,14719,false},
        {3,15574,false},
        {4,16091,true},
        {4,16113,true},
        {4,16133,true},
        {4,16145,true},
        {4,16154,true},
        {4,16167,true},
        {4,16178,true},
        {4,16189,true},
        {4,16191,true},
        {4,16197,false},
        {4,16202,false},
        {4,16203,false},
        {4,16517,true},
        {4,16525,true},
        {4,16529,true},
        {4,16531,true},
        {4,16535,true},
        {4,16536,true},
        {4,16537,false},
        {4,16547,false},
        {4,16747,false},
        {4,16756,false},
        {4,16767,false},
        {4,17148,false},
        {4,17150,false},
        {4,17349,false},
        {4,17489,false},
        {4,17502,false},
        {4,17526,false},
        {4,17544,false},
        {4,17559,false},
    };
    return updates;
}
inline std::vector<DashboardUpdate> updates_for(int family,bool previews=false) {
    std::vector<DashboardUpdate> out;
    for (auto update:dashboard_updates()) if (update.family==family && (previews || !update.preview)) out.push_back(update);
    return out;
}
inline std::string dashboard_version(int build) {return "2.0."+std::to_string(build)+".0";}
inline int dashboard_family(int era,int build) {
    return era<3?era:build>=14000 && build<16091?3:4;
}
}
