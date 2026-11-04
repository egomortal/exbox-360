// New AV1 host-only check using the unchanged Ex-Box renderer.
// g++ -std=c++17 -Wall -Wextra -Werror -pthread tests/test_avatar360_host.cpp -o build/test-avatar360-host
// build/test-avatar360-host private/avatar360/av1/review/Avatar6404.nxak
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../avatar_kit.hpp"
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc != 2) { std::fprintf(stderr, "usage: test-avatar360-host KIT\n"); return 2; }
    avkit::Kit kit;
    if (!kit.load_file(argv[1])) { std::fprintf(stderr, "kit load failed\n"); return 1; }
    if (kit.joints.size() != 71 || kit.parts.size() != 6 || kit.bodies.size() != 1 ||
        kit.bodies[0].clips.size() != 14 || kit.textures.size() != 7)
        return 1;
    avkit::use_kit(&kit);
    avkit::Renderer renderer(kit);
    renderer.set_look(avkit::fresh_look());
    avkit::Image image;
    image.resize(320, 480);
    renderer.render(image, renderer.pose("idle", 0.3f), avkit::Camera::full(), 1);
    std::size_t covered = 0;
    for (const auto pixel : image.argb) if (pixel >> 24) ++covered;
    avkit::use_kit(nullptr);
    std::printf("AV1: %zu visible pixels, %zu direct bytes\n", covered, avkit::direct_memory_live());
    return covered > 1000 ? 0 : 1;
}
