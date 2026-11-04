// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/session_outcome.hpp"
#include <cassert>
using namespace exbox::editor;
using R=av2::native::ExitResult;
int main() {
    SaveSnapshot seed{true,"seed","old"}, end=seed;
    assert(session_outcome(seed,end,true,true)==R::cancelled);
    end.manifest="changed";assert(session_outcome(seed,end,true,true)==R::failed);
    end.metadata="new";assert(session_outcome(seed,end,true,true)==R::saved);
    assert(session_outcome(seed,end,true,false)==R::failed);
    assert(session_outcome(seed,end,false,true)==R::failed);
    end.manifest=seed.manifest;assert(session_outcome(seed,end,true,true)==R::saved);
    end.readable=false;assert(session_outcome(seed,end,true,true)==R::failed);
    SaveSnapshot empty;assert(session_outcome(empty,empty,true,false)==R::cancelled);
    end={true,"", "new"};assert(session_outcome(seed,end,true,true)==R::failed);
}
