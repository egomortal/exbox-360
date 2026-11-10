// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
namespace frame_pacing {
class Pacer { double deadline_=0, period_=0;
public:
 double wait_ms(double started,double finished,bool dimmed){
  const double period=dimmed?100.0:1000.0/60.0;
  if(period!=period_||deadline_==0||started>deadline_+period){period_=period;deadline_=started+period;}
  if(finished>deadline_){deadline_=finished+period;return 0;}
  const double wait=std::max(0.0,deadline_-finished);deadline_+=period;return wait;
 }
};
}
