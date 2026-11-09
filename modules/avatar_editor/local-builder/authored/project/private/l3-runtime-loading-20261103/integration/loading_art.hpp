// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace exbox::transition {
constexpr int width=1920,height=1080;
// Original deterministic loading artwork; no external images or font dependency.
inline uint32_t pixel(int x,int y){
 const unsigned t=unsigned(y)*24/unsigned(height-1);uint32_t rgb=((10+t/3)<<16)|((20+t/2)<<8)|(30+t);
 const int dx=x-960,dy=y-500;
 if(dx>=-100&&dx<100&&dy>=-100&&dy<100){const bool gap=(dx>=-6&&dx<6)||(dy>=-6&&dy<6);if(!gap)rgb=dx<0?(dy<0?0x74d66b:0x53b985):(dy<0?0x58bca1:0x89db7b);}
 if(y>=675&&y<685&&x>=840&&x<1080)rgb=0x43655c;
 for(int i=0;i<3;++i)if(x>=925+i*30&&x<935+i*30&&y>=725&&y<735)rgb=0xc1d6c9;
 return 0xff000000u|rgb;
}
enum class Decision {none,launch,fail};
class Gate {
 enum class Stage {idle,fading,waiting,requested,held};Stage stage_=Stage::idle;uint32_t start_=0;uint64_t target_=0;
public:
 bool begin(uint32_t now){if(stage_!=Stage::idle)return false;start_=now;target_=0;stage_=Stage::fading;return true;}
 unsigned alpha(uint32_t now)const{if(stage_==Stage::held||stage_==Stage::requested)return 255;const uint32_t elapsed=now-start_;return elapsed>=650?255:elapsed*255/650;}
 bool active()const{return stage_!=Stage::idle;}
 bool held()const{return stage_==Stage::held;}
 void submitted(uint64_t serial,uint32_t now){if(stage_==Stage::fading&&serial&&alpha(now)==255){target_=serial;stage_=Stage::waiting;}}
 Decision poll(uint32_t now,uint64_t completed,bool error){
  if(stage_!=Stage::fading&&stage_!=Stage::waiting)return Decision::none;
  if(error||uint32_t(now-start_)>=5000){stage_=Stage::idle;return Decision::fail;}
  if(stage_==Stage::waiting&&completed>=target_){stage_=Stage::requested;return Decision::launch;}return Decision::none;
 }
 void result(bool ok){if(stage_==Stage::requested)stage_=ok?Stage::held:Stage::idle;}
 void returned(){stage_=Stage::idle;target_=0;}
};
}