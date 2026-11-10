// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include <strings.h>
char* __wrap_strcasestr(const char* text,const char* needle) {
 const size_t length=strlen(needle);
 if(!length)return (char*)text;
 for(;*text;++text)if(strncasecmp(text,needle,length)==0)return (char*)text;
 return 0;
}
