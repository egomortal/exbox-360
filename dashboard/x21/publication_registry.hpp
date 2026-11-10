#pragma once
#include "source_generations.hpp"
namespace x21source {
inline Cache global_cache;
inline Generations generations(global_cache);
inline std::shared_ptr<const Publication> known(ResourceKey key,const void*data,size_t bytes){
 if(auto value=generations.find(key))return value;
 generations.retire_resource(key.owner,key.resource);
 return generations.publish(key,data,bytes);
}
}
