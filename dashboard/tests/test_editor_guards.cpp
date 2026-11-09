// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/guards.hpp"
#include <cassert>
#include <cstring>
static unsigned logs=0;
static void record(const char* text) { assert(std::strstr(text,"defensive null"));++logs; }
int main() {
    using exbox::editor::null_operation_guard;
    assert(!null_operation_guard(0x945065f0,record));
    assert(!null_operation_guard(0x40029970,record));
    assert(logs==0);
    assert(null_operation_guard(0,record));assert(logs==1);
    assert(null_operation_guard(0,nullptr));assert(logs==1);
}
