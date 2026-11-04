// Ex-Box addition: native launch-context probe. No avatar/rendering changes.
// Memory method: docs/PS5-HOMEBREW-PLATFORM-NOTES.md section 1.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <dirent.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <chrono>
#include "path_inventory.h"
#include "../../system_data.hpp"
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance,const char*);
extern "C" int sceSystemServiceLoadExec(const char*,const char* const*);
extern "C" int sceKernelConfiguredFlexibleMemorySize(size_t*);
extern "C" int sceKernelAvailableFlexibleMemorySize(size_t*);
extern "C" int sceKernelMapNamedFlexibleMemory(void**,size_t,int,int,const char*);
extern "C" int sceKernelMunmap(void*,size_t);
extern "C" int64_t sceKernelGetDirectMemorySize();
extern "C" int sceKernelAvailableDirectMemorySize(int64_t,int64_t,size_t,int64_t*,size_t*);
int exbox_dashboard_main();
extern "C" void exbox_prepare_optional_modules(void(*)(const char*,...));
namespace {
FILE* report=nullptr;
void note(const char* format,...) {
  char line[1024];va_list args;va_start(args,format);std::vsnprintf(line,sizeof(line),format,args);va_end(args);
  std::fputs(line,stderr);std::fflush(stderr);
  if(report){std::fputs(line,report);std::fflush(report);}
}
template<class T> T symbol(void* module,const char* name) {
  errno=0;void* address=module?dlsym(module,name):nullptr;
  note("symbol %s: %s errno=%d\n",name,address?"ok":"fail",errno);
  return reinterpret_cast<T>(address);
}
void memory_probe() {
  note("kernel memory query method: direct native SDK libkernel imports (no dlsym)\n");
  using Size=int(*)(size_t*);
  const Size queries[]={&sceKernelConfiguredFlexibleMemorySize,&sceKernelAvailableFlexibleMemorySize};
  const char* names[]={"sceKernelConfiguredFlexibleMemorySize","sceKernelAvailableFlexibleMemorySize"};
  for(unsigned i=0;i<2;++i) {
    size_t size=0;errno=0;const int rc=queries[i](&size);
    note("%s: %s rc=%08x errno=%d bytes=%zu (query only)\n",names[i],rc?"fail":"ok",unsigned(rc),errno,size);
  }
  auto map=&sceKernelMapNamedFlexibleMemory;
  auto unmap=&sceKernelMunmap;
  // No heap allocation while testing exhaustion. Release every owned mapping
  // before opening pad/audio/video/modules. The safety cap is reported as a
  // lower bound, never silently presented as the true allocation limit.
  constexpr size_t chunk=1u<<20,limit=8192;
  static void* mappings[limit]{};
  if(map && unmap) {
    size_t count=0;int rc=0,last_errno=0;
    for(;count<limit;++count) {
      errno=0;rc=map(&mappings[count],chunk,3,0,"exbox-native-budget");last_errno=errno;
      if(rc!=0) break;
    }
    note("flexible true-budget probe: %s bytes=%zu chunks=%zu rc=%08x errno=%d\n",count==limit?"capped lower bound":"exhaustion measured",count*chunk,count,unsigned(rc),last_errno);
    size_t failures=0;
    while(count) {--count;errno=0;const int result=unmap(mappings[count],chunk);if(result){++failures;note("flexible unmap: fail rc=%08x errno=%d\n",unsigned(result),errno);}mappings[count]=nullptr;}
    note("flexible cleanup: %s failures=%zu\n",failures?"fail":"ok",failures);
  }
  auto total=&sceKernelGetDirectMemorySize;
  auto available=&sceKernelAvailableDirectMemorySize;
  if(total && available) {
    const int64_t size=total();int64_t start=0;size_t span=0;errno=0;
    const int rc=size>0?available(0,size,0x10000,&start,&span):-1;
    note("direct memory: %s total=%lld largest_available_span=%zu start=%lld rc=%08x errno=%d\n",rc?"fail":"ok",static_cast<long long>(size),span,static_cast<long long>(start),unsigned(rc),errno);
  }

}
void file_probe() {
  for(const char* path:{"/user/appmeta","/system_data/priv/appmeta","/system_data/priv/cache/profile","/user/av_contents/photo","/data/exbox","/data/nxe","/data/homebrew/ExBox360","/mnt/usb0"}) {
    errno=0;DIR* directory=opendir(path);const int opened_errno=errno;
    if(!directory){note("read %s: fail errno=%d\n",path,opened_errno);continue;}
    errno=0;const auto* entry=readdir(directory);const int read_errno=errno;
    note("read %s: %s errno=%d empty=%d\n",path,entry||read_errno==0?"ok":"fail",read_errno,int(!entry));closedir(directory);
  }
  for(const char* path:native_source_paths) {
    errno=0;const int fd=open(path,O_RDONLY|O_NONBLOCK);
    if(fd<0){note("source path %s: fail errno=%d\n",path,errno);continue;}
    struct stat info{};const int rc=fstat(fd,&info);
    if(rc!=0){note("source path %s: fail stat errno=%d\n",path,errno);close(fd);continue;}
    if(S_ISDIR(info.st_mode)) {
      close(fd);errno=0;DIR* dir=opendir(path);
      if(!dir){note("source path %s: fail directory errno=%d\n",path,errno);continue;}
      errno=0;const auto* entry=readdir(dir);const int error=errno;
      note("source path %s: %s directory errno=%d\n",path,entry||error==0?"ok":"fail",error);closedir(dir);
    } else if(S_ISREG(info.st_mode)) {
      char byte;errno=0;const auto read_count=read(fd,&byte,1);
      note("source path %s: %s regular-file errno=%d\n",path,read_count>=0?"ok":"fail",errno);close(fd);
    } else {note("source path %s: skipped nonregular object\n",path);close(fd);}
  }
  // Only create a unique probe file in the existing application data folder.
  // Never truncate a save or infer write permission from read permission.
  char path[]="/data/nxe/.native-probe-XXXXXX";errno=0;int fd=mkstemp(path);
  if(fd<0){note("data write: fail errno=%d\n",errno);return;}
  const char bytes[]="Ex-Box native probe\n";errno=0;
  const auto count=write(fd,bytes,sizeof(bytes)-1);const int write_errno=errno;
  const int sync=fsync(fd),sync_errno=errno;close(fd);const int removed=unlink(path);
  note("data write: %s bytes=%lld errno=%d fsync=%d fsync_errno=%d cleanup=%d\n",count==sizeof(bytes)-1?"ok":"fail",static_cast<long long>(count),write_errno,sync,sync_errno,removed);
  void* launch=dlopen("libSceLncUtil.sprx",RTLD_LAZY);
  (void)symbol<void*>(launch,"sceLncUtilLaunchApp"); // Resolution only. Never call.
  if(launch) dlclose(launch);
}
extern "C" void exbox_probe_alternate_transports(void(*)(const char*,...));
[[maybe_unused]] void companion_probe() {
  struct Socket {int fd;~Socket(){if(fd>=0)close(fd);}} socket{::socket(AF_INET,SOCK_STREAM,0)};
  if(socket.fd<0){note("companion TCP: fail socket errno=%d\n",errno);return;}
  const int flags=fcntl(socket.fd,F_GETFL,0);
  if(flags<0){note("companion TCP: fail F_GETFL errno=%d\n",errno);return;}
  if(fcntl(socket.fd,F_SETFL,flags|O_NONBLOCK)<0){note("companion TCP: fail F_SETFL errno=%d\n",errno);return;}
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  auto wait=[&](short events){
    for(;;){
      const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
      if(left<=0){errno=ETIMEDOUT;return false;}
      pollfd fd{socket.fd,events,0};const int result=poll(&fd,1,static_cast<int>(left));
      if(result<0 && errno==EINTR)continue;
      if(result<=0){if(result==0)errno=ETIMEDOUT;return false;}
      return true;
    }
  };
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(7090);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  const int connected=connect(socket.fd,reinterpret_cast<sockaddr*>(&address),sizeof(address));
  if(connected<0 && errno!=EINPROGRESS){note("companion TCP: fail connect errno=%d\n",errno);return;}
  if(connected<0){
    if(!wait(POLLOUT)){note("companion TCP: fail timeout errno=%d\n",errno);return;}
    int error=0;socklen_t size=sizeof(error);
    if(getsockopt(socket.fd,SOL_SOCKET,SO_ERROR,&error,&size)<0 || error){note("companion TCP: fail SO_ERROR=%d errno=%d\n",error,errno);return;}
  }
  note("companion TCP 127.0.0.1:7090: ok\n");
  constexpr char request[]="GET / HTTP/1.0\r\nHost: 127.0.0.1:7090\r\nConnection: close\r\n\r\n";
  size_t sent=0;
  while(sent<sizeof(request)-1){
    if(!wait(POLLOUT)){note("companion GET /: fail send timeout errno=%d\n",errno);return;}
    const auto count=send(socket.fd,request+sent,sizeof(request)-1-sent,MSG_NOSIGNAL);
    if(count<0 && (errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
    if(count<=0){note("companion GET /: fail send errno=%d\n",errno);return;}sent+=static_cast<size_t>(count);
  }
  char response[256]{};size_t received=0;
  while(received<sizeof(response)-1){
    if(!wait(POLLIN)){note("companion GET /: fail receive timeout errno=%d\n",errno);return;}
    const auto count=recv(socket.fd,response+received,sizeof(response)-1-received,0);
    if(count<0 && (errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
    if(count<=0){note("companion GET /: fail receive errno=%d\n",errno);return;}received+=static_cast<size_t>(count);
    if(char* end=std::strstr(response,"\r\n")){*end=0;note("companion GET / status: %s (no /launch requested)\n",response);return;}
  }
  note("companion GET /: fail oversized status line\n");
}
void sdl_probe() {
  exbox_prepare_optional_modules(note);
  errno=0;const int initialized=SDL_Init(0);
  note("SDL init: %s errno=%d detail=%s\n",initialized?"fail":"ok",errno,SDL_GetError());
  // Continue independent checks even when one subsystem fails.
  errno=0;const int pad_init=SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
  note("pad init: %s errno=%d detail=%s\n",pad_init?"fail":"ok",errno,SDL_GetError());
  SDL_GameController* controller=nullptr;
  for(int i=0;i<SDL_NumJoysticks();++i) if(SDL_IsGameController(i)){controller=SDL_GameControllerOpen(i);if(controller)break;}
  note("pad open: %s errno=%d detail=%s\n",controller?"ok":"fail",errno,SDL_GetError());
  if(controller) SDL_GameControllerClose(controller);
  errno=0;const int audio_init=SDL_InitSubSystem(SDL_INIT_AUDIO);
  note("audio init: %s errno=%d detail=%s\n",audio_init?"fail":"ok",errno,SDL_GetError());
  SDL_AudioSpec want{},have{};want.freq=48000;want.format=AUDIO_S16SYS;want.channels=2;want.samples=1024;
  errno=0;const auto audio=SDL_OpenAudioDevice(nullptr,0,&want,&have,0);
  note("audio open (paused, no sound): %s errno=%d detail=%s\n",audio?"ok":"fail",errno,SDL_GetError());
  if(audio) SDL_CloseAudioDevice(audio);
  errno=0;const int video_init=SDL_InitSubSystem(SDL_INIT_VIDEO);
  note("video init: %s errno=%d detail=%s\n",video_init?"fail":"ok",errno,SDL_GetError());
  errno=0;auto* window=SDL_CreateWindow("Ex-Box native context test",0,0,1280,720,SDL_WINDOW_SHOWN);
  auto* surface=window?SDL_GetWindowSurface(window):nullptr;
  note("VideoOut via SDL: %s errno=%d driver=%s detail=%s\n",surface?"ok":"fail",errno,SDL_GetCurrentVideoDriver()?SDL_GetCurrentVideoDriver():"none",SDL_GetError());
  if(window) SDL_DestroyWindow(window);
  note("identity after SDL init: uid=%u euid=%u errno=%d\n",unsigned(getuid()),unsigned(geteuid()),errno);
  SDL_Quit();
}
void vulkan_probe() {
  auto create=reinterpret_cast<PFN_vkCreateInstance>(vk_icdGetInstanceProcAddr(nullptr,"vkCreateInstance"));
  if(!create){note("Vulkan create symbol: fail errno=%d\n",errno);return;}
  VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_1;app.pApplicationName="Ex-Box native probe";
  VkInstanceCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;info.pApplicationInfo=&app;
  VkInstance instance=nullptr;errno=0;auto rc=create(&info,nullptr,&instance);
  note("Vulkan instance: %s rc=%d errno=%d\n",rc==VK_SUCCESS?"ok":"fail",int(rc),errno);
  if(rc!=VK_SUCCESS) return;
  auto destroy=reinterpret_cast<PFN_vkDestroyInstance>(vk_icdGetInstanceProcAddr(instance,"vkDestroyInstance"));
  auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(vk_icdGetInstanceProcAddr(instance,"vkEnumeratePhysicalDevices"));
  auto families=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(vk_icdGetInstanceProcAddr(instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
  auto make=reinterpret_cast<PFN_vkCreateDevice>(vk_icdGetInstanceProcAddr(instance,"vkCreateDevice"));
  auto get=reinterpret_cast<PFN_vkGetDeviceProcAddr>(vk_icdGetInstanceProcAddr(instance,"vkGetDeviceProcAddr"));
  if(enumerate && families && make && get) {
    VkPhysicalDevice devices[8]{};uint32_t count=8;rc=enumerate(instance,&count,devices);
    if((rc==VK_SUCCESS||rc==VK_INCOMPLETE) && count) {
      VkQueueFamilyProperties queues[32]{};uint32_t n=32;families(devices[0],&n,queues);
      bool attempted=false;
      for(uint32_t i=0;i<n && i<32;++i) if(queues[i].queueCount && (queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)) {
        attempted=true;float priority=1;VkDeviceQueueCreateInfo queue{};queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue.queueFamilyIndex=i;queue.queueCount=1;queue.pQueuePriorities=&priority;
        VkDeviceCreateInfo device_info{};device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue;
        VkDevice device=nullptr;errno=0;rc=make(devices[0],&device_info,nullptr,&device);
        note("RADV device: %s rc=%d errno=%d\n",rc==VK_SUCCESS?"ok":"fail",int(rc),errno);
        if(rc==VK_SUCCESS){auto drop=reinterpret_cast<PFN_vkDestroyDevice>(get(device,"vkDestroyDevice"));if(drop){drop(device,nullptr);note("RADV destroy device: ok errno=%d\n",errno);}else note("RADV destroy device: fail missing symbol\n");}break;
      }
      if(!attempted) note("RADV queue selection: fail no graphics queue\n");
    } else note("RADV physical device: fail rc=%d count=%u\n",int(rc),count);
  } else note("Vulkan instance entrypoints: fail missing symbol\n");
  if(destroy){destroy(instance,nullptr);note("Vulkan destroy instance: ok errno=%d\n",errno);}
  else note("Vulkan destroy instance: fail missing symbol\n");
}
}
extern "C" [[noreturn]] void catchReturnFromMain(int status) {
  note("native exit: status=%d; Q4 shell LoadExec\n",status);
  const int rc=sceSystemServiceLoadExec("exit",nullptr);note("LoadExec returned %08x errno=%d\n",unsigned(rc),errno);
  for(;;) sleep(1);
}
int main() {
  report=std::fopen("/app0/exbox-native-probe.txt","w");
  note("Ex-Box native probe: log=%s uid=%u euid=%u errno=%d\n",report?"ok":"stderr only",unsigned(getuid()),unsigned(geteuid()),errno);
  memory_probe();file_probe();sdl_probe();vulkan_probe();
  note("self-test complete; starting unchanged SDL dashboard\n");
  sysdata::initialize();
  const int result=exbox_dashboard_main();
  note("dashboard main returned early: rc=%d SDL/TTF reason=%s errno=%d\n",result,SDL_GetError(),errno);
  return result;
}
