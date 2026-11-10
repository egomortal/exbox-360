#pragma once
#include "surface_generations.hpp"
#include "publication_registry.hpp"
namespace x21source {
inline ResourceKey surface_key(SDL_Surface*s){std::vector<uint64_t>ids;bool all=surface_generations::take_retired(ids);if(all)generations.retire_owner(3);else for(auto id:ids)generations.retire_resource(3,id);auto id=surface_generations::find(s);return id?ResourceKey{3,1,id.id,id.generation}:ResourceKey{};}
inline std::shared_ptr<const Publication> surface(SDL_Surface*s){auto key=surface_key(s);return key.valid()?known(key,s->pixels,size_t(s->pitch)*s->h):global_cache.intern(s->pixels,size_t(s->pitch)*s->h);}
}
