#pragma once
#include <cstdint>
extern "C" void av2_diag_thread(uint32_t id,const char* name,int state);
extern "C" void av2_diag_interrupt(uint32_t address,int entering);
extern "C" void av2_diag_arm();
extern "C" void av2_diag_threads_report();
struct Av2ImportObservation {
 uint32_t id;const char* name;const char* previous;uint32_t previous_arg;uint32_t arg;bool trace;
 Av2ImportObservation(uint32_t,const char*,uint32_t,uint32_t);
 ~Av2ImportObservation();
};
