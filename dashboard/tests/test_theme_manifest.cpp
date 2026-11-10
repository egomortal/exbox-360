#include "../modules/themes/theme_manifest.hpp"
#include "../modules/themes/sha256.hpp"
#include "../modules/themes/theme_runtime.hpp"

#include <cassert>
#include <iostream>
#include <sstream>

using namespace exbox::themes;

static std::string manifest_for(const std::string& directory, const std::string& name,
                                const std::vector<Asset>& assets, bool complete) {
    const auto id = stable_identity(directory, assets);
    std::ostringstream out;
    out << "EXBOX-THEME-CATALOG 1\nTHEME " << std::quoted(id) << ' ' << std::quoted(name)
        << ' ' << std::quoted(directory) << ' ' << int(complete) << '\n';
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const std::string role = i == 0 ? "thumbnail" : "wallpaper" + std::to_string(i);
        out << "ASSET " << std::quoted(id) << ' ' << std::quoted(role) << ' '
            << std::quoted(assets[i].relative_path) << ' ' << assets[i].bytes << ' '
            << std::quoted(assets[i].content_sha256) << ' '
            << (assets[i].encoding == EncodedImage::png ? "png" : "jpeg") << '\n';
    }
    return out.str();
}

int main() {
    assert(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string aa(64, 'a'), bb(64, 'b'), cc(64, 'c'), dd(64, 'd'), ee(64, 'e');
    const std::vector<Asset> complete{
        {"Theme/Thumbnail.png", 120, aa, EncodedImage::jpeg},
        {"Theme/Wallpaper1.png", 1000, bb, EncodedImage::jpeg},
        {"Theme/Wallpaper2.png", 1001, cc, EncodedImage::png},
        {"Theme/Wallpaper3.png", 1002, dd, EncodedImage::png},
        {"Theme/Wallpaper4.png", 1003, ee, EncodedImage::jpeg},
    };
    auto parsed = parse_manifest(manifest_for("Theme", "Owner Theme", complete, true));
    assert(parsed); assert(parsed.rejected == 0); assert(parsed.entries.size() == 1);
    assert(parsed.entries[0].complete); assert(parsed.entries[0].thumbnail);
    assert(parsed.entries[0].wallpapers[0]->encoding == EncodedImage::jpeg);
    assert(parsed.entries[0].wallpapers[1]->encoding == EncodedImage::png);

    auto partial = complete; partial.pop_back();
    parsed = parse_manifest(manifest_for("Partial", "Partial Theme", partial, false));
    assert(parsed && parsed.entries.size() == 1 && !parsed.entries[0].complete);

    auto wrong_complete = parse_manifest(manifest_for("Partial", "Partial Theme", partial, true));
    assert(wrong_complete && wrong_complete.entries.empty() && wrong_complete.rejected == 1);
    auto tampered = manifest_for("Theme", "Owner Theme", complete, true);
    tampered.replace(tampered.find(bb), bb.size(), std::string(64, 'f'));
    parsed = parse_manifest(tampered); assert(parsed && parsed.entries.empty() && parsed.rejected == 1);

    assert(!parse_manifest("EXBOX-THEME-CATALOG 2\n"));
    auto traversal = manifest_for("Theme", "Owner Theme", complete, true);
    traversal.replace(traversal.find("Theme/Wallpaper1.png"), 20, "../escape/image.jpg");
    assert(!parse_manifest(traversal));
    auto duplicate = manifest_for("Theme", "Owner Theme", complete, true);
    duplicate += duplicate.substr(duplicate.find("ASSET"));
    assert(!parse_manifest(duplicate));
    assert(!parse_manifest(manifest_for("Theme", "Owner Theme", complete, true), 0));
    assert(lowercase_sha256(aa)); assert(!lowercase_sha256(std::string(64, 'A')));
    assert(stable_id(stable_identity("Theme", complete))); assert(!stable_id("theme-bad"));
    for(int mask=0;mask<16;++mask)for(int requested=0;requested<4;++requested){
        CatalogEntry entry;
        for(int slot=0;slot<4;++slot)if(mask&(1<<slot))entry.wallpapers[std::size_t(slot)]=complete[std::size_t(slot+1)];
        const auto compatible=route_wallpaper(entry,requested,RouteMode::Compatibility);
        const auto authored=route_wallpaper(entry,requested,RouteMode::Authored);
        if(mask==0){assert(compatible.status==RouteStatus::Unavailable&&!compatible.asset);assert(authored.status==RouteStatus::Unavailable&&!authored.asset);}
        else{
            int lowest=0;while(!(mask&(1<<lowest)))++lowest;
            assert(compatible.status==RouteStatus::Compatibility&&compatible.source_slot==lowest&&compatible.asset==&*entry.wallpapers[std::size_t(lowest)]);
            assert(authored.status==RouteStatus::DefaultBackground&&authored.source_slot==-1&&!authored.asset);
            exbox_services::theme_scene::Evidence evidence;
            evidence.present_roles=exbox_services::theme_scene::bit(exbox_services::theme_scene::Role::Background);
            evidence.owner_assets_verified=evidence.route_table_proven=evidence.z_order_proven=evidence.crop_sampling_proven=true;
            const auto proven=route_wallpaper(entry,requested,RouteMode::Authored,evidence);
            if(mask&(1<<requested))assert(proven.status==RouteStatus::Exact&&proven.source_slot==requested&&proven.asset==&*entry.wallpapers[std::size_t(requested)]);
            else assert(proven.status==RouteStatus::DefaultBackground&&proven.source_slot==-1&&!proven.asset);
        }
    }
    std::cout << "theme_manifest: routing matrix and manifest assertions passed\n";
}
