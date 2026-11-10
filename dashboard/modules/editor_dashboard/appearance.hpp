// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only bridge for the editor's X_AVATAR_METADATA. Field positions and
// byte order follow its guest_asset.h / asset_pack.h, not the dashboard Look.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include "bytes.hpp"

namespace editor_dashboard::appearance {
struct AssetId {
    std::array<std::uint8_t,16> bytes{};
    bool empty() const { for(auto b:bytes)if(b)return false;return true; }
    std::uint16_t pack_index() const {return std::uint16_t(bytes[4])<<8|bytes[5];}
    bool operator==(const AssetId& other) const {return bytes==other.bytes;}
};
struct Component {
    AssetId id;
    std::uint16_t categories=0;
    bool operator==(const Component& other) const {return id==other.id&&categories==other.categories;}
};
struct Texture {
    AssetId id;
    // scale, rotation, translation x/y: preserve original values even when
    // a particular material does not use them.
    std::array<float,4> transform{};
    bool operator==(const Texture& other) const {return id==other.id&&transform==other.transform;}
};
struct Manifest {
    // Preserve the complete save as identity: unparsed trailing fields must
    // never collapse two different committed editor saves into one cache key.
    std::array<std::uint8_t,1000> original{};
    std::uint32_t version=0;
    float weight=0,height=0;
    std::array<AssetId,3> shapes;
    std::array<Texture,6> textures;
    std::array<std::uint32_t,9> colors;
    Component body,head;
    std::array<Component,13> components;
    std::array<Component,4> fallback;
    bool same_commit(const Manifest& other) const {return original==other.original;}
};
inline std::optional<Manifest> parse(ByteView data) {
    if(data.size()!=1000||!data.data())return {};
    Manifest out;
    std::memcpy(out.original.data(),data.data(),1000);
    const auto u16=[&](std::size_t o){return std::uint16_t(std::uint16_t(data[o])<<8|data[o+1]);};
    const auto u32=[&](std::size_t o){return std::uint32_t(data[o])<<24|std::uint32_t(data[o+1])<<16|std::uint32_t(data[o+2])<<8|data[o+3];};
    const auto real=[&](std::size_t o){auto bits=u32(o);float f;std::memcpy(&f,&bits,4);return f;};
    const auto id=[&](std::size_t o){AssetId a;std::memcpy(a.bytes.data(),data.data()+o,16);return a;};
    const auto comp=[&](std::size_t o){return Component{id(o),u16(o+16)};};
    out.version=u32(0);out.weight=real(4);out.height=real(8);
    if(!std::isfinite(out.weight)||!std::isfinite(out.height))return {};
    for(std::size_t i=0;i<3;++i)out.shapes[i]=id(0x0c+16*i);
    for(std::size_t i=0;i<6;++i){
        auto& t=out.textures[i];t.id=id(0x3c+32*i);
        for(std::size_t j=0;j<4;++j){t.transform[j]=real(0x4c+32*i+4*j);if(!std::isfinite(t.transform[j]))return {};}
    }
    for(std::size_t i=0;i<9;++i)out.colors[i]=u32(0xfc+4*i);
    out.body=comp(0x120);out.head=comp(0x140);
    for(std::size_t i=0;i<13;++i)out.components[i]=comp(0x160+32*i);
    for(std::size_t i=0;i<4;++i)out.fallback[i]=comp(0x300+32*i);
    return out;
}

// Do not publish a candidate appearance from bytes observed during a save.
// Caller supplies reads bracketing resolution; equality includes every byte.
inline bool coherent(const Manifest& candidate,ByteView after) {
    return after.data()&&after.size()==candidate.original.size()&&
        std::memcmp(candidate.original.data(),after.data(),after.size())==0;
}
} // namespace editor_dashboard::appearance
