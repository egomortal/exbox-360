// SPDX-License-Identifier: GPL-3.0-or-later
#include "manifest_cache.hpp"
namespace exbox::editor {
namespace {
alignas(16) ManifestBytes bytes{};
bool present = false;
}
ManifestBytes& manifest_bytes() { return bytes; }
bool& manifest_present() { return present; }
static_assert(sizeof(ManifestBytes) == 1000);
}
