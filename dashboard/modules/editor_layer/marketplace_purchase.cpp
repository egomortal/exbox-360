// SPDX-License-Identifier: GPL-3.0-or-later
#include "marketplace_purchase.hpp"
namespace exbox::editor {
void marketplace_offline_purchase(PPCRegister&) {
    // This editor is permanently offline. The pinned original returns for an
    // empty/installed ID; an absent ID fails retrieval before installing,
    // changing generation, or broadcasting content-installed. Preserve that
    // no-op contract without invoking a transport or touching asset files.
    // Online purchasing is deliberately not part of this adapter's contract.
}
}
