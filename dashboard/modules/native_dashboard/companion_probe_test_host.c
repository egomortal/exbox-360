#define _GNU_SOURCE
#include "companion_probe.h"
int main(int argc,char**argv){if(argc!=3)return 2;if(probe_transport_init(argv[1],argv[2]))return 3;for(int i=0;i<150;i++){probe_transport_poll();usleep(20000);}probe_transport_close();return 0;}
