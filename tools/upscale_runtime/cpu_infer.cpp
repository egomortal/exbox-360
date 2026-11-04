// Original Ex-Box CPU adapter. ncnn is linked from the pinned BSD-3-Clause source build.
#include "net.h"
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
int main(int argc,char**argv){
 if(argc!=7)return 2;
 int w=std::stoi(argv[5]),h=std::stoi(argv[6]);if(w<1||h<1||w>512||h>512)return 3;
 std::vector<unsigned char> rgb(size_t(w)*h*3);std::ifstream input(argv[3],std::ios::binary);input.read(reinterpret_cast<char*>(rgb.data()),rgb.size());if(!input||input.peek()!=EOF)return 4;
 ncnn::Net net;net.opt.num_threads=1;net.opt.use_vulkan_compute=false;net.opt.use_packing_layout=false;net.opt.use_winograd_convolution=false;net.opt.use_sgemm_convolution=true;net.opt.use_fp16_packed=false;net.opt.use_fp16_storage=false;net.opt.use_fp16_arithmetic=false;net.opt.use_bf16_storage=false;
 if(net.load_param(argv[1])||net.load_model(argv[2]))return 5;
 auto image=ncnn::Mat::from_pixels(rgb.data(),ncnn::Mat::PIXEL_RGB,w,h);for(int c=0;c<3;++c){float* channel=image.channel(c);for(int i=0;i<w*h;++i)channel[i]/=255.f;}auto ex=net.create_extractor();if(ex.input("input",image))return 6;ncnn::Mat out;if(ex.extract("output",out)||out.w!=w*4||out.h!=h*4||out.c!=3)return 7;
 std::vector<unsigned char> result(size_t(out.w)*out.h*3);for(int c=0;c<3;++c){const float* src=out.channel(c);for(size_t i=0;i<size_t(out.w)*out.h;++i){if(!std::isfinite(src[i]))return 8;result[i*3+c]=static_cast<unsigned char>(std::floor(std::clamp(src[i],0.f,1.f)*255.f+0.5f));}}
 std::ofstream output(argv[4],std::ios::binary);output.write(reinterpret_cast<const char*>(result.data()),result.size());output.close();return output?0:9;
}

