// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
namespace frame_metrics {
class Window {
 std::array<double,600> samples_{}; size_t used_=0;
public:
 bool add(double ms) { if(std::isfinite(ms)&&ms>=0&&used_<samples_.size()) samples_[used_++]=ms; return used_==samples_.size(); }
 bool report(char* out,size_t capacity,int density) {
  if(!used_) return false;
  std::sort(samples_.begin(),samples_.begin()+used_);
  size_t over=0; for(size_t i=0;i<used_;++i) if(samples_[i]>1000.0/60.0) ++over;
  auto percentile=[&](double q){return samples_[size_t(std::ceil(q*double(used_)))-1];};
  std::snprintf(out,capacity,"render work %dx n=%zu p50=%.3f p95=%.3f p99=%.3f max=%.3f over60=%zu",density,used_,percentile(.50),percentile(.95),percentile(.99),samples_[used_-1],over);
  used_=0; return true;
 }
};
}
