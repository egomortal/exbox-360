#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
extern "C" void exbox_prepare_optional_modules(void(*)(const char*,...));
extern "C" int __wrap_sceKeyboardInit();
extern "C" int __wrap_sceKeyboardOpen(int,int,int,void*);
extern "C" int __wrap_sceKeyboardReadState(int,void*);
extern "C" int __wrap_sceKeyboardClose(int);
extern "C" int __wrap_sceImeDialogInit(const void*,void*);
extern "C" int __wrap_sceImeDialogGetResult(void*);
extern "C" int __wrap_sceImeDialogTerm();
extern "C" int __wrap_sceImeDialogGetStatus();
static bool present=false;static int called=0;
static int fake_init(){++called;return 23;}
static void log(const char*,...){}
extern "C" int sceSysmoduleLoadModule(uint16_t id){assert(id==0x106);return -1;}
extern "C" int sceKernelGetModuleList(int* h,size_t n,size_t* count){assert(n==512);*count=1;h[0]=42;return 0;}
extern "C" int sceKernelDlsym(int h,const char* name,void** out){assert(h==42);*out=nullptr;if(present&&!strcmp(name,"sceKeyboardInit"))*out=reinterpret_cast<void*>(&fake_init);return *out?0:-1;}
int main(){
 exbox_prepare_optional_modules(log);
 assert(__wrap_sceKeyboardInit()==-1);assert(__wrap_sceKeyboardOpen(1,0,0,nullptr)==-1);assert(__wrap_sceKeyboardReadState(1,nullptr)==-1);assert(__wrap_sceKeyboardClose(1)==-1);
 assert(__wrap_sceImeDialogInit(nullptr,nullptr)==-1);assert(__wrap_sceImeDialogGetResult(nullptr)==-1);assert(__wrap_sceImeDialogTerm()==-1);assert(__wrap_sceImeDialogGetStatus()==0);assert(called==0);
 present=true;exbox_prepare_optional_modules(log);assert(__wrap_sceKeyboardInit()==23&&called==1);
}
