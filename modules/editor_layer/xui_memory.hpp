// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "xui_range.hpp"
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <cstdint>
#include <optional>
#include <span>
namespace exbox::editor {
// Small guest ranges only. Callers run on the existing guest UI thread; memory
// mappings are assumed stable between validation and access, as in that runtime.
class XuiMemory {
public:
    static std::span<uint8_t> range(uint32_t address,size_t length,uint32_t access) {
        auto* memory=rex::system::kernel_memory();
        if(!memory)return {};
        const bool allowed=xui_range_allowed(address,length,[&](uint32_t at){
            auto* heap=memory->LookupHeap(at);
            uint32_t size=0,protection=0;rex::memory::HeapAllocationInfo region{};
            return heap && heap->QuerySize(at,&size) && size &&
                heap->QueryRegionInfo(at,&region) && (region.state&rex::memory::kMemoryAllocationCommit) &&
                heap->QueryProtect(at,&protection) && (protection&access)==access;
        });
        if(!allowed)return {};
        return {memory->TranslateVirtual<uint8_t*>(address),length};
    }
    static std::optional<uint32_t> word(uint32_t address) {
        const auto bytes=range(address,4,rex::memory::kMemoryProtectRead);
        if(bytes.empty())return {};
        return uint32_t(bytes[0])<<24 | uint32_t(bytes[1])<<16 | uint32_t(bytes[2])<<8 | bytes[3];
    }
    static void put(std::span<uint8_t> output,uint32_t value) {
        for(unsigned i=0;i<4;++i)output[i]=static_cast<uint8_t>(value>>(24-8*i));
    }
};
}
