// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <string>
namespace render_preference {
inline int load(const std::string& path){char b[4]{};auto* f=std::fopen(path.c_str(),"rb");if(!f)return 1;size_t n=std::fread(b,1,sizeof(b),f);std::fclose(f);bool valid=n==1||(n==2&&b[1]=='\n')||(n==3&&b[1]=='\r'&&b[2]=='\n');return valid&&b[0]=='2'?2:1;}
inline bool save(const std::string& path,int value){if(value!=1&&value!=2)return false;std::string temp=path+".tmp";auto* f=std::fopen(temp.c_str(),"wb");if(!f)return false;char b[2]={char('0'+value),'\n'};bool ok=std::fwrite(b,1,2,f)==2;if(std::fflush(f)!=0)ok=false;if(std::fclose(f)!=0)ok=false;return ok&&std::rename(temp.c_str(),path.c_str())==0;}
}
