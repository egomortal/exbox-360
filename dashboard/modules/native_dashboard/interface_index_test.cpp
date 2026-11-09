#include <net/if.h>
#include <cerrno>
#include <cassert>
static bool fail=false;
static int freed=0;
extern "C" struct if_nameindex* if_nameindex() noexcept {
  static char loop[]="lo0",ether[]="eth0";
  static struct if_nameindex entries[]={{1,loop},{7,ether},{0,nullptr}};
  if(fail){errno=ENOMEM;return nullptr;}
  return entries;
}
extern "C" void if_freenameindex(struct if_nameindex*) noexcept {++freed;}
int main() {
  assert(if_nametoindex("eth0")==7 && freed==1);
  assert(if_nametoindex("lo0")==1 && freed==2);
  assert(if_nametoindex("missing")==0 && errno==ENXIO && freed==3);
  assert(if_nametoindex("")==0 && errno==ENXIO && freed==4);
  fail=true;assert(if_nametoindex("eth0")==0 && errno==ENOMEM && freed==4);
}
