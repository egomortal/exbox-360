// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>
#include <cstdint>
namespace exbox::editor {
// Exact modifier matching prevents Shift+key from firing a bare-key binding.
// Key names remain the SDK's responsibility; the parser owns only delimiters.
inline std::string_view bind_trim(std::string_view s) {
    const auto first=s.find_first_not_of(' ');
    return first==s.npos ? std::string_view{} : s.substr(first,s.find_last_not_of(' ')-first+1);
}
inline bool modifier_name(std::string_view s, std::string_view lower) {
    if(s.size()!=lower.size())return false;
    for(size_t i=0;i<s.size();++i) {
        unsigned char c=static_cast<unsigned char>(s[i]);
        if(c>='A' && c<='Z') c=static_cast<unsigned char>(c+('a'-'A'));
        if(c!=static_cast<unsigned char>(lower[i]))return false;
    }
    return true;
}
template<class KeyMatches>
bool binding_matches(std::string_view spec,uint8_t modifiers,KeyMatches key_matches) {
    // Alternatives are independent: an invalid chord does not poison later ones.
    while(!spec.empty()) {
        const auto comma=spec.find(',');
        auto chord=spec.substr(0,comma);
        spec=comma==spec.npos ? std::string_view{} : spec.substr(comma+1);
        uint8_t required=0; bool valid=true;
        for(auto plus=chord.find('+');plus!=chord.npos;plus=chord.find('+')) {
            const auto part=bind_trim(chord.substr(0,plus));
            if(modifier_name(part,"shift"))required|=1;
            else if(modifier_name(part,"ctrl"))required|=2;
            else if(modifier_name(part,"alt"))required|=4;
            else valid=false;
            chord.remove_prefix(plus+1);
        }
        chord=bind_trim(chord);
        if(valid && !chord.empty() && required==modifiers && key_matches(chord))return true;
    }
    return false;
}
} // namespace exbox::editor
