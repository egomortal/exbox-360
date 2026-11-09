#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include "rex/kernel/xam/avatar_search.h"
#include "filter_controls.h"
#include <rex/ui/virtual_key.h>

namespace qa {
int scope=-1;
unsigned rebuilds=0;
bool overlay=false;
rex::kernel::xam::AvatarCatalogFilter state;
}
namespace rex::kernel::xam {
int GetAvatarCatalogSearchScope(){return qa::scope;}
AvatarCatalogFilter GetAvatarCatalogFilter(){return qa::state;}
void SetAvatarCatalogFilter(const AvatarCatalogFilter& value){qa::state=value;}
std::vector<AvatarFilterChoice> QueryAvatarFilterChoices(AvatarFilterDimension){
  return {{"Blue",2},{"Green",1}};
}
}
namespace ae_search {
enum class StorePage { kNone,kGamesList,kItems };
struct Controller {void SetFilterTextInput(bool){}};
Controller& Get(){static Controller value;return value;}
void SetFilterMenuOverlay(bool open,const char*,const char*,const char*){qa::overlay=open;}
void RequestCatalogRebuild(){++qa::rebuilds;}
}
