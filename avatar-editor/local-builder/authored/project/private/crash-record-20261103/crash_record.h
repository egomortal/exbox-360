// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace exbox::diagnostics {
// Preopen a dedicated marker descriptor outside any signal handler. This
// primitive installs no handler and must not intercept recoverable MMIO faults.
bool record_signal(int preopened_fd, int signal_number) noexcept;
// Call before rotating the previous boot log. Failure leaves inputs untouched.
// Report labels unknown signals honestly; no crash cause is inferred from logs.
bool previous_report(const char* log, const char* marker, const char* report);
}
