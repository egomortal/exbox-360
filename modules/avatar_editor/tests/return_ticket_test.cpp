#include "../native/return_ticket.h"
#include <cassert>
#include <string>
using namespace av2::native;
int main() {
  ReturnTicket original;
  original.nonce="0123456789abcdef0123456789abcdef";
  original.era=3; original.revision=17559;
  original.dashboard_state="{\"screen\":\"avatar\",\"selection\":7}\n";
  original.dashboard_state.push_back('\0'); // length, not C-string termination.
  for(auto result:{ExitResult::saved,ExitResult::cancelled,ExitResult::failed}) {
    original.result=result;
    std::string bytes;
    assert(encode_return_ticket(original,bytes));
    ReturnTicket decoded;
    assert(decode_return_ticket(bytes,decoded));
    assert(decoded.nonce==original.nonce && decoded.era==3 && decoded.revision==17559);
    assert(decoded.result==result && decoded.dashboard_state==original.dashboard_state);
    ReturnTicket unchanged=decoded;
    assert(!decode_return_ticket(bytes.substr(0,bytes.size()-1),decoded));
    assert(decoded.dashboard_state==unchanged.dashboard_state);
    assert(!decode_return_ticket(bytes+"x",decoded));
    auto malformed=bytes; malformed[0]='?';
    assert(!decode_return_ticket(malformed,decoded));
    malformed=bytes; malformed.replace(malformed.find("era=3"),5,"era=-1");
    assert(!decode_return_ticket(malformed,decoded));
    malformed=bytes; malformed.replace(malformed.find("bytes="),6,"bytes=9999999999999999");
    assert(!decode_return_ticket(malformed,decoded));
  }
  std::string output="unchanged";
  original.nonce="../not-a-nonce";
  assert(!encode_return_ticket(original,output) && output=="unchanged");
  original.nonce="0123456789abcdef0123456789abcdef";
  original.dashboard_state.assign(kMaxDashboardState,'x');
  assert(encode_return_ticket(original,output));
  ReturnTicket decoded; assert(decode_return_ticket(output,decoded));
  original.dashboard_state.push_back('x');
  assert(!encode_return_ticket(original,output));
}
