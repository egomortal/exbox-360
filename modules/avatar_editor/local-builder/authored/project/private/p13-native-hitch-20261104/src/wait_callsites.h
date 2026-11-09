#pragma once
#include <cstdint>
// Exact selected generated direct calls only. Unknown, indirect or nonzero LR high bits return 0.
inline constexpr uint32_t av2_verified_wait_pc(uint64_t lr) noexcept {
 switch(lr) {
 case 0x92134d7cULL: return 0x92134d78u;
 case 0x92134e24ULL: return 0x92134e20u;
 case 0x922207b4ULL: return 0x922207b0u;
 case 0x92223540ULL: return 0x9222353cu;
 case 0x92224934ULL: return 0x92224930u;
 case 0x92226374ULL: return 0x92226370u;
 case 0x9222a200ULL: return 0x9222a1fcu;
 case 0x9226a628ULL: return 0x9226a624u;
 case 0x9226a6e0ULL: return 0x9226a6dcu;
 case 0x9226a774ULL: return 0x9226a770u;
 default: return 0;
 }
}
