} // namespace ae_search (actual source excerpt)

int main(){
  using rex::ui::VirtualKey;
  using namespace ae_search;
  const auto event=[](VirtualKey key,uint16_t flags){return HandleFilterPad(uint16_t(key),flags);};
  assert(!event(VirtualKey::kXInputPadY,1));
  assert(!event(VirtualKey::kXInputPadY,2));
  assert(!FilterMenuOpen()&&!qa::overlay);
  qa::scope=3;
  assert(!event(VirtualKey::kXInputPadA,1));
  assert(event(VirtualKey::kXInputPadY,1));
  assert(FilterMenuOpen()&&qa::overlay);
  assert(event(VirtualKey::kXInputPadY,5));
  assert(FilterMenuOpen());
  assert(event(VirtualKey::kXInputPadY,2));
  assert(FilterMenuOpen());
  assert(event(VirtualKey::kXInputPadDpadLeft,1)); // Type wraps to Sort.
  assert(event(VirtualKey::kXInputPadA,1));
  for(char c:std::string("Color"))assert(FilterMenuType(c));
  assert(event(VirtualKey::kXInputPadA,1));
  assert(qa::state.sort==rex::kernel::xam::AvatarSortMode::kColor);
  assert(qa::rebuilds==1);
  assert(event(VirtualKey::kXInputPadB,1));
  assert(!FilterMenuOpen()&&!qa::overlay);
  // Closing Y down, repeat and release must all remain consumed.
  assert(event(VirtualKey::kXInputPadY,1));
  assert(event(VirtualKey::kXInputPadY,2));
  assert(event(VirtualKey::kXInputPadY,1));
  assert(!FilterMenuOpen());
  assert(event(VirtualKey::kXInputPadY,5));
  assert(!FilterMenuOpen());
  assert(event(VirtualKey::kXInputPadY,2));
  assert(!FilterMenuOpen());
  assert(event(VirtualKey::kXInputPadY,1));
  assert(event(VirtualKey::kXInputPadY,2));
  qa::scope=-1;
  assert(!event(VirtualKey::kXInputPadA,1));
  assert(!FilterMenuOpen()&&!qa::overlay);
  assert(!event(VirtualKey::kXInputPadY,1));
  assert(!event(VirtualKey::kXInputPadY,2));
  // Leaving the closet during a held Y must clear the overlay even when a
  // matching repeat/release is consumed to finish that owned button gesture.
  qa::scope=3;
  assert(event(VirtualKey::kXInputPadY,1));
  qa::scope=-1;
  event(VirtualKey::kXInputPadY,5);
  assert(!FilterMenuOpen()&&!qa::overlay);
  event(VirtualKey::kXInputPadY,2);
  assert(!FilterMenuOpen());
  std::cout<<"PASS actual controller source: scope, repeat, release, searched Color sort\n";
}
