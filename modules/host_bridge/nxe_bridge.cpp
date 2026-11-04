// NXE PS5 - Native loopback IPC to the NXE payload service.
// Copyright (C) 2026 NXE PS5 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/nxe_bridge.hpp"

#include <string>
#include <string_view>
#include <vector>

#ifdef __PROSPERO__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace nxe
{
namespace
{
#ifdef __PROSPERO__
constexpr std::size_t kMaxReply = 64 * 1024;

bool exchange(std::string_view request, std::string &reply)
{
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;
    const timeval timeout{2, 0};
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7090);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool okay = connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
    std::size_t sent = 0;
    while (okay && sent < request.size())
    {
        const int count = send(sock, request.data() + sent, request.size() - sent, 0);
        if (count <= 0)
            okay = false;
        else
            sent += static_cast<std::size_t>(count);
    }
    reply.clear();
    char buffer[1024];
    while (okay && reply.size() < kMaxReply)
    {
        const int count = recv(sock, buffer, sizeof(buffer), 0);
        if (count < 0)
            okay = false;
        else if (count == 0)
            break;
        else
            reply.append(buffer, static_cast<std::size_t>(count));
    }
    (void)close(sock);
    return okay && reply.size() < kMaxReply && reply.rfind("OK\n", 0) == 0;
}

bool safe_field(std::string_view value)
{
    if (value.empty() || value.size() > 1024)
        return false;
    for (char c : value)
        if (c == '\n' || c == '\r' || c == '\t' || c == '\0')
            return false;
    return true;
}
#endif
} // namespace

std::vector<Homebrew> discover_homebrew()
{
#ifdef __PROSPERO__
    std::string reply;
    if (!exchange("LIST\n", reply))
        return {};
    std::vector<Homebrew> apps;
    std::size_t cursor = 3;
    while (cursor < reply.size() && apps.size() < 128)
    {
        const std::size_t end = reply.find('\n', cursor);
        if (end == std::string::npos)
            break;
        const std::string_view line(reply.data() + cursor, end - cursor);
        if (line == ".")
            break;
        const std::size_t tab = line.find('\t');
        if (tab != std::string_view::npos)
        {
            const std::string_view title = line.substr(0, tab);
            const std::string_view path = line.substr(tab + 1);
            if (safe_field(title) && safe_field(path))
                apps.push_back({std::string(title), std::string(path)});
        }
        cursor = end + 1;
    }
    return apps;
#else
    return {{"Omni", "/data/homebrew/Omni/eboot.elf"}};
#endif
}

bool launch_homebrew(const Homebrew &app)
{
#ifdef __PROSPERO__
    if (!safe_field(app.executable))
        return false;
    std::string reply;
    return exchange("LAUNCH\t" + app.executable + "\n", reply);
#else
    (void)app;
    return false;
#endif
}

void suspend_relaunch()
{
#ifdef __PROSPERO__
    std::string reply;
    (void)exchange("SUSPEND\n", reply);
#endif
}
} // namespace nxe
