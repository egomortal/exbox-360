#pragma once
#include "../avatar_kit.hpp"
#include <stdexcept>
static uint32_t x21_avatar_bits(float v){uint32_t q;memcpy(&q,&v,4);return q;}
namespace avkit { struct ExboxGpuPacket { static std::vector<uint32_t> make(avkit::Renderer&r,const std::vector<avkit::M34>&pose,avkit::Camera cam,int width,int height,bool diagnostics=false){
 (void)width;(void)diagnostics;
 if(r.baked_lighting_||r.head_scale_!=1)throw std::runtime_error("unsupported baked lighting/head scale");
 std::vector<uint32_t> s(32);s[4]=uint32_t(s.size());s[5]=uint32_t(r.draws_.size());s.resize(s.size()+r.draws_.size()*16);
 const float th=std::tan(.42f/2);s[8]=x21_avatar_bits(std::cos(cam.yaw));s[9]=x21_avatar_bits(std::sin(cam.yaw));s[10]=x21_avatar_bits(cam.span/2/th);s[11]=x21_avatar_bits(float(height)/th);s[12]=x21_avatar_bits(cam.center_y);
 std::vector<uint32_t> vertices,triangles;
 for(size_t di=0;di<r.draws_.size();++di){const auto&d=r.draws_[di];const auto&pr=*d.prim;uint32_t base=uint32_t(vertices.size()/12);size_t meta=s[4]+di*16;
  if(d.cutting||d.pattern||!d.mip||d.mip->level.empty()||(d.alpha!=0&&d.alpha!=4))throw std::runtime_error("unsupported selected material: "+d.part->name);
  size_t level=0;float want=float(height)/cam.span*1.6f;while(level+1<d.mip->level.size()&&float(d.mip->w[level+1])>=want)++level;
  s[meta]=uint32_t(s.size());s[meta+1]=d.mip->w[level];s[meta+2]=d.mip->h[level];s[meta+3]=d.alpha;s[meta+4]=d.two_sided;s[meta+5]=x21_avatar_bits(d.inflate);s[meta+6]=x21_avatar_bits(r.body_scale_);s.insert(s.end(),d.mip->level[level].begin(),d.mip->level[level].end());
  uint32_t palette=uint32_t(s.size());for(size_t j=0;j<pose.size();j++){auto m=avkit::mul(pose[j],d.part->ibm[j]);for(float f:m.m)s.push_back(x21_avatar_bits(f));}
  for(const auto&v:pr.v){for(float f:v.p)vertices.push_back(x21_avatar_bits(f));for(float f:v.n)vertices.push_back(x21_avatar_bits(f));for(float f:v.uv)vertices.push_back(x21_avatar_bits(f));uint32_t joints,weights;memcpy(&joints,v.j,4);memcpy(&weights,v.w,4);vertices.insert(vertices.end(),{joints,weights,palette,uint32_t(di)});}
  for(size_t t=0;t<pr.idx.size()/3;t++){
   if(d.tri_hide&&t<d.tri_hide->size()&&(*d.tri_hide)[t])continue;
   if(!pr.region.empty()){uint32_t bit=1u<<pr.region[t];if((d.only&&!(d.only&bit))||(!d.only&&(d.hide&bit)))continue;}else if(d.only)continue;
   if(d.hair_above<1e8f&&(pr.v[pr.idx[t*3]].p[1]+pr.v[pr.idx[t*3+1]].p[1]+pr.v[pr.idx[t*3+2]].p[1])/3>d.hair_above)continue;
   triangles.insert(triangles.end(),{base+pr.idx[t*3],base+pr.idx[t*3+1],base+pr.idx[t*3+2],uint32_t(di)});
  }
 }
 s[0]=uint32_t(vertices.size()/12);s[1]=uint32_t(triangles.size()/4);s[2]=uint32_t(s.size());s.insert(s.end(),vertices.begin(),vertices.end());s[3]=uint32_t(s.size());s.insert(s.end(),triangles.begin(),triangles.end());return s;
}

}; }
inline std::vector<uint32_t> exbox_avatar_packet(avkit::Renderer&r,const std::vector<avkit::M34>&p,avkit::Camera c,int w,int h,bool d=false){return avkit::ExboxGpuPacket::make(r,p,c,w,h,d);}
