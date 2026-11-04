// Native libc gap. Use the platform's real interface enumeration; never invent
// an interface index or report success when the requested name is absent.
#include <net/if.h>
#include <cerrno>
#include <cstring>
extern "C" unsigned int if_nametoindex(const char* name) {
  if(!name){errno=EINVAL;return 0;}
  struct if_nameindex* interfaces=if_nameindex();
  if(!interfaces) return 0;
  unsigned int found=0;
  for(auto* entry=interfaces;entry->if_index && entry->if_name;++entry)
    if(std::strcmp(entry->if_name,name)==0){found=entry->if_index;break;}
  if_freenameindex(interfaces);
  if(!found) errno=ENXIO;
  return found;
}
