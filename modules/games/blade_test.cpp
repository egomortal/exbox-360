#include "games_blade.hpp"
#include <cassert>
#include <iostream>
using namespace exbox_games;
int main() {
    Blade blade;
    auto library = build_library(Source{}, {{"Alpha", "", ""}, {"Beta", "", ""},
        {"Delta", "", ""}, {"Echo", "", ""}, {"Foxtrot", "", ""}});
    blade.entries = library.entries;
    auto ps5 = installed_entries({{"PPSA12345", "Charlie", ""}});
    blade.entries.insert(blade.entries.end(), ps5.begin(), ps5.end());
    blade.refresh();
    assert(blade.tiles.size() == 6 && blade.controls_focused);
    blade.press(Button::Down);
    auto intent = blade.press(Button::A);
    assert(intent.picker && intent.title_id == player_title && !intent.dispatch_allowed);
    assert(blade.tiles[0].action_label == "Open PS5X360");
    blade.press(Button::Down); assert(blade.selected == 4);
    blade.press(Button::Right); assert(blade.selected == 5);
    blade.press(Button::Right); assert(blade.selected == 5);
    blade.press(Button::Down); assert(blade.selected == 5);
    blade.press(Button::Up); assert(blade.selected == 1);
    blade.press(Button::Up); assert(blade.controls_focused);
    blade.press(Button::Right); blade.press(Button::Right); blade.press(Button::A);
    assert(blade.tiles.size() == 1 && blade.filter == Filter::PS5);
    blade.press(Button::Down); intent = blade.press(Button::A);
    assert(!intent.picker && intent.title_id == "PPSA12345" && !intent.dispatch_allowed);
    blade.press(Button::B); blade.press(Button::Right); blade.press(Button::Right);
    blade.press(Button::A); assert(blade.tiles.empty() && blade.controls_focused);
    blade.press(Button::A); assert(blade.tiles.size() == 1);
    blade.search("CHAR"); assert(blade.tiles.size() == 1);
    blade.search("missing"); assert(blade.tiles.empty());
    blade.press(Button::Down); assert(blade.controls_focused);
    blade.search(""); blade.control = 0; blade.press(Button::A);
    blade.press(Button::Down); blade.press(Button::Right);
    auto key = blade.tiles[blade.selected].entry.key;
    std::reverse(blade.entries.begin(), blade.entries.end()); blade.refresh();
    assert(blade.tiles[blade.selected].entry.key == key);
    blade.entries.clear(); blade.refresh(); assert(blade.controls_focused);
    assert(blade.press(Button::A).title_id.empty());
    std::cout << "blade gate PASS\n";
}
