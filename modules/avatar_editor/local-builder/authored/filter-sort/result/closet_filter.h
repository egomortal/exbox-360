#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace av2::closet_filter {

inline constexpr std::string_view kOther = "Other";

enum class Dimension { kType, kSourceGame, kCategory, kColor, kTag };
enum class SortMode { kName, kType, kSourceGame, kCategory, kColor, kRecentlyUsed };

struct Item {
  std::string guid;
  std::string name;
  std::string type;
  std::string source_game;
  std::string category;
  std::string color;
  std::vector<std::string> tags;
  bool favorite = false;
  std::uint64_t last_used = 0;
};

struct Filter {
  std::string type;
  std::string source_game;
  std::string category;
  std::string color;
  std::string tag;
  bool favorites_only = false;
};

struct Choice {
  std::string label;
  std::size_t count = 0;
};

// Adds a wardrobe-owned item. Blank metadata is normalized to Other.
// The category mask supplies a stable fallback type/category when no sidecar
// metadata exists; no asset bytes are retained.
Item make_owned_item(std::string guid, std::string name, std::uint32_t category_mask,
                     std::string type = {}, std::string source_game = {},
                     std::string category = {}, std::string color = {},
                     std::vector<std::string> tags = {});

bool matches(const Item& item, const Filter& filter);
std::vector<Choice> choices(const std::vector<Item>& owned, Dimension dimension,
                            const Filter& base = {});
std::vector<Choice> search_choices(const std::vector<Choice>& owned_choices,
                                   std::string_view query);
std::optional<std::size_t> alpha_jump(const std::vector<Choice>& choices, char letter,
                                      std::size_t after = 0);
std::vector<std::size_t> select(const std::vector<Item>& owned, const Filter& filter,
                                SortMode sort);

}  // namespace av2::closet_filter
