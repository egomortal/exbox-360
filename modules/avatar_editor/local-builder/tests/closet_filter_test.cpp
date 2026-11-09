#include "closet_filter.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace av2::closet_filter;

int main(int argc, char** argv) {
  std::vector<Item> items;
  items.push_back(make_owned_item("a", "Blue Hoodie", 0x8, {}, "Example Game"));
  items.push_back(make_owned_item("b", "Red Cap", 0x40));
  items.push_back(make_owned_item("c", "Golden Prop", 0x1000, "Props", "Show Pack",
                                  "Hand Props", "Gold", {"Animated", "Owned"}));
  items[1].favorite = true;
  items[0].last_used = 9; items[2].last_used = 20;
  assert(items[1].source_game == "Other" && items[1].category == "Headwear");
  assert(items[0].color == "Blue" && items[1].color == "Red");
  auto games = choices(items, Dimension::kSourceGame);
  assert(games.size() == 3 && games[0].label == "Example Game" && games[1].label == "Other");
  assert(search_choices(games, "show").size() == 1);
  assert(alpha_jump(games, 'o').value() == 1);
  Filter f; f.favorites_only = true;
  assert(select(items, f, SortMode::kName) == std::vector<std::size_t>{1});
  f = {}; f.type = "Props";
  assert(select(items, f, SortMode::kName) == std::vector<std::size_t>{2});
  auto recent = select(items, {}, SortMode::kRecentlyUsed);
  assert(recent.front() == 2 && recent[1] == 0);

  if (argc == 2) {
    std::ifstream input(argv[1]); assert(input);
    std::string line; std::size_t count = 0;
    while (std::getline(input, line)) {
      std::size_t a = line.find('\t'), b = line.find('\t', a + 1), c = line.find('\t', b + 1);
      assert(a != std::string::npos && b != std::string::npos && c != std::string::npos);
      items.push_back(make_owned_item(line.substr(0, a), line.substr(c + 1),
          static_cast<std::uint32_t>(std::stoul(line.substr(a + 1, b - a - 1), nullptr, 16))));
      ++count;
    }
    assert(count == 21293);
    auto source_games = choices(items, Dimension::kSourceGame);
    assert(!source_games.empty());
    bool other = false;
    for (const auto& choice : source_games) if (choice.label == "Other") other = true;
    assert(other);
    auto categories = choices(items, Dimension::kCategory);
    assert(categories.size() >= 5);
    std::cout << "actual-owned=" << count << " categories=" << categories.size()
              << " source-games=" << source_games.size() << '\n';
  }
  std::cout << "closet metadata/filter/sort/search/alpha-jump PASS\n";
}
