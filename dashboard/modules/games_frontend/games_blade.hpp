// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "games_library.hpp"
#include <set>

namespace exbox_games {
enum class Filter { All, Xbox360, PS5 };
enum class Button { Left, Right, Up, Down, A, B };
struct Tile {
    Entry entry;
    std::string subtitle, action_label;
};
struct Intent {
    std::string library_key, title_id;
    bool picker = false;
    bool dispatch_allowed = false;
    std::string reason;
};
inline std::string ascii_fold(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    return text;
}
class Blade {
public:
    // Four columns mirrors dash::kViewerColumns; renderer supplies actual geometry.
    static constexpr std::size_t columns = 4;
    inline static constexpr const char* controls[] = {
        "All", "Xbox 360", "PS5", "Show/hide Xbox 360", "Show/hide PS5"};
    std::vector<Entry> entries;
    std::vector<Tile> tiles;
    Filter filter = Filter::All;
    bool show_xbox = true, show_ps5 = true;
    bool controls_focused = true;
    std::size_t control = 0, selected = 0;
    std::string query;

    void refresh() {
        std::string key = selected < tiles.size() ? tiles[selected].entry.key : "";
        tiles.clear();
        std::set<std::string> seen;
        const auto needle = ascii_fold(query);
        for (const auto& entry : entries) {
            const bool xbox = entry.platform == Platform::Xbox360;
            if ((xbox && (!show_xbox || filter == Filter::PS5)) ||
                (!xbox && (!show_ps5 || filter == Filter::Xbox360))) continue;
            if (ascii_fold(entry.title).find(needle) == std::string::npos) continue;
            if (!seen.insert(entry.key).second) continue;
            tiles.push_back({entry, xbox ? "Xbox 360 • Select game in PS5X360" : "PS5",
                xbox ? "Open PS5X360" : "Play"});
        }
        std::sort(tiles.begin(), tiles.end(), [](const Tile& a, const Tile& b) {
            auto x = ascii_fold(a.entry.title), y = ascii_fold(b.entry.title);
            return x == y ? a.entry.key < b.entry.key : x < y;
        });
        selected = 0;
        for (std::size_t i = 0; i < tiles.size(); ++i)
            if (tiles[i].entry.key == key) selected = i;
        if (tiles.empty()) controls_focused = true;
    }
    void search(std::string text) { query = std::move(text); refresh(); }
    Intent press(Button button) {
        if (control >= 5) control = 0;
        if (selected >= tiles.size()) selected = 0;
        if (tiles.empty()) controls_focused = true;
        if (button == Button::B) { controls_focused = true; return {}; }
        if (controls_focused) {
            if (button == Button::Left && control > 0) --control;
            if (button == Button::Right && control < 4) ++control;
            if (button == Button::Down && !tiles.empty()) controls_focused = false;
            if (button == Button::A) {
                if (control < 3) filter = static_cast<Filter>(control);
                else if (control == 3) show_xbox = !show_xbox;
                else show_ps5 = !show_ps5;
                refresh();
            }
            return {};
        }
        if (button == Button::Left && selected % columns != 0) --selected;
        if (button == Button::Right && selected % columns != columns-1 && selected+1 < tiles.size()) ++selected;
        if (button == Button::Up) {
            if (selected < columns) controls_focused = true;
            else selected -= columns;
        }
        if (button == Button::Down && selected+columns < tiles.size()) selected += columns;
        if (button == Button::A) {
            const auto& entry = tiles[selected].entry;
            return {entry.key, entry.launch_title, entry.platform == Platform::Xbox360, false,
                entry.availability == Availability::Unavailable ? "Source unavailable" :
                "Launch pending companion v4b console verification"};
        }
        return {};
    }
};
} // namespace exbox_games

