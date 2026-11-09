#include "filter_controls.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using av2::closet_filter::SortModeFromLabel;
using av2::closet_filter::TryRewriteMarketplaceLegend;

static std::vector<uint8_t> be(const std::u16string& value, bool terminator=true) {
  std::vector<uint8_t> bytes;
  for (char16_t ch : value) { bytes.push_back(uint8_t(ch>>8)); bytes.push_back(uint8_t(ch)); }
  if (terminator) { bytes.push_back(0); bytes.push_back(0); }
  return bytes;
}

static unsigned checks=0;
static void accepted(const std::u16string& input, const std::u16string& expected) {
  auto bytes=be(input); const auto original=bytes; const auto want=be(expected);
  std::vector<uint8_t> output(want.size()+8,0xA5);
  const auto count=TryRewriteMarketplaceLegend(true,bytes.data(),bytes.size(),output.data(),want.size());
  assert(count==want.size());
  assert(std::equal(want.begin(),want.end(),output.begin()));
  assert(std::all_of(output.begin()+count,output.end(),[](uint8_t value){return value==0xA5;}));
  assert(bytes==original);
  ++checks;
}
static void rejected(const std::u16string& input,bool active=true,bool terminator=true) {
  auto bytes=be(input,terminator); const auto original=bytes;
  std::array<uint8_t,192> output; output.fill(0xA5); const auto before=output;
  assert(TryRewriteMarketplaceLegend(active,bytes.data(),bytes.size(),output.data(),output.size())==0);
  assert(output==before); assert(bytes==original); ++checks;
}

int main() {
  accepted(u"Marketplace",u"Sort / Filter");
  accepted(u"Avatar Marketplace",u"Sort / Filter");
  accepted(u"\uE000 Marketplace",u"\uE000 Sort / Filter");
  accepted(u"\uF8FF  Avatar Marketplace",u"\uF8FF  Sort / Filter");
  rejected(u"Marketplace",false);
  rejected(u"Avatar Marketplace",false);
  rejected(u"Marketplace settings");
  rejected(u"Visit Marketplace");
  rejected(u"marketplace");
  rejected(u"Marketplac");
  rejected(u"Marketplac\u00E9");
  rejected(u"\uE000\uE001 Marketplace");
  rejected(u"\uD7FF Marketplace");
  rejected(u"\uF900 Marketplace");
  rejected(u"Sort / Filter");
  rejected(u"");
  rejected(u"Marketplace",true,false);
  auto bytes=be(u"Marketplace"); std::array<uint8_t,192> output; output.fill(0xA5);
  const auto original=output;
  assert(!TryRewriteMarketplaceLegend(true,nullptr,bytes.size(),output.data(),output.size())); ++checks;
  assert(!TryRewriteMarketplaceLegend(true,bytes.data(),bytes.size(),nullptr,output.size())); ++checks;
  assert(!TryRewriteMarketplaceLegend(true,bytes.data(),0,output.data(),output.size())); ++checks;
  assert(!TryRewriteMarketplaceLegend(true,bytes.data(),bytes.size()-1,output.data(),output.size())); ++checks;
  for (size_t cap=0; cap<be(u"Sort / Filter").size(); ++cap) {
    assert(!TryRewriteMarketplaceLegend(true,bytes.data(),bytes.size(),output.data(),cap)); ++checks;
  }
  assert(output==original);
  // Search narrows choices to one visible row. Selection must keep the original
  // sort identity instead of interpreting that visible row index as Name.
  assert(SortModeFromLabel("Color")==4); ++checks;
  assert(SortModeFromLabel("Source game")==2); ++checks;
  assert(SortModeFromLabel("Name")==0); ++checks;
  assert(SortModeFromLabel("Type")==1); ++checks;
  assert(SortModeFromLabel("Category")==3); ++checks;
  assert(SortModeFromLabel("Unknown")==-1); ++checks;
  assert(SortModeFromLabel("")==-1); ++checks;
  std::cout << "PASS " << checks << " independent caption/sort cases\n";
}
