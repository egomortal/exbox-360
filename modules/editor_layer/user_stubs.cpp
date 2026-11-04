// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/ppc/context.h>
// These compatibility entries intentionally do not invent service behavior.
// Ordinary stubs preserve the entire context; constant stubs assign full r3.
#define STUB_preserve(name, value) \
    extern "C" void __wrap_##name(PPCContext& ctx, uint8_t* base) { \
        (void)ctx; (void)base; \
    }
#define STUB_set(name, value) \
    extern "C" void __wrap_##name(PPCContext& ctx, uint8_t* base) { \
        (void)base; ctx.r3.u64 = value; \
    }
#define EDITOR_USER_STUB(name, mode, value) STUB_##mode(name, value)
#include "user_stubs.inc"
#undef EDITOR_USER_STUB
#undef STUB_set
#undef STUB_preserve
