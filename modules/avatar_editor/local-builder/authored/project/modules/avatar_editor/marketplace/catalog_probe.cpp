#include "offline_marketplace.h"
#include "kernel/xam/avatars/closet.h"
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace av2 {
bool verify_offline_catalog(const std::filesystem::path& assets) {
  auto& closet=rex::avatars::GetCloset();
  if (!closet.Load(assets/"closet") || closet.items().empty()) return false;
  for (const auto& item:closet.items()) if (closet.Find(item.id)!=&item) {
    std::fprintf(stderr,"LOCAL CATALOG ownership mismatch: %s stock=%d\n",item.id.to_string().c_str(),rex::avatars::IsStockPackId(item.id));
    return false;
  }
  size_t tested=0;
  for (size_t i=0;i<closet.items().size();i+=std::max(size_t(1),closet.items().size()/16)) {
    const auto& item=closet.items()[i];
    std::vector<uint8_t> original;
    if (!closet.ReadItemBytes(item.id,original) || original.empty()) return false;
    int status=0; std::string headers,body;
    if (!offline_marketplace_fetch("av2.local",80,false,"GET","/local/avataritems/"+item.id.to_string()+".acp",
        &status,&headers,&body) || status!=200 || body.size()!=original.size() ||
        std::memcmp(body.data(),original.data(),body.size())!=0 || headers.find("X-Avatar-Item-Name:")==std::string::npos)
      { std::fprintf(stderr,"LOCAL CATALOG delivery mismatch: %s status=%d got=%zu expected=%zu\n",item.id.to_string().c_str(),status,body.size(),original.size()); return false; }
    ++tested;
  }
  for (const char* path:{"/icons/../../secret.png","/avataritems/not-a-guid.acp"}) {
    int status=0; std::string headers,body;
    if (!offline_marketplace_fetch("av2.local",80,false,"GET",path,&status,&headers,&body) || status!=404 || !body.empty())
      return false;
  }
  for(const auto& request: {std::pair{"GET","/epix/en-US/manifest.xml"},
      std::pair{"GET","/Catalog/Catalog.asmx/Query?methodName=FindGameOffers"},
      std::pair{"POST","/avataritems/not-a-guid.acp"}}) {
    int status=0;std::string headers,body;
    if(!offline_marketplace_fetch("av2.local",80,false,request.first,request.second,&status,&headers,&body))return false;
    const bool catalog=std::string(request.second).starts_with("/Catalog/");
    if(status!=(catalog?200:std::string(request.first)=="POST"?405:410))return false;
    if(catalog ? body!="<feed><totalItems>0</totalItems><numItems>0</numItems></feed>" : !body.empty())return false;
  }
  std::fprintf(stderr,"LOCAL CATALOG verified %zu ownership lookups, %zu unchanged compressed deliveries, invalid paths rejected\n",
    closet.items().size(),tested);
  return true;
}
}