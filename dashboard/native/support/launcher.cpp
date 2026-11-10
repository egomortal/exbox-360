// SPDX-License-Identifier: GPL-3.0-or-later
// Native SDL dashboard entry. No diagnostic resource probes or video owner.
#include "system_data.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
extern "C" int sceSystemServiceLoadExec(const char*,const char* const*);
extern "C" void exbox_prepare_optional_modules(void(*)(const char*,...));
extern "C" bool exbox_check_native_imports();
int exbox_dashboard_main();
extern "C" void exbox_native_note(const char* format,...) {
 char line[1024];va_list args;va_start(args,format);std::vsnprintf(line,sizeof(line),format,args);va_end(args);
 const bool newline=!line[0]||line[std::strlen(line)-1]!=char(10);
 // Release diagnostics are best-effort; never fsync a frame or log line.
 if(FILE* file=std::fopen("/app0/exbox-native.log","a")){
  std::fputs(line,file);if(newline)std::fputc(10,file);std::fflush(file);std::fclose(file);
 }else{std::fputs(line,stderr);if(newline)std::fputc(10,stderr);std::fflush(stderr);}
}
extern "C" [[noreturn]] void catchReturnFromMain(int status) {
 exbox_native_note("native exit status=%d; shell LoadExec",status);
 const int rc=sceSystemServiceLoadExec("exit",nullptr);
 exbox_native_note("shell LoadExec returned rc=%08x errno=%d",unsigned(rc),errno);
 for(;;)sleep(1);
}
int main(){
 exbox_native_note("Ex-Box 360 native full candidate: SDL display owner");
 if(!exbox_check_native_imports())catchReturnFromMain(1);
 exbox_prepare_optional_modules(exbox_native_note);
 sysdata::initialize();
 const int result=exbox_dashboard_main();
 exbox_native_note("dashboard returned status=%d errno=%d",result,errno);
 return result;
}