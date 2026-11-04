// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/key_bindings.hpp"
#include <cassert>
using namespace exbox::editor;
int main() {
    auto enter=[](std::string_view s){return s=="Enter";};
    for(uint8_t mods=0;mods<8;++mods) {
        assert(binding_matches("Enter",mods,enter)==(mods==0));
        assert(binding_matches("Shift+Enter",mods,enter)==(mods==1));
        assert(binding_matches("CTRL + Alt + Enter",mods,enter)==(mods==6));
        assert(binding_matches("alt+Shift+Ctrl+Enter",mods,enter)==(mods==7));
    }
    assert(binding_matches("Invalid+Enter,  Enter  ",0,enter));
    assert(binding_matches(",,Shift+Enter,Enter,",0,enter));
    assert(binding_matches("Shift+Shift+Enter",1,enter));
    for(auto bad:{"", "  ", "Shift+", "+Enter", "Meta+Enter", "Enter+Shift", "Escape"})
        assert(!binding_matches(bad,0,enter));
    bool called=false;
    assert(!binding_matches("Unknown+Enter",0,[&](auto){called=true;return true;}));
    assert(!called);
}
