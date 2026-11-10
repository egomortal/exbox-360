#pragma once
#include <cmath>
#include <stdexcept>
// Interpolate only within a caller-committed clip. An uncommitted next endpoint
// holds the current sample; it never evaluates another clip or advances a director.
inline std::vector<avkit::M34> committed_pose(const avkit::Renderer&r,const std::string&clip,float current,float next,float alpha,bool same_commit){
 const auto*body=r.body();const avkit::Clip*known=nullptr;
 if(body)for(const auto&c:body->clips)if(c.name==clip){known=&c;break;}
 if(!known||!std::isfinite(current)||!std::isfinite(next)||!std::isfinite(alpha))throw std::runtime_error("unknown/nonfinite committed clip");
 const float last=std::max(0.f,std::nextafter(known->seconds(),0.f));
 current=std::clamp(current,0.f,last);next=std::clamp(next,0.f,last);
 // Wrapping backwards is a discontinuity, not a long blend through the clip.
 float t=current;if(same_commit&&next>=current)t=current+(next-current)*std::clamp(alpha,0.f,1.f);
 return r.pose(clip,t);
}
