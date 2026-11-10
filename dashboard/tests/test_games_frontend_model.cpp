#include "../dash_model.hpp"
#include <cstdio>
#include <set>

using namespace dash;
int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)

static const exbox_games::Tile *tile(const Model &m,const std::string &title) {
    for(const auto &row:m.games_frontend.tiles)if(row.entry.title==title)return &row;
    return nullptr;
}

int main() {
    Model m;m.welcome_visible=false;
    m.set_games({
        {"PS5X360","","PPSA50011","player.png"},
        {"Astro","","PPSA00001","astro.png"},
        {"Bad","","BAD","bad.png"},
    });
    GameFolderInventory inventory;
    inventory.source={"owner-roms","/data/homebrew/PPSA50011/assets/roms",exbox_games::Availability::Available};
    inventory.folders={{"Sonic 06","Sonic the Hedgehog (2006)","sonic06"},{"Halo 3","","halo3"},{"Halo 3","duplicate","wrong"},{"../bad","",""}};
    inventory.limited=true;m.set_game_frontend_inventory(inventory);
    CHECK(m.games_frontend_limited);CHECK(m.games_frontend_rejected==1);
    CHECK(m.games_frontend.entries.size()==3); // two 360 folders + Astro; player and invalid PS5 IDs excluded
    CHECK(tile(m,"Sonic the Hedgehog (2006)"));CHECK(tile(m,"Halo 3"));CHECK(tile(m,"Astro"));CHECK(!tile(m,"PS5X360"));
    const auto sonic=tile(m,"Sonic the Hedgehog (2006)");
    CHECK(sonic&&sonic->entry.key==exbox_games::identity("owner-roms","Sonic 06"));
    CHECK(sonic&&sonic->entry.launch_title==exbox_games::player_title);

    // Selection survives reordered metadata refresh by stable source/folder key.
    for(std::size_t i=0;i<m.games_frontend.tiles.size();++i)if(m.games_frontend.tiles[i].entry.title=="Sonic the Hedgehog (2006)")m.games_frontend.selected=i;
    const auto selected_key=m.games_frontend.tiles[m.games_frontend.selected].entry.key;
    std::reverse(inventory.folders.begin(),inventory.folders.end());m.set_game_frontend_inventory(inventory);
    CHECK(m.games_frontend.tiles[m.games_frontend.selected].entry.key==selected_key);

    // G1 exposes intents but never dispatches the fixed picker or a guessed path.
    m.games_frontend.controls_focused=false;
    auto intent=m.games_frontend.press(exbox_games::Button::A);
    CHECK(!intent.dispatch_allowed);CHECK(intent.title_id==exbox_games::player_title);CHECK(intent.picker);
    CHECK(intent.reason.find("verification")!=std::string::npos);

    // Continue Playing exposes supplied Xbox metadata truthfully but stays
    // inert until the cooperative launch/return contract is accepted.
    const std::string recent_key=exbox_games::identity("owner-roms","Sonic 06");
    m.played(recent_key);m.open_collection(Collection::Recent);
    const auto recent_cards=m.library_cards();CHECK(recent_cards.size()==1);
    CHECK(recent_cards[0].subtitle.find("dispatch pending")!=std::string::npos);
    const auto hints=m.hints();
    CHECK(std::none_of(hints.begin(),hints.end(),[](const Hint&hint){return hint.button==xb::A||hint.button==xb::Y;}));
    CHECK(std::any_of(hints.begin(),hints.end(),[](const Hint&hint){return hint.button==xb::X&&hint.label=="Refresh";}));
    CHECK(m.press(xb::A).command==Command::None);
    CHECK(m.press(xb::Y).command==Command::None&&!m.info);

    m.games_frontend.filter=exbox_games::Filter::Xbox360;m.games_frontend.show_xbox=false;m.games_frontend.show_ps5=true;m.games_frontend.refresh();
    const auto saved=m.saved_game_frontend_settings();CHECK(saved.size()==3);
    Model restored;for(const auto &line:saved)restored.load_game_frontend_setting(line+"\r\n");
    // CRLF is rejected instead of accepting malformed persisted values.
    CHECK(restored.games_frontend.filter==exbox_games::Filter::All);CHECK(restored.games_frontend.show_xbox);
    for(const auto &line:saved)restored.load_game_frontend_setting(line);
    CHECK(restored.games_frontend.filter==exbox_games::Filter::Xbox360);CHECK(!restored.games_frontend.show_xbox);CHECK(restored.games_frontend.show_ps5);
    restored.load_game_frontend_setting("games_frontend_filter=9");restored.load_game_frontend_setting("games_frontend_show_ps5=-1");
    CHECK(restored.games_frontend.filter==exbox_games::Filter::Xbox360);CHECK(restored.games_frontend.show_ps5);

    // Invalid source metadata is bounded to a visible rejected count, never a crash.
    inventory.source.id="../bad";m.set_game_frontend_inventory(inventory);
    CHECK(m.games_frontend_rejected==inventory.folders.size());CHECK(m.games_frontend.entries.size()==1);CHECK(m.games_frontend.entries[0].title=="Astro");
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}

