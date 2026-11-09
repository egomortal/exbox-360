#include "hitch_profile.h"
#include "guest_creation.h"
#include "normal_exit_api.h"
#include "native_pulse.h"
#include "loading_presenter.h"
#include "launch_profile.h"
#include "clean_exit.h"
extern "C" void av2_flush_pipeline_cache();
#include "blocked_exit.h"
#include "hang_monitor.h"
#include "gpu_diagnostics.h"
#include "guest_diagnostics.h"
#include <atomic>
static std::atomic<bool> diagnostic_hang{false};
extern "C" bool av2_early_vulkan_prepare();
extern "C" void av2_early_vulkan_release();
extern "C" bool av2_boot_capture_stdio();
// Q7 native editor entry. Original manifest/PNG save bridge and companion return.
// Built locally; runtime behavior still requires Claude's console test.
#include "ae_init.h"
#include "boot_log.h"
#include "title_environment.h"
#include "gpu/gpu.h"
#include "native_pad.h"
#include "silent_audio.h"
#include "guest_drain.h"
#include "avatar_save_store.h"
#include "dashboard_return.h"
#include <rex/runtime.h>
#include <rex/input/input_system.h>
#include <rex/audio/audio_system.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/logging.h>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <filesystem>
#include <unistd.h>
#include <cstdarg>
#include <dirent.h>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include <stdexcept>
#include "paged_vfs.h"
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/file.h>
extern "C" void exbox_prepare_optional_modules(void(*)(const char*,...));
namespace { void optional_module_log(const char* format,...) {
 char line[512];va_list args;va_start(args,format);std::vsnprintf(line,sizeof(line),format,args);va_end(args);
 av2_boot_log("optional", "%s",line);
} }
extern "C" bool av2_check_native_imports();
extern "C" int sceSystemServiceLoadExec(const char*,const char* const*);
extern "C" [[noreturn]] void catchReturnFromMain(int status) {
  if(!av2_native_pulse_stop())av2_exit_with_live_guests(status);
  av2_launch_phase("exit");av2_launch_dump("exit");av2_hitch_finish("exit");
  av2_boot_log("exit","status=%d stage-or-signal-stop=%d",status,int(av2_boot_should_stop()));
  if(!av2_boot_should_stop()) av2::native::return_to_dashboard(status);
  std::fprintf(stderr,"AV2 native exit status=%d; Q4 shell exit\n",status);
  std::fflush(nullptr);
  const int result=sceSystemServiceLoadExec("exit",nullptr);
  std::fprintf(stderr,"AV2 LoadExec returned %08x; awaiting shell termination\n",unsigned(result));
  std::fflush(nullptr);
  for(;;) ::sleep(1); // Q4: never fall through to refused exit/_exit syscalls.
}
extern "C" int sceKernelGetModuleList(int*,size_t,size_t*);
extern "C" int sceKernelDlsym(int,const char*,void**);
namespace {
bool native_vfs_preflight(const std::filesystem::path& assets);
std::filesystem::path select_asset_root(int argc,char** argv) {
  std::vector<std::string> roots={"/app0/assets","/data/homebrew/PPSA99734/assets","/data/nxe/avatar-editor/assets"};
  std::string requested=argc>1?argv[1]:"";
  if(requested.empty())if(FILE* file=std::fopen("/data/nxe/avatar-editor/asset-root.txt","r")) {
    char line[1024]{};if(std::fgets(line,sizeof(line),file))requested=line;std::fclose(file);
    while(!requested.empty() && (requested.back()=='\n'||requested.back()=='\r'))requested.pop_back();
  }
  if(!requested.empty()) {
    if(requested.front()!='/')throw std::runtime_error("asset-root override must be absolute");
    roots.insert(roots.begin(),requested);
  }
  std::string selected;
  for(const auto& root:roots) {
    struct stat st{};errno=0;
    if(stat(root.c_str(),&st) || !S_ISDIR(st.st_mode)) {
      av2_boot_log("asset-root","function=stat root=%s unavailable errno=%d",root.c_str(),errno);continue;
    }
    const int indexed=av2::platform::configure_asset_index(root);
    av2_boot_log("asset-root","root=%s method=index result=%d",root.c_str(),indexed);
    // Select only after actual guest-VFS reads succeed, not merely because
    // a directory or an index can be opened. Keep probing the remaining roots.
    if(indexed)av2::platform::configure_asset_index("");
    if(selected.empty() && !indexed && (requested.empty() || root==requested)) {
      if(native_vfs_preflight(root)){selected=root;}
    }
  }
  if(selected.empty())throw std::runtime_error("no usable asset root; upload assets/.av2-index or set asset-root.txt");
  if(av2::platform::configure_asset_index(selected))throw std::runtime_error("selected asset index became unavailable");
  av2_boot_log("asset-root","selected=%s mode=index",selected.c_str());return selected;
}
bool native_vfs_preflight(const std::filesystem::path& assets) {
  // Exercise the same VFS/device/file classes as Runtime::SetupVfs, before
  // allocating guest memory. This is not a replacement fopen-only path check.
  rex::filesystem::VirtualFileSystem vfs;
  auto device=std::make_unique<rex::filesystem::HostPathDevice>("\\Device\\Harddisk0\\Partition1",assets,true);
  if(!device->Initialize() || !vfs.RegisterDevice(std::move(device)))return false;
  vfs.RegisterSymbolicLink("game:","\\Device\\Harddisk0\\Partition1");
  std::string item;
  av2::platform::DirectoryPage closet;
  if(!closet.open(assets.c_str(),"closet")) {
    std::array<av2::platform::DirectoryPage::Entry,32> batch{};
    for(;;){size_t count=0;if(closet.next(batch,count)||!count)break;
      for(size_t i=0;i<count;++i){std::string name=batch[i].name;
        if(name.size()==40 && name.ends_with(".bin")){item="closet/"+name;break;}}
      if(!item.empty())break;
    }
  }
  if(item.empty()){av2_boot_log("vfs-self-test","no closet .bin found errno=%d",errno);return false;}
  for(const std::string& name:{std::string("avatareditor.xex"),std::string("ae.pack"),std::string("fonts/xenonclatin.xtt"),item}) {
    const std::string guest="game:\\"+name;
    auto* entry=vfs.ResolvePath(guest);
    if(!entry){av2_boot_log("vfs-self-test","guest=%s resolve failed errno=%d",guest.c_str(),errno);return false;}
    rex::filesystem::File* file=nullptr;
    auto result=entry->Open(rex::filesystem::FileAccess::kGenericRead,&file);
    size_t read=0;std::array<uint8_t,16> header{};
    if(!result && file){result=file->ReadSync(std::span<uint8_t>(header),0,&read);file->Destroy();}
    av2_boot_log("vfs-self-test","guest=%s resolved=%s result=%x read=%zu errno=%d",guest.c_str(),entry->absolute_path().c_str(),result,read,errno);
    if(result || !read)return false;
  }
  return true;
}
}
int main(int argc,char** argv) {
  av2_normal_exit_enable(); // Explicit frontend opt-in before any guest creation.
  av2_launch_phase("main");
  av2_boot_log("main","entered argc=%d",argc);
  // Audit relocated imports before logging/runtime libraries can invoke them.
  if(!av2_check_native_imports())return 39;
  exbox_prepare_optional_modules(optional_module_log);
  av2_boot_install_signals();
  std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
  // Provisional bring-up paths; preserve the dashboard's existing text save.
  std::filesystem::path assets;
  const std::filesystem::path state="/data/nxe/avatar-editor";
  av2::native::NativePad* pad=nullptr;
  rex::Runtime* runtime=nullptr;
  rex::system::object_ref<rex::system::XThread> guest;
  bool renderer=false, guest_started=false;
  av2::diagnostics::HangMonitor watchdog;
  auto finish=[&](int status) {
    av2_boot_log("teardown","watchdog join begin");
    watchdog.stop();
    if(!av2_native_pulse_stop())av2_exit_with_live_guests(status);
    av2_boot_log("teardown","watchdog joined; preserve launched guests on stop/error");
    // The registry is populated after a thread starts executing. An empty
    // snapshot cannot prove a newly launched guest is safe to destroy.
    if(guest_started)
      av2_exit_with_live_guests(status);
    av2_boot_log("teardown","no outstanding launched title; ordinary cleanup permitted");
    av2_boot_log("teardown","begin status=%d renderer=%d guest=%d",status,int(renderer),int(guest_started));
    guest.reset();
    if(renderer){ae::gpu::Shutdown();renderer=false;}
    if((av2_boot_gpu_result()!=0 && av2_boot_gpu_result()!=-4)) {
      // A timeout does not prove the GPU stopped reading its allocations.
      // Keep ownership instead of freeing in-flight buffers or relaunching.
      av2_boot_log("teardown","GPU completion unknown after bounded wait; retaining process/resources, no relaunch");
      for(;;)::sleep(1);
    }
    av2_early_vulkan_release();
    delete runtime;runtime=nullptr;
    delete pad;pad=nullptr;
    av2_boot_log("teardown","GPU/runtime/pad released; shell exit next");
    return status;
  };
  try {
    av2::platform::set_asset_index_logger(av2_boot_log);
    assets=([&]{Av2HitchLaunchScope scope("host.select_asset_root");return select_asset_root(argc,argv);}());
    av2_boot_startup_probe(assets.string().c_str());
    if(av2_boot_should_stop())return finish(0);
    rex::LogConfig logging;logging.log_to_console=true;logging.default_level=spdlog::level::info;rex::InitLogging(logging);
    if(!([&]{Av2HitchLaunchScope scope("host.vfs_preflight");return native_vfs_preflight(assets);}()))return finish(40);
    std::filesystem::create_directories(state);
    const std::filesystem::path shared="/data/nxe/avatar/editor";
    av2::native::configure_avatar_save_store(shared);
    av2::native::prepare_dashboard_return();
    rex::cvar::SetFlagByName("avatar_data_dir",shared.string());
    // Debug environment must precede Mesa's first instance/physical-device init.
    // Diagnostic facilities must never prevent GPU bring-up in a restricted title.
    errno=0; const int debug_radv=setenv("RADV_DEBUG","startup",1); const int radv_errno=errno;
    errno=0; const int debug_mesa=setenv("MESA_DEBUG","1",1); const int mesa_errno=errno;
    av2_boot_log("vulkan-early","RADV_DEBUG rc=%d errno=%d MESA_DEBUG rc=%d errno=%d",debug_radv,radv_errno,debug_mesa,mesa_errno);
    if(!av2_boot_capture_stdio()) {
      errno=0; const int file_rc=setenv("MESA_LOG_FILE","/data/nxe/avatar-editor/mesa.log",1); const int file_errno=errno;
      av2_boot_log("stdio","redirect unavailable; MESA_LOG_FILE rc=%d errno=%d; continuing",file_rc,file_errno);
    }
    if(!([&]{Av2HitchLaunchScope scope("host.early_vulkan");return av2_early_vulkan_prepare();}()))return finish(33);
    renderer=([&]{Av2HitchLaunchScope scope("host.loading_presenter");return ae::gpu::InitializeLoading();}());
    if(!renderer)return finish(av2_boot_gpu_result()?38:av2_boot_should_stop()?0:33);
    if(av2_boot_should_stop())return finish(0);
    pad=new av2::native::NativePad;
    if(!pad->initialize()) return finish(30);
    rex::cvar::SetFlagByName("game_data_root",assets.string());
    rex::cvar::SetFlagByName("gpu_shader_pack",(assets/"ae.pack").string());
    rex::cvar::SetFlagByName("gpu_dump_every","0");
    runtime=new rex::Runtime(assets,state/"user",{},state/"cache",assets);
    rex::RuntimeConfig config;config.tool_mode=true;
    config.kernel_init=av2::initialize_title_environment;
    config.input_factory=[pad](bool){return pad->make_input_system();};
    config.audio_factory=av2::native::create_silent_audio;
    av2_boot_log("runtime","Setup begin");
    auto status=([&]{Av2HitchLaunchScope scope("host.runtime_setup");return runtime->Setup(PPCImageConfig,std::move(config));}());
    std::fprintf(stderr,"AV2 native Setup=%08x\n",status);
    av2_boot_log("runtime","Setup result=%08x",status);
    if(status)return finish(av2_boot_should_stop()?0:31);
    if(av2_boot_phase("runtime"))return finish(0);
    status=([&]{Av2HitchLaunchScope scope("host.load_xex");return runtime->LoadXexImage("game:\\avatareditor.xex");}());
    std::fprintf(stderr,"AV2 native LoadXexImage=%08x\n",status);
    av2_boot_log("xex","LoadXexImage result=%08x",status);
    if(status)return finish(32);
    if(av2_boot_phase("xex"))return finish(0);
    if(!([&]{Av2HitchLaunchScope scope("host.title_pages");return av2::prepare_title_system_pages(*runtime);}()))return finish(32);
    if(av2_boot_phase("assets"))return finish(0);
    av2_boot_vfs_trace_end();
    renderer=([&]{Av2HitchLaunchScope scope("host.renderer_init");return ae::gpu::Initialize(nullptr);}());
    if(!renderer)return finish(av2_boot_gpu_result()?38:av2_boot_should_stop()?0:33);
    if(av2_boot_should_stop())return finish(0);
    if(!av2_native_pulse_start(runtime))return finish(38);
    if(!watchdog.start(av2::diagnostics::gpu_snapshot,
      [](av2::diagnostics::Snapshot s,bool hang){av2::diagnostics::report_gpu(s,hang);av2_diag_threads_report();},
      []{diagnostic_hang=true;av2_boot_request_stop("20s without presentation; cooperative teardown");}))
      av2_boot_log("watchdog","thread creation failed; continuing without watchdog");
    guest=([&]{Av2HitchLaunchScope scope("host.launch_module");return runtime->LaunchModule();}());
    if(!guest)return finish(34);
    guest_started=true;
    // OS/controller work stays on the frontend thread. Guest reads snapshots.
    for(;;) {
      if(av2_boot_should_stop())break;
      if(av2_normal_exit_requested())break;
      pad->poll();
      const auto wait=av2_native_poll_guest_completion(guest.get(),std::chrono::milliseconds(4));
      if(wait==rex::thread::WaitResult::kSuccess) break;
      if(wait!=rex::thread::WaitResult::kTimeout)return finish(37);
    }
    if(av2_boot_should_stop())return finish(diagnostic_hang.load()?42:0);
    if(av2_normal_exit_requested())
      av2_boot_log("exit", "normal handoff route=%s status=%d", av2_normal_exit_route_name(), av2_normal_exit_status());
    // P8 normal completion/quiescence: drain and publish; parked owners remain retained for Q4.
    struct NormalExitOps {
      av2::diagnostics::HangMonitor& watchdog;
      rex::Runtime* runtime;
      bool& guest_started;
      void stop_watchdog(){watchdog.stop();}
      void terminate_guest(){av2_native_close_guest_admission();runtime->kernel_state()->TerminateTitle();}
      bool drain_guest(){return av2::native::drain_guest_threads(std::chrono::seconds(15));}
      void guest_drained(){guest_started=false;}
      bool gpu_completion_known(){const int result=av2_boot_gpu_result();return result==0 || result==-4;}
      void finalize_save_and_ticket(){
        const bool saved=av2::native::avatar_was_saved();
        const bool committed=av2::native::finalize_avatar_save();
        av2::native::set_dashboard_exit_result(!committed?av2::native::ExitResult::failed:
          (saved?av2::native::ExitResult::saved:av2::native::ExitResult::cancelled));
      }
      void flush_pipeline_cache(){av2_flush_pipeline_cache();}
      void flush_logs(){rex::FlushLogging();}
    } exit_ops{watchdog,runtime,guest_started};
    if(!av2_native_pulse_stop())return finish(35);
    const auto decision=av2::native::prepare_clean_exit(exit_ops);
    if(decision==av2::native::CleanExitDecision::drain_timeout)return finish(35);
    if(decision==av2::native::CleanExitDecision::gpu_completion_unknown){
      av2_boot_log("teardown","GPU completion unknown; retaining process/resources, no relaunch");
      for(;;)::sleep(1);
    }
    av2_boot_log("exit","guest drained/quiescent; save/ticket/cache finalized; owners retained for Q4 exit");
    catchReturnFromMain(0);
  } catch(const std::exception& error) {
    std::fprintf(stderr,"AV2 native frontend exception: %s; renderer=%d\n",error.what(),int(renderer));
    // Runtime ownership intentionally stays with the process on incomplete
    // startup/guest errors. No destructor races a potentially live guest.
    av2_boot_log("exception","%s",error.what());
    return finish(36);
  }
}
