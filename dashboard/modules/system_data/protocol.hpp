#pragma once
// Ex-Box addition: portable companion wire types. Paths/names are hex encoded,
// so whitespace/newlines never create extra commands or records.
#include <cstdint>
#include <string>
#include <vector>
namespace exbox::system_data {
constexpr std::uint64_t max_file_bytes=64ull*1024*1024;
constexpr std::size_t max_listing_bytes=4*1024*1024;
struct Entry {std::string name;char type='?';std::uint64_t size=0;std::int64_t mtime=0;std::uint64_t device=0;};
inline std::string hex(const std::string& text){static constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(text.size()*2);for(unsigned char c:text){out+=digits[c>>4];out+=digits[c&15];}return out;}
inline bool unhex(const std::string& text,std::string& out){
 if(text.size()%2||text.size()>8192)return false;
 auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
 std::string value;value.reserve(text.size()/2);
 for(std::size_t i=0;i<text.size();i+=2){int a=digit(text[i]),b=digit(text[i+1]);if(a<0||b<0||((a<<4)|b)==0)return false;value+=char((a<<4)|b);}out=std::move(value);return true;
}
inline std::string entry_line(const Entry& e){return std::string(1,e.type)+"\t"+std::to_string(e.size)+"\t"+std::to_string(e.mtime)+"\t"+std::to_string(e.device)+"\t"+hex(e.name)+"\n";}
}
