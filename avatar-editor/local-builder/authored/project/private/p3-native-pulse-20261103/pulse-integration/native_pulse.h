#pragma once
#include <cstdint>
namespace rex {class Runtime;}
extern "C" bool av2_native_pulse_start(rex::Runtime*);
extern "C" bool av2_native_pulse_stop();
extern "C" void av2_native_pulse_register(uint32_t,uint32_t);
extern "C" void av2_native_pulse_scope(bool);
