#include "closet_filter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <tuple>

namespace av2::closet_filter {
namespace {

std::string trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  return value.empty() ? std::string(kOther) : value;
}

char fold(char ch) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

bool less_text(std::string_view a, std::string_view b) {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
      [](char x, char y) { return fold(x) < fold(y); });
}

bool equal_text(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
      [](char x, char y) { return fold(x) == fold(y); });
}

bool contains_text(std::string_view value, std::string_view query) {
  if (query.empty()) return true;
  return std::search(value.begin(), value.end(), query.begin(), query.end(),
      [](char x, char y) { return fold(x) == fold(y); }) != value.end();
}

std::pair<std::string, std::string> fallback_group(std::uint32_t mask) {
  struct Row { std::uint32_t bit; const char* type; const char* category; };
  static constexpr std::array<Row, 13> rows{{
      {0x8, "Clothes", "Tops"}, {0x10, "Clothes", "Bottoms"},
      {0x20, "Clothes", "Shoes"}, {0x40, "Clothes", "Headwear"},
      {0x100, "Accessories", "Glasses"}, {0x200, "Accessories", "Earrings"},
      {0x400, "Accessories", "Wristwear"}, {0x800, "Accessories", "Rings"},
      {0x1000, "Props", "Props"}, {0x2000, "Accessories", "Accessories"},
      {0x8000, "Clothes", "Outfits"}, {0x10000, "Clothes", "Outfits"},
      {0x80000, "Props", "Props"},
  }};
  for (const auto& row : rows) if (mask & row.bit) return {row.type, row.category};
  return {std::string(kOther), std::string(kOther)};
}

std::string infer_color(std::string_view name) {
  static constexpr std::array<std::string_view, 13> colors{
      "Black", "White", "Grey", "Gray", "Red", "Blue", "Green",
      "Yellow", "Orange", "Purple", "Pink", "Brown", "Gold"};
  for (auto color : colors) if (contains_text(name, color))
    return color == "Gray" ? "Grey" : std::string(color);
  return std::string(kOther);
}

const std::string& field(const Item& item, Dimension dimension) {
  switch (dimension) {
    case Dimension::kType: return item.type;
    case Dimension::kSourceGame: return item.source_game;
    case Dimension::kCategory: return item.category;
    case Dimension::kColor: return item.color;
    case Dimension::kTag: break;
  }
  return item.type;  // tags are counted separately
}

}  // namespace

Item make_owned_item(std::string guid, std::string name, std::uint32_t category_mask,
                     std::string type, std::string source_game, std::string category,
                     std::string color, std::vector<std::string> tags) {
  auto fallback = fallback_group(category_mask);
  Item result;
  result.guid = std::move(guid);
  result.name = trim(std::move(name));
  result.type = type.empty() ? fallback.first : trim(std::move(type));
  result.source_game = trim(std::move(source_game));
  result.category = category.empty() ? fallback.second : trim(std::move(category));
  result.color = color.empty() ? infer_color(result.name) : trim(std::move(color));
  for (auto& tag : tags) {
    tag = trim(std::move(tag));
    if (tag != kOther && std::none_of(result.tags.begin(), result.tags.end(),
        [&](const std::string& have) { return equal_text(have, tag); }))
      result.tags.push_back(std::move(tag));
  }
  if (result.tags.empty()) result.tags.emplace_back(kOther);
  return result;
}

bool matches(const Item& item, const Filter& filter) {
  if (filter.favorites_only && !item.favorite) return false;
  if (!filter.type.empty() && !equal_text(item.type, filter.type)) return false;
  if (!filter.source_game.empty() && !equal_text(item.source_game, filter.source_game)) return false;
  if (!filter.category.empty() && !equal_text(item.category, filter.category)) return false;
  if (!filter.color.empty() && !equal_text(item.color, filter.color)) return false;
  if (!filter.tag.empty() && std::none_of(item.tags.begin(), item.tags.end(),
      [&](const std::string& tag) { return equal_text(tag, filter.tag); })) return false;
  return true;
}

std::vector<Choice> choices(const std::vector<Item>& owned, Dimension dimension,
                            const Filter& base) {
  std::map<std::string, std::size_t, decltype(&less_text)> counts(&less_text);
  for (const auto& item : owned) {
    if (!matches(item, base)) continue;
    if (dimension == Dimension::kTag) {
      for (const auto& tag : item.tags) ++counts[tag];
    } else {
      ++counts[field(item, dimension)];
    }
  }
  std::vector<Choice> result;
  result.reserve(counts.size());
  for (auto& [label, count] : counts) result.push_back({label, count});
  return result;
}

std::vector<Choice> search_choices(const std::vector<Choice>& owned_choices,
                                   std::string_view query) {
  std::vector<Choice> result;
  for (const auto& choice : owned_choices)
    if (contains_text(choice.label, query)) result.push_back(choice);
  return result;
}

std::optional<std::size_t> alpha_jump(const std::vector<Choice>& values, char letter,
                                      std::size_t after) {
  if (values.empty()) return std::nullopt;
  const char target = fold(letter);
  for (std::size_t offset = 0; offset < values.size(); ++offset) {
    const std::size_t index = (after + offset) % values.size();
    if (!values[index].label.empty() && fold(values[index].label.front()) == target) return index;
  }
  return std::nullopt;
}

std::vector<std::size_t> select(const std::vector<Item>& owned, const Filter& filter,
                                SortMode sort) {
  std::vector<std::size_t> result;
  for (std::size_t i = 0; i < owned.size(); ++i) if (matches(owned[i], filter)) result.push_back(i);
  const auto key = [&](const Item& item) -> std::tuple<std::string_view, std::string_view> {
    switch (sort) {
      case SortMode::kType: return {item.type, item.name};
      case SortMode::kSourceGame: return {item.source_game, item.name};
      case SortMode::kCategory: return {item.category, item.name};
      case SortMode::kColor: return {item.color, item.name};
      default: return {item.name, item.guid};
    }
  };
  std::stable_sort(result.begin(), result.end(), [&](std::size_t a, std::size_t b) {
    if (sort == SortMode::kRecentlyUsed && owned[a].last_used != owned[b].last_used)
      return owned[a].last_used > owned[b].last_used;
    auto [a1, a2] = key(owned[a]); auto [b1, b2] = key(owned[b]);
    return less_text(a1, b1) || (equal_text(a1, b1) && less_text(a2, b2));
  });
  return result;
}

}  // namespace av2::closet_filter
