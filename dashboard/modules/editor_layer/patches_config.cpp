// SPDX-License-Identifier: GPL-3.0-or-later
#include <rex/cvar.h>
// Retain the legacy key and default for config compatibility. Own callbacks
// keep hook counters; diagnostic pointer-reading trace output is retired.
REXCVAR_DEFINE_BOOL(ae_mkt_trace, false, "AE", "Legacy marketplace trace option; hook counters remain available.");
