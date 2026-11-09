// SPDX-License-Identifier: BSD-3-Clause
#include <rex/kernel/xam/avatar_search.h>
// Existing atomic scope snapshot; the renderer does not read guest memory.
extern "C" bool av2_sort_filter_legend_active() {
  return rex::kernel::xam::GetAvatarCatalogSearchScope() >= 0;
}
