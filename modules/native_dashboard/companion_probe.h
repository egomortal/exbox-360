/* Ex-Box addition: Q8d diagnostic only. No filesystem feed or launch commands.
 * The existing daemon remains responsible for all original commands. */
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
static int probe_socket=-1;
static char probe_socket_path[104],probe_spool_path[1024];
static int probe_transport_init(const char *socket_path,const char *spool_path){
    if(strlen(socket_path)>=sizeof probe_socket_path||strlen(spool_path)>=sizeof probe_spool_path)return -1;
    strcpy(probe_socket_path,socket_path);strcpy(probe_spool_path,spool_path);
    if(mkdir(spool_path,01777)&&errno!=EEXIST)return -1;
    struct stat st;if(lstat(spool_path,&st)||!S_ISDIR(st.st_mode))return -1;
    if(chmod(spool_path,01777))return -1;
    /* Do not unlink a pre-existing endpoint: another companion may own it. */
    probe_socket=socket(AF_UNIX,SOCK_STREAM,0);if(probe_socket<0)return -1;
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;strcpy(address.sun_path,socket_path);
    if(bind(probe_socket,(struct sockaddr*)&address,sizeof address)||listen(probe_socket,4)){close(probe_socket);probe_socket=-1;return -1;}
    (void)chmod(socket_path,0666);return 0;
}
static void probe_spool_poll(void){
    int root=open(probe_spool_path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(root<0)return;
    DIR *dir=fdopendir(root);if(!dir){close(root);return;}
    struct dirent *ent;unsigned handled=0;
    while(handled<32&&(ent=readdir(dir))){
        const char *name=ent->d_name;if(strlen(name)!=16||strncmp(name,"probe-",6)||strcmp(name+12,".req"))continue;
        int safe=1;for(int i=6;i<12;i++)if(!((name[i]>='A'&&name[i]<='Z')||(name[i]>='a'&&name[i]<='z')||(name[i]>='0'&&name[i]<='9')))safe=0;
        if(!safe)continue;
        ++handled;int input=openat(root,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(input<0)continue;
        char request[64]={0};struct stat st;int valid=!fstat(input,&st)&&S_ISREG(st.st_mode)&&st.st_size==14&&read(input,request,sizeof request)==14&&!memcmp(request,"EXBOX-PROBE-1\n",14);close(input);
        if(!valid)continue;
        char response[32],temporary[48];snprintf(response,sizeof response,"%.12s.rsp",name);snprintf(temporary,sizeof temporary,".%.12s.%ld.tmp",name,(long)getpid());
        int output=openat(root,temporary,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0666);if(output<0)continue;
        const char bytes[]="EXBOX-PROBE-1 OK\n";int ok=(geteuid()!=0||fchown(output,st.st_uid,st.st_gid)==0)&&write(output,bytes,sizeof bytes-1)==sizeof bytes-1&&fsync(output)==0;close(output);
        if(ok&&!renameat(root,temporary,root,response)){(void)fsync(root);(void)unlinkat(root,name,0);}else(void)unlinkat(root,temporary,0);
    }
    closedir(dir);
}
static void probe_transport_poll(void){
    probe_spool_poll();if(probe_socket<0)return;
    fd_set set;FD_ZERO(&set);FD_SET(probe_socket,&set);struct timeval zero={0,0};if(select(probe_socket+1,&set,NULL,NULL,&zero)<=0)return;
    int client=accept(probe_socket,NULL,NULL);if(client<0)return;
    struct timeval timeout={1,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
    char request[256]={0};ssize_t n=recv(client,request,sizeof request-1,0);
    const char *response=n>=14&&!strncmp(request,"GET / HTTP/1.",13)?"HTTP/1.0 200 OK\r\nContent-Length: 17\r\nConnection: close\r\n\r\nEXBOX-PROBE-1 OK\n":"HTTP/1.0 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    (void)send(client,response,strlen(response),MSG_NOSIGNAL);close(client);
}
static void probe_transport_close(void){if(probe_socket>=0){close(probe_socket);probe_socket=-1;(void)unlink(probe_socket_path);}}
