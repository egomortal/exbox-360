#pragma once
#include <cstdint>
namespace av2::back_prompt {
enum class Action { None, Save, Discard, Cancel };
struct Event { bool consumed=false; Action action=Action::None; };
class State {
 public:
  uint64_t snapshot()const{return active_?(uint64_t(generation_)<<32)|choice_:0;}
  Event key(uint16_t key,uint16_t flags,uint32_t editor_state,uint64_t shown) {
    const bool down=flags&1,up=flags&2,repeat=flags&4;
    const uint64_t bit=key>=0x5800&&key<0x5840?uint64_t(1)<<(key-0x5800):0;
    const bool swallowed=swallowed_&bit;
    if(up)swallowed_&=~bit;
    if(up&&(delivered_&bit)){delivered_&=~bit;return {};}
    if(!active_) {
      if(swallowed)return {true};
      if(key!=0x5801||!down||up||repeat||editor_state!=2){if(down)delivered_|=bit;return {};}
      // Require a fresh B once earlier action presses have settled. Direction
      // releases may pair safely; never forward a prior A/B release under a modal.
      if(delivered_&~(uint64_t(15)<<16)){swallowed_|=bit;return {true};}
      swallowed_|=bit;active_=true;released_=false;choice_=2;bump();return {true};
    }
    if(down)swallowed_|=bit;
    if(editor_state!=2)return close(Action::Cancel);
    if(key==0x5801&&up)released_=true;
    if(!down||up||repeat||swallowed||!released_)return {true};
    if(key==0x5801)return close(Action::Cancel);
    if(key==0x5810||key==0x5812) {if(choice_){--choice_;bump();}return {true};}
    if(key==0x5811||key==0x5813) {if(choice_<2){++choice_;bump();}return {true};}
    if(key==0x5800&&shown==snapshot())return close(choice_==0?Action::Save:choice_==1?Action::Discard:Action::Cancel);
    return {true};
  }
 private:
  void bump(){if(++generation_==0)++generation_;}
  Event close(Action a){active_=false;return {true,a};}
  bool active_=false,released_=false;uint32_t choice_=2,generation_=0;uint64_t swallowed_=0,delivered_=0;
};
}
