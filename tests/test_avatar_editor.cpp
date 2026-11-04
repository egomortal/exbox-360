#include "../modules/avatar_editor/avatar_editor.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    assert(file.good());
}

int main() {
    // Keep synthetic fixtures for review; never delete source or user files.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::path("build") / ("av2-fixture-" + std::to_string(stamp));
    const std::string root_text = root.string();
    Av2Report report{};
    assert(av2_probe_layout(nullptr, &report) == AV2_BAD_ARGUMENT);
    assert(av2_probe_layout(root_text.c_str(), nullptr) == AV2_BAD_ARGUMENT);
    assert(av2_probe_layout(root_text.c_str(), &report) == AV2_MISSING_ASSET);
    const char* files[] = {
        "avatareditor.xex", "AvatarAssetPack.toc", "AvatarAssetPackLegacyV1.toc",
        "fonts/xenonclatin.xtt", "fonts/xenonjklatin.xtt",
        "Avatar6400.Avatar", "Avatar6404.Avatar", "Avatar6411.Avatar",
        "Cheer.AvatarAnimation", "Gift.AvatarAnimation", "IdleOffScreen.AvatarAnimation",
        "Look.AvatarAnimation", "RunIn.AvatarAnimation", "RunOutLong.AvatarAnimation",
        "RunOutMedium.AvatarAnimation", "RunOutShort.AvatarAnimation",
        "RunOutStandard.AvatarAnimation", "Salute.AvatarAnimation"
    };
    for (const char* name : files) write(root / "assets" / name, "fixture");
    const auto index = root / "assets/closet/closet_index.tsv";
    const std::string row = "00000008-028c-0002-407c-9db1f97f5bcb\t8\t2\tFixture";
    write(index, row + "\r\n" + row);
    assert(av2_probe_layout(root_text.c_str(), &report) == AV2_OK);
    assert(report.catalog_rows == 2 && report.max_catalog_line_bytes == row.size());
    assert(!report.core_started);
    assert(av2_initialize_headless(root_text.c_str(), &report) == AV2_CORE_UNAVAILABLE);
    assert(!report.core_started);
    write(index, row + "\ninvalid\n");
    assert(av2_probe_layout(root_text.c_str(), &report) == AV2_INVALID_CATALOG);
    write(index, row + std::string(4096, 'x'));
    assert(av2_probe_layout(root_text.c_str(), &report) == AV2_INVALID_CATALOG);
    write(index, row + std::string(1, '\0'));
    assert(av2_probe_layout(root_text.c_str(), &report) == AV2_INVALID_CATALOG);
    write(index, "\r\n");
    assert(av2_probe_layout(root_text.c_str(), &report) == AV2_INVALID_CATALOG);
    write(index, row + "\n");
    write(root / "assets/avatareditor.xex", "");
    assert(av2_initialize_headless(root_text.c_str(), &report) == AV2_MISSING_ASSET);
    const std::string long_root(3000, 'x');
    assert(av2_probe_layout(long_root.c_str(), &report) == AV2_BAD_ARGUMENT);
    return 0;
}
