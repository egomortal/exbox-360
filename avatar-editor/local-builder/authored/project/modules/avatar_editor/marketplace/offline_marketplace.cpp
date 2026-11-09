#include "offline_marketplace.h"
#include "kernel/xam/avatars/closet.h"
#include <cstdio>
#include <string_view>
namespace av2 {
namespace {
std::string percent_encode(const std::string& s) {
  constexpr char digits[]="0123456789ABCDEF";
  std::string out;
  for (const unsigned char c:s) {
    if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_') out+=char(c);
    else { out+='%'; out+=digits[c>>4]; out+=digits[c&15]; }
  }
  return out;
}
}
// Local item transport. Only parsed GUIDs reach the closet, so path traversal
// cannot select an arbitrary host file. Compressed blobs are returned unchanged.
bool offline_marketplace_fetch(const std::string&,uint16_t,bool,const std::string& verb,
 const std::string& path,int* status,std::string* headers,std::string* body) {
  *status=404; headers->clear(); body->clear();
  if (verb!="GET") { *status=405; return true; }
  const std::string route=path.substr(0,path.find('?'));
  // Owner: no storefront or service connections. Quietly finish the original
  // background catalog preload with an empty feed; local clothing uses Closet.
  if(route=="/Catalog/Catalog.asmx/Query") {
    *body="<feed><totalItems>0</totalItems><numItems>0</numItems></feed>";
    *status=200;*headers="Content-Type: application/xml\r\nContent-Length: "+std::to_string(body->size())+"\r\n";
    return true;
  }
  if(route.starts_with("/epix/")){*status=410;return true;}

  const bool icon=route.starts_with("/icons/") && route.ends_with(".png");
  const auto marker=route.find("/avataritems/");
  const bool item=marker!=std::string::npos && route.ends_with(".acp");
  if (!icon && !item) {
    return true;
  }
  const size_t start=icon?7:marker+std::string_view("/avataritems/").size();
  const std::string guid=route.substr(start,route.size()-start-4);
  rex::avatars::AssetId id{};
  if (!rex::avatars::ParseAssetId(guid,&id)) return true;
  auto& closet=rex::avatars::GetCloset();
  const auto* entry=closet.Find(id);
  std::vector<uint8_t> bytes;
  if (!(icon?closet.ReadItemIcon(id,bytes):closet.ReadItemBytes(id,bytes))) return true;
  body->assign(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  *headers=std::string("Content-Type: ")+(icon?"image/png":"application/octet-stream")+
    "\r\nContent-Length: "+std::to_string(bytes.size())+"\r\n";
  if (entry && !icon) {
    char categories[16]; std::snprintf(categories,sizeof(categories),"%x",entry->categories);
    *headers+="X-Avatar-Item-Name: "+percent_encode(entry->name)+"\r\nX-Avatar-Item-Categories: "+categories+
      "\r\nX-Avatar-Item-Bodies: "+std::to_string(entry->bodies)+"\r\n";
  }
  *status=200; return true;
}
}
namespace ae::gpu {
// Null backend has no GPU resource cache. Q6 must forward these invalidations.
#ifndef AV2_OWNER_RENDERER
void MarkRangeDirty(uint32_t,uint32_t) {}
#endif
}