#pragma once
#include <cstdint>
#include <string>
namespace av2 {
bool offline_marketplace_fetch(const std::string& host,uint16_t port,bool secure,
 const std::string& verb,const std::string& path,int* status,std::string* headers,std::string* body);
}