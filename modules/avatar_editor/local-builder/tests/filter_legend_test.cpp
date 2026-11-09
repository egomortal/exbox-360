#include "sort_filter_legend.h"
#include "asset.h"
#include <array>
#include <cassert>
#include <string>
#include <vector>

namespace rex::kernel::xam {
int scope=-1;
int GetAvatarCatalogSearchScope() { return scope; }
}

static void le(std::vector<uint8_t>& bytes,size_t at,uint32_t value) {
  for(unsigned i=0;i<4;++i)bytes[at+i]=uint8_t(value>>(i*8));
}
static uint32_t fnv(const uint8_t* p,size_t n) {
  uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;
}
static void write(const std::string& path,const std::vector<uint8_t>& bytes) {
  FILE* f=std::fopen(path.c_str(),"wb");assert(f);
  assert(std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size());assert(!std::fclose(f));
}
int main(int argc,char**argv) {
  assert(argc>=2);
  using namespace av2::sort_filter_legend;
  for(int s:{-3,-1,0,13,24}) {
    rex::kernel::xam::scope=s;
    assert(av2_sort_filter_legend_active()==(s>=0));
    assert(visible(s)==av2_sort_filter_legend_active());
  }
  auto r=fit(0,0,1280,720);
  assert(r.valid&&r.left==519&&r.top==630&&r.right==775&&r.bottom==670);
  r=fit(0,0,3840,2160);
  assert(r.valid&&r.left==1557&&r.top==1890&&r.right==2325&&r.bottom==2010);
  r=fit(100,30,1280,720);
  assert(r.valid&&r.left==619&&r.top==660&&r.right==875&&r.bottom==700);
  assert(!fit(0,0,0,720).valid&&!fit(0,0,1280,-1).valid);
  std::vector<uint8_t> data(38);
  std::memcpy(data.data(),"A1LEGEND",8);le(data,8,1);le(data,12,256);le(data,16,40);le(data,20,6);
  data[32]=0;data[33]=40;data[34]=7;data[35]=9;data[36]=11;data[37]=255;
  le(data,24,fnv(data.data()+32,6));
  const std::string root=argv[1];
  write(root+"/valid.rle",data);
  av2::legend_art::Asset asset;assert(asset.load((root+"/valid.rle").c_str()));
  std::array<uint8_t,256*40*4> decoded{};assert(asset.fill(decoded.data(),256*4));
  for(size_t i=0;i<256*40;++i)assert(decoded[i*4]==7&&decoded[i*4+1]==9&&decoded[i*4+2]==11&&decoded[i*4+3]==255);
  auto bad=data;bad.back()^=1;write(root+"/bad-hash.rle",bad);assert(!asset.load((root+"/bad-hash.rle").c_str()));
  bad=data;bad.push_back(0);write(root+"/extra.rle",bad);assert(!asset.load((root+"/extra.rle").c_str()));
  bad=data;le(bad,12,512);write(root+"/bad-size.rle",bad);assert(!asset.load((root+"/bad-size.rle").c_str()));
  bad=data;bad[32]=bad[33]=0;le(bad,24,fnv(bad.data()+32,6));write(root+"/zero-run.rle",bad);
  assert(asset.load((root+"/zero-run.rle").c_str()));assert(!asset.fill(decoded.data(),256*4));
  assert(!asset.load((root+"/missing.rle").c_str()));
  if(argc>2) { assert(asset.load(argv[2]));assert(asset.fill(decoded.data(),256*4)); }
  std::puts("PASS actual scope bridge,1280/4K/letterbox fit,and bounded existing RLE loader");
}
