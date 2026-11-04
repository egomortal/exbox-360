// Ex-Box addition: optional native-title imports. The installed SDL archive is
// untouched; only the native probe links these guarded import wrappers.
#include <cstddef>
#include <cstdint>
#include <cstdio>
extern "C" int sceSysmoduleLoadModule(uint16_t);
extern "C" int sceKernelGetModuleList(int*,size_t,size_t*);
extern "C" int sceKernelDlsym(int,const char*,void**);
namespace {
using Log=void(*)(const char*,...);
Log log_line=nullptr;
void* symbols[8]{};
const char* names[]={"sceKeyboardInit","sceKeyboardOpen","sceKeyboardReadState","sceKeyboardClose","sceImeDialogInit","sceImeDialogGetResult","sceImeDialogTerm","sceImeDialogGetStatus"};
void* lookup(const char* name) {
  auto volatile list=&sceKernelGetModuleList;auto volatile resolve=&sceKernelDlsym;
  if(!list||!resolve){log_line("module lookup imports missing; optional calls disabled\n");return nullptr;}
  int handles[512]{};size_t count=0;
  const int rc=list(handles,512,&count);
  if(rc || count>512){log_line("module list: rc=%08x count=%zu\n",unsigned(rc),count);return nullptr;}
  for(size_t i=0;i<count;++i){void* result=nullptr;if(!resolve(handles[i],name,&result)&&result){log_line("optional %s: resolved module=%d\n",name,handles[i]);return result;}}
  log_line("optional %s: unavailable, calls guarded\n",name);return nullptr;
}
}
extern "C" void exbox_prepare_optional_modules(Log output) {
  log_line=output;
  // Public PS5 keyboard module ID, confirmed in the native Ruffle input probe.
  auto volatile load=&sceSysmoduleLoadModule;
  if(load)log_line("keyboard public sysmodule 0x0106: rc=%08x\n",unsigned(load(0x106)));
  else log_line("keyboard sysmodule loader unavailable; keyboard disabled\n");
  for(size_t i=0;i<8;++i)symbols[i]=lookup(names[i]);
  // The SDK places this export in libSceSystemService, not libSceLncUtil.
  (void)lookup("sceLncUtilLaunchApp"); // Resolution only: never launch from probe.
}
extern "C" int __wrap_sceKeyboardInit(){auto f=reinterpret_cast<int(*)()>(symbols[0]);return f?f():-1;}
extern "C" int __wrap_sceKeyboardOpen(int u,int t,int i,void* p){auto f=reinterpret_cast<int(*)(int,int,int,void*)>(symbols[1]);return f?f(u,t,i,p):-1;}
extern "C" int __wrap_sceKeyboardReadState(int h,void* p){auto f=reinterpret_cast<int(*)(int,void*)>(symbols[2]);return f?f(h,p):-1;}
extern "C" int __wrap_sceKeyboardClose(int h){auto f=reinterpret_cast<int(*)(int)>(symbols[3]);return f?f(h):-1;}
extern "C" int __wrap_sceImeDialogInit(const void* p,void* o){auto f=reinterpret_cast<int(*)(const void*,void*)>(symbols[4]);return f?f(p,o):-1;}
extern "C" int __wrap_sceImeDialogGetResult(void* p){auto f=reinterpret_cast<int(*)(void*)>(symbols[5]);return f?f(p):-1;}
extern "C" int __wrap_sceImeDialogTerm(){auto f=reinterpret_cast<int(*)()>(symbols[6]);return f?f():-1;}
extern "C" int __wrap_sceImeDialogGetStatus(){auto f=reinterpret_cast<int(*)()>(symbols[7]);return f?f():0;}
