from pathlib import Path
import subprocess,json
r=Path(__file__).resolve().parents[1];out=r/'private/native-dashboard/feed-tests'
prefix=r'''#define _GNU_SOURCE
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
typedef struct {char title[128];char path[1024];} App;
static App apps[2];static int app_count,suspended,auto_launch_pending,return_from_game_armed,launch_requested,auto_attempted;static time_t handoff_until;
static int launches;
static void scan_apps(void){app_count=1;strcpy(apps[0].title,"Demo");strcpy(apps[0].path,"/data/homebrew/Demo/eboot.elf");}
static int write_all(int fd,const char* p,size_t n){return send(fd,p,n,0)==(ssize_t)n?0:-1;}
static void reply(int fd,const char* p){assert(!write_all(fd,p,strlen(p)));}
static void launch_homebrew(const char* p){assert(!strcmp(p,"/data/homebrew/Demo/eboot.elf"));++launches;}
void exbox_feed_dispatch(int fd,const char* p){(void)fd;(void)p;}
int exbox_feed_title_installed(const char* p){(void)p;return 0;}
int exbox_feed_launch_title(const char* p){(void)p;return -1;}
void log_line(const char* p,...){(void)p;}
'''
suffix=r'''
static void check(const char* command,const char* expected){int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(send(pair[0],command,strlen(command),0)==(ssize_t)strlen(command));handle_client(pair[1]);close(pair[1]);char response[4096]={0};ssize_t n=recv(pair[0],response,sizeof(response)-1,0);assert(n>=0);assert(!strcmp(response,expected));close(pair[0]);}
int main(void){check("LIST\n","OK\nDemo\t/data/homebrew/Demo/eboot.elf\n.\n");check("SUSPEND\n","OK\n");assert(suspended==1);check("GET /launch HTTP/1.0\r\n\r\n","HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");assert(launch_requested==1);check("LAUNCH\t/data/homebrew/Demo/eboot.elf\n","OK\n");assert(launches==1);check("LAUNCH\t/not-approved\n","ERR\n");check("unknown\n","ERR\n");(void)auto_attempted;(void)handoff_until;}
'''
for label,path in [('original',r.parent/'native-daemon/main.c'),('feed',r/'private/native-dashboard/companion-q8c/main.c')]:
 text=path.read_text();function=text[text.index('static void handle_client('):text.index('\nint main(void)')]
 source=out/f'legacy-{label}.c';source.write_text(prefix+function+suffix)
 binary=out/f'legacy-{label}'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True);subprocess.run([str(binary)],check=True,timeout=5)
(out/'legacy-compatibility.json').write_text(json.dumps({'original_and_feed':'passed','commands':['LIST','SUSPEND','GET /launch','LAUNCH allowed homebrew','LAUNCH rejected path','unknown'],'console_launches':False},indent=2)+'\n')
print('Original and feed legacy command handlers: byte-identical replies for six cases')
