// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../avatar_editor/native/return_ticket.h"
#include <optional>
#include <string>
namespace exbox::editor {
struct SaveSnapshot {
    bool readable=true;
    std::optional<std::string> manifest, metadata;
};
// Metadata is the final commit marker. File timestamps are deliberately ignored.
// An unchanged session is a frontend cancellation, not a guest Cancel event.
inline av2::native::ExitResult session_outcome(const SaveSnapshot& before,
    const SaveSnapshot& after, bool normal_exit, bool complete_metadata) {
    using Result=av2::native::ExitResult;
    if(!normal_exit || !before.readable || !after.readable)return Result::failed;
    if(before.manifest==after.manifest && before.metadata==after.metadata)return Result::cancelled;
    if(after.manifest && !after.manifest->empty() && after.metadata &&
       before.metadata!=after.metadata && complete_metadata)return Result::saved;
    return Result::failed; // Changed data without a new complete commit.
}
}
