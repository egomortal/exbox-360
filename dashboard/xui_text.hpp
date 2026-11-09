#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace xui {
// Recovered from the owner's 1888 XAM font renderer, not the third-party enum.
enum TextFlags {TextShadow=1,TextWordBreak=0x10,TextSingleLine=0x20,
    TextRight=0x200,TextCenter=0x400,TextVCenter=0x1000,TextEllipsis=0x4000};
inline std::vector<std::string> utf8_characters(const std::string &text) {
    std::vector<std::string> result;
    for (size_t i=0;i<text.size();) {
        const auto c=static_cast<unsigned char>(text[i]);size_t n=c<128?1:c<224?2:c<240?3:4;
        n=std::min(n,text.size()-i);result.push_back(text.substr(i,n));i+=n;
    }
    return result;
}
struct TextLine {std::string text;float width=0;};
template<class Measure> inline std::vector<TextLine> layout_text(const std::string &input,float width,int flags,Measure measure) {
    std::vector<TextLine> lines;std::string line;size_t last_space=std::string::npos;
    auto emit=[&] {lines.push_back({line,measure(line)});line.clear();last_space=std::string::npos;};
    for (const auto &c:utf8_characters(input)) {
        if (c=="\r") continue;
        if (c=="\n") {if (flags&TextSingleLine) break;emit();continue;}
        const std::string candidate=line+c;
        if (!(flags&TextSingleLine) && (flags&TextWordBreak) && !line.empty() && measure(candidate)>width) {
            if (last_space!=std::string::npos) {
                const auto tail=line.substr(last_space+1);line.resize(last_space);emit();line=tail;
            } else emit();
        }
        if (c==" " || c=="\t") last_space=line.size();
        line+=c;
    }
    if (!line.empty() || lines.empty()) emit();
    if (flags&TextEllipsis) for (auto &item:lines) if (item.width>width) {
        auto chars=utf8_characters(item.text);const std::string ellipsis="\xe2\x80\xa6";
        while (!chars.empty()) {
            chars.pop_back();item.text.clear();for (auto &c:chars) item.text+=c;
            if (measure(item.text+ellipsis)<=width) break;
        }
        item.text=measure(ellipsis)<=width?item.text+ellipsis:"";item.width=measure(item.text);
    }
    return lines;
}
}
