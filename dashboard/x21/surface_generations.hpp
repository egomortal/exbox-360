#pragma once
#include <SDL.h>
#include <cstdint>
#include <map>
#include <mutex>
#include <limits>
#include <vector>
namespace surface_generations {
struct Identity {uint64_t id=0,generation=0;explicit operator bool()const{return id&&generation;}};
struct Entry {Identity identity;const void* pixels;int w,h,pitch;uint32_t format,key;bool keyed;};
inline std::mutex lock;inline std::map<SDL_Surface*,Entry> entries;inline uint64_t serial=0;
inline std::vector<uint64_t> retired;inline bool retire_all=false;
inline void retire_locked(uint64_t id){if(!id||retire_all)return;if(retired.size()==8192){retired.clear();retire_all=true;}else retired.push_back(id);}
inline bool take_retired(std::vector<uint64_t>&out){std::lock_guard<std::mutex>g(lock);out.swap(retired);bool all=retire_all;retire_all=false;return all;}
inline Entry describe(SDL_Surface*s,Identity id){Uint32 key=0;bool keyed=SDL_GetColorKey(s,&key)==0;return {id,s->pixels,s->w,s->h,s->pitch,s->format->format,key,keyed};}
// Seal only verified immutable resource completions, never dynamic canvases.
inline Identity seal(SDL_Surface*s){if(!s||!s->format||!s->pixels)return {};std::lock_guard<std::mutex>g(lock);if(serial==UINT64_MAX||(entries.size()>=8192&&!entries.count(s)))return {};auto old=entries.find(s);if(old!=entries.end())retire_locked(old->second.identity.id);Identity id{++serial,1};entries[s]=describe(s,id);return id;}
// Addresses are lookup indices only; storage/key changes revoke the seal.
inline Identity find(SDL_Surface*s){if(!s||!s->format)return {};std::lock_guard<std::mutex>g(lock);auto i=entries.find(s);if(i==entries.end())return {};auto n=describe(s,i->second.identity);auto&o=i->second;if(n.pixels!=o.pixels||n.w!=o.w||n.h!=o.h||n.pitch!=o.pitch||n.format!=o.format||n.keyed!=o.keyed||(n.keyed&&n.key!=o.key)){retire_locked(o.identity.id);entries.erase(i);return {};}return o.identity;}
// BEFORE pixel mutation/free. Queued Publication bytes retain their own lifetime.
inline void invalidate(SDL_Surface*s){if(!s)return;std::lock_guard<std::mutex>g(lock);auto i=entries.find(s);if(i!=entries.end()){retire_locked(i->second.identity.id);entries.erase(i);}}
inline void clear(){std::lock_guard<std::mutex>g(lock);entries.clear();retired.clear();retire_all=true;}
}
