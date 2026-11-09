// Native titles have no terminal. Keep compiled spdlog fallback detection safe.
// No host override; linked only by build-q7-frontend.sh.
extern "C" int __wrap_isatty(int) { return 0; }

// The SDK assigns strcasestr to a WebKit-only provider unavailable to native
// titles. Preserve its locale-aware byte comparison using libc tolower.
#include <cctype>
extern "C" char* __wrap_strcasestr(const char* haystack,const char* needle) {
  if(!*needle)return const_cast<char*>(haystack);
  for(const char* start=haystack;*start;++start) {
    const char* h=start;const char* n=needle;
    while(*h && *n && std::tolower(static_cast<unsigned char>(*h))==
                       std::tolower(static_cast<unsigned char>(*n))) {++h;++n;}
    if(!*n)return const_cast<char*>(start);
  }
  return nullptr;
}
