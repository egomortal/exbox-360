#include "avatar_kit.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc < 3 || argc > 5) return 2;
    const int samples = argc >= 4 ? std::stoi(argv[3]) : 4;
    const int model_index = argc == 5 ? std::stoi(argv[4]) : 1;
    if (samples < 4 || samples > 360) return 2;
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    avkit::Kit kit;
    if (!kit.load(bytes.data(), bytes.size())) return 3;
    avkit::use_kit(&kit);
    avkit::Look look = avkit::fresh_look();
    const auto &cats = avkit::categories();
    int found = 0;
    for (const auto &cat : cats) {
        if (std::string(cat.label) == "Body Type") avkit::set_value(look, cat, 1);
        if (std::string(cat.label) == "Image Model") {
            if (model_index < 1 || model_index >= cat.count) return 2;
            avkit::set_value(look, cat, model_index);
            found = 1;
        }
    }
    if (!found) return 4;
    avkit::fit_catalog(look);
    avkit::Renderer renderer(kit);
    renderer.set_look(look);
    for (int n = 0; n < samples; ++n) {
        avkit::Image image;
        image.resize(460, 700);
        avkit::Camera camera = avkit::Camera::full();
        camera.yaw = float(n) * 6.28318530718f / float(samples);
        const auto started = std::chrono::steady_clock::now();
        renderer.render(image, renderer.pose("idle", 0.3f), camera, 2);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        char path[1024];
        std::snprintf(path, sizeof(path), "%s/image-model-%d.ppm", argv[2], n);
        std::ofstream out(path, std::ios::binary);
        out << "P6\n" << image.w << " " << image.h << "\n255\n";
        int covered = 0;
        for (std::uint32_t px : image.argb) {
            if (px >> 24) ++covered;
            const unsigned a = px >> 24;
            const char rgb[3] = {
                char((((px >> 16) & 255) * a + 210 * (255 - a)) / 255),
                char((((px >> 8) & 255) * a + 225 * (255 - a)) / 255),
                char(((px & 255) * a + 202 * (255 - a)) / 255)};
            out.write(rgb, 3);
        }
        std::printf("view %d: %d visible pixels, %lld ms\n", n, covered,
                    static_cast<long long>(elapsed));
        if (covered < 10000) return 5;
    }
    avkit::use_kit(nullptr);
}
