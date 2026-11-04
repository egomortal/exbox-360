// NXE PS5 - Native loopback IPC for the dashboard payload service.
// Copyright (C) 2026 NXE PS5 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace nxe
{
struct Homebrew
{
    std::string title;
    std::string executable;
};

std::vector<Homebrew> discover_homebrew();
bool launch_homebrew(const Homebrew &app);
void suspend_relaunch();
} // namespace nxe
