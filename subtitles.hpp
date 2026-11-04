// Ex-Box 360: SubRip (.srt) captions for Game Videos. Pure, header-only.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace srt
{
struct Cue
{
    int from = 0, to = 0; // ms
    std::vector<std::string> lines;
};

// "00:01:02,345" (or with '.'), into ms; -1 when it is not a time.
inline int parse_time(const std::string &s, size_t &at)
{
    int part[4] = {}, n = 0, digits = 0;
    while (at < s.size() && s[at] == ' ') ++at;
    for (; at < s.size() && n < 4; ++at)
    {
        const char c = s[at];
        if (c >= '0' && c <= '9') {part[n] = part[n] * 10 + (c - '0'); ++digits;}
        else if ((c == ':' && n < 2) || ((c == ',' || c == '.') && n == 2)) {if (!digits) return -1; ++n; digits = 0;}
        else break;
    }
    if (n != 3 || !digits || part[1] > 59 || part[2] > 59) return -1;
    return ((part[0] * 60 + part[1]) * 60 + part[2]) * 1000 + std::min(part[3], 999);
}

// Tags such as <i> and {\an8} are dropped: the player has one style.
inline std::string plain(const std::string &line)
{
    std::string out;
    char closing = 0;
    for (char c : line)
    {
        if (closing) {if (c == closing) closing = 0; continue;}
        if (c == '<') {closing = '>'; continue;}
        if (c == '{' ) {closing = '}'; continue;}
        out += c;
    }
    return out;
}

// Cues sorted by start. Malformed blocks are skipped, not fatal: sidecars
// come from anywhere.
inline std::vector<Cue> parse(const std::string &text)
{
    std::vector<Cue> cues;
    size_t at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0; // UTF-8 BOM
    Cue cue;
    bool in_cue = false;
    auto finish = [&] {
        if (in_cue && !cue.lines.empty() && cue.to > cue.from) cues.push_back(cue);
        cue = Cue{};
        in_cue = false;
    };
    while (at <= text.size())
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        at = end + 1;
        const size_t arrow = line.find("-->");
        if (arrow != std::string::npos)
        {
            finish();
            size_t p = 0;
            const int from = parse_time(line.substr(0, arrow), p);
            p = 0;
            const int to = parse_time(line.substr(arrow + 3), p);
            if (from >= 0 && to >= 0) {cue.from = from; cue.to = to; in_cue = true;}
            continue;
        }
        if (line.find_first_not_of(" \t") == std::string::npos) {finish(); continue;}
        // A cue's number with no blank line before it belongs to the next cue.
        const size_t next = text.find('\n', at);
        if (line.find_first_not_of("0123456789") == std::string::npos &&
            text.substr(at, next == std::string::npos ? std::string::npos : next - at).find("-->") != std::string::npos)
            continue;
        if (in_cue && cue.lines.size() < 3) // more than three lines would cover the picture
        {
            const std::string shown = plain(line);
            if (!shown.empty()) cue.lines.push_back(shown);
        }
    }
    finish();
    std::stable_sort(cues.begin(), cues.end(), [](const Cue &a, const Cue &b) { return a.from < b.from; });
    return cues;
}

// The cue on screen at ms, or null.
inline const Cue *at(const std::vector<Cue> &cues, int ms)
{
    auto it = std::upper_bound(cues.begin(), cues.end(), ms, [](int t, const Cue &c) { return t < c.from; });
    // Overlapping cues are few; look back a little, not through the whole file.
    for (int back = 0; back < 8 && it != cues.begin(); ++back)
        if (ms < (--it)->to) return &*it;
    return nullptr;
}
} // namespace srt
