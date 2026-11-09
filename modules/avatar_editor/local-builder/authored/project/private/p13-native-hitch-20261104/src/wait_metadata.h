#pragma once
#include <cstdint>
struct Av2HitchWait {
 uint64_t lr=0,timeout=0,os_tid=0;
 uint32_t pc=0,entry=0,startup=0,result=0,reason=0,mode=0,alertable=0;
 bool valid=false,thread_known=false,guest=false,main=false,timed=false;
};
