// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
namespace exbox::editor {
// Observed 12581 handle ABI: upper 16 bits are a generation, lower 16 an
// index. The supplied reader returns a decoded big-endian word only when the
// complete four-byte guest range is readable. This helper never writes memory.
// Addresses are revision-specific evidence, not a promise for other editors.
struct XuiHandleLayout {
    uint32_t table=0x9457EAF8U;
    uint32_t count=0x9457EF18U;
};
template<class ReadWord>
std::optional<uint32_t> xui_element(uint32_t handle,ReadWord read,
                                   XuiHandleLayout layout={}) {
    if(!handle)return {};
    const auto count=read(layout.count);
    const uint32_t index=handle&0xffffU;
    if(!count || index>=*count)return {};
    const auto page=read(layout.table+((index>>6)&0x3fffffcU));
    if(!page || !*page)return {};
    const uint32_t offset=(index*8U)&0x7f8U;
    if(*page>UINT32_MAX-offset-4U)return {};
    const uint32_t entry=*page+offset;
    const auto generation=read(entry),first=read(entry+4U);
    if(!generation || *generation!=(handle>>16) || !first || !*first)return {};
    std::array<uint32_t,65> visited{};
    uint32_t node=*first;
    // A malformed chain cannot make UI traversal hang. Valid scenes terminate
    // with a zero next link; missing data is distinct from a valid zero word.
    for(std::size_t depth=0;depth<visited.size();++depth) {
        for(std::size_t i=0;i<depth;++i)if(visited[i]==node)return {};
        visited[depth]=node;
        if(node>UINT32_MAX-32U)return {};
        const auto next=read(node+4U);
        if(!next)return {};
        if(!*next) {
            const auto element=read(node+32U);
            return element && *element ? element : std::nullopt;
        }
        node=*next;
    }
    return {};
}
} // namespace exbox::editor
