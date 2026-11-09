#include "games_library.hpp"
#include <cassert>
#include <iostream>
using namespace exbox_games;
int main() {
    Source source;
    source.availability = Availability::Available;
    auto library = build_library(source, {{"Game Two", "", "owner:two"},
        {"Game One", "", ""}, {"Game One", "ignored duplicate", ""},
        {"..", "", ""}, {"bad/name", "", ""}, {"bad\\name", "", ""},
        {std::string("bad\0name", 8), "", ""}, {"", "", ""}});
    assert(library.rejected == 5 && library.entries.size() == 2);
    assert(library.entries[0].title == "Game One");
    assert(library.entries[1].owner_art_key == "owner:two");
    assert(library.entries[0].key != library.entries[1].key);
    assert(identity("a:b", "c") != identity("a", "b:c"));
    assert(identity("source1", "Same") != identity("source2", "Same"));
    auto unicode = build_library(source, {{"Jeu été", "", ""}, {"game", "", ""}, {"Game", "", ""}});
    assert(unicode.entries.size() == 3);
    source.availability = Availability::Unavailable;
    assert(build_library(source, {{"Game", "", ""}}).entries[0].availability == Availability::Unavailable);
    assert(build_library(Source{}, {}).entries.empty());
    source.id = "../bad";
    bool rejected = false;
    try { build_library(source, {}); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    auto ps5 = installed_entries({{"PPSA12345", "Host title", "owner:host"},
        {"PPSA12345", "Duplicate", ""}, {"PPSA50011", "Player", ""},
        {"../../bad", "Bad", ""}, {"CUSA12345", "PS4", ""}});
    assert(ps5.size() == 1 && ps5[0].platform == Platform::PS5);
    assert(ps5[0].key == "title:PPSA12345" && ps5[0].launch_title == "PPSA12345");
    assert(library.entries[0].launch_title == player_title);
    std::cout << "library gate PASS\n";
}
