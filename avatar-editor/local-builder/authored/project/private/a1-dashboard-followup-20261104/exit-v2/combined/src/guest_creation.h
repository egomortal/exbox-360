#pragma once
namespace rex::system {class XThread;}
bool av2_native_begin_guest_creation(rex::system::XThread*);
void av2_native_finish_guest_creation(rex::system::XThread*,bool);
void av2_native_guest_start_gate(rex::system::XThread*);
void av2_native_close_guest_admission();
class Av2GuestCreationScope {
 rex::system::XThread* thread_;bool admitted_,finished_=false;
 public:
 explicit Av2GuestCreationScope(rex::system::XThread*t):thread_(t),admitted_(av2_native_begin_guest_creation(t)){}
 ~Av2GuestCreationScope(){if(admitted_&&!finished_)av2_native_finish_guest_creation(thread_,false);}
 bool admitted()const{return admitted_;}
 void ready(){av2_native_finish_guest_creation(thread_,true);finished_=true;}
};

#include <chrono>
namespace rex::thread {enum class WaitResult;}
rex::thread::WaitResult av2_native_poll_guest_completion(rex::system::XThread*,std::chrono::milliseconds);
