#include "title_environment.h"
#include <rex/runtime.h>
#include <rex/kernel/xam/apps/xlivebase_app.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/system/xmemory.h>
#include <rex/logging.h>
#include <filesystem>
namespace av2 {
void initialize_title_environment(rex::Runtime* runtime,rex::system::KernelState* kernel) {
  // Original ae_app.h selects title-owned XLiveBase and mounts the flash fonts.
  rex::kernel::xam::apps::InitializeTitleKernel(runtime,kernel);
  const auto fonts=std::filesystem::absolute(runtime->game_data_root())/"fonts";
  if (!std::filesystem::is_directory(fonts)) {
    REXLOG_WARN("Q5: staged fonts directory is missing: {}",fonts.string()); return;
  }
  auto device=std::make_unique<rex::filesystem::HostPathDevice>("\\Device\\Flash\\Fonts",fonts,true);
  if (device->Initialize() && runtime->file_system()->RegisterDevice(std::move(device)))
    runtime->file_system()->RegisterSymbolicLink("font:","\\Device\\Flash\\Fonts");
}
bool prepare_title_system_pages(rex::Runtime& runtime) {
  // Existing Windows title semantics: zero flag/data pages for the absent
  // system modules. No avatar heap or arena sizing policy is changed here.
  // The old shadow dispatch-table workaround is unnecessary after Q3's fix.
  constexpr struct { uint32_t base,size; } regions[]={{0x8E000000,0x600000},{0x80300000,0x10000}};
  auto* memory=runtime.memory();
  for (const auto& region:regions) {
    auto* heap=memory->LookupHeap(region.base);
    if (!heap || !heap->AllocFixed(region.base,region.size,0x10000,
        rex::memory::kMemoryAllocationReserve|rex::memory::kMemoryAllocationCommit,
        rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite)) {
      REXLOG_ERROR("Q5 system flag page allocation failed at {:08X}",region.base); return false;
    }
    memory->Zero(region.base,region.size);
    REXLOG_INFO("Q5 existing title system flag pages {:08X}+{:X}",region.base,region.size);
  }
  return true;
}
}