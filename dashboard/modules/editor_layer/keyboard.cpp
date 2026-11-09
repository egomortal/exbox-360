// SPDX-License-Identifier: GPL-3.0-or-later
#include "keyboard_text.hpp"
#include <rex/hook.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>
namespace exbox::editor {
using rex::X_RESULT;
u32 keyboard_entry(u32,u32,mapped_wstring initial,mapped_wstring,mapped_wstring,
                   mapped_wstring output,u32 capacity,mapped_void overlapped) {
    if(!output || !capacity)return X_ERROR_INVALID_PARAMETER;
    std::vector<uint8_t> snapshot;
    if(initial) {
        const auto text=initial.value();
        snapshot=keyboard_default({reinterpret_cast<const uint8_t*>(text.data()),text.size()*2});
    }
    auto complete=[output,capacity,snapshot=std::move(snapshot)](uint32_t& error,uint32_t& length)->X_RESULT {
        const bool copied=keyboard_copy(snapshot,
            {reinterpret_cast<uint8_t*>(output.host_address()),size_t(capacity)*2},capacity);
        error=X_ERROR_SUCCESS;length=0;
        return copied?X_ERROR_SUCCESS:X_ERROR_INVALID_PARAMETER;
    };
    if(!overlapped){uint32_t error=0,length=0;return complete(error,length);}
    auto* kernel=rex::system::kernel_state();
    kernel->CompleteOverlappedDeferredEx(std::move(complete),overlapped.guest_address(),
        [kernel]{kernel->BroadcastNotification(9,1);},
        [kernel]{kernel->BroadcastNotification(9,0);});
    return X_ERROR_IO_PENDING;
}
}
REX_EXPORT(__imp__XamShowKeyboardUI,exbox::editor::keyboard_entry)
