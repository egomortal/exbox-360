#include "../modules/themes/theme_manifest.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto parsed = exbox::themes::parse_manifest(text);
    if (!parsed) { std::cerr << parsed.error << '\n'; return 1; }
    std::size_t complete = 0;
    for (const auto& entry : parsed.entries) complete += entry.complete;
    std::cout << "themes=" << parsed.entries.size() << " complete=" << complete
              << " partial=" << parsed.entries.size()-complete << " rejected=" << parsed.rejected << '\n';
    return 0;
}
