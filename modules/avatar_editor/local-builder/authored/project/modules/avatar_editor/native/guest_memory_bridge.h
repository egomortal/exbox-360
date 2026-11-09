#pragma once
#include <cstddef>
#include <cstdint>
extern "C" {
void* av2_guest_reserve();
bool av2_guest_release();
bool av2_guest_contains(const void*,size_t);
bool av2_guest_commit(void*,size_t,unsigned permissions);
bool av2_guest_protect(void*,size_t,unsigned permissions);
bool av2_guest_decommit(void*,size_t);
unsigned av2_guest_permissions(const void*);
}
