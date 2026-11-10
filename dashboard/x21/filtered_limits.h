// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef EXBOX_FILTERED_MAX_SLOTS
#define EXBOX_FILTERED_MAX_SLOTS 21
#endif
#if EXBOX_FILTERED_MAX_SLOTS < 4 || EXBOX_FILTERED_MAX_SLOTS > 21
#error EXBOX_FILTERED_MAX_SLOTS must be between 4 and 21
#endif
// Count changes do not increase the64MiB output cache or240MiB shared ledger.
