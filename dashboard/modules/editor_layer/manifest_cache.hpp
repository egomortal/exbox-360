// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace exbox::editor {
using ManifestBytes = uint8_t[1000];
// Shared process-lifetime state for retained avatar readers and the own save
// entry point. Array references preserve existing sizeof and raw-byte calls.
// This boundary deliberately adds no locking or serialization policy.
ManifestBytes& manifest_bytes();
bool& manifest_present();
}
