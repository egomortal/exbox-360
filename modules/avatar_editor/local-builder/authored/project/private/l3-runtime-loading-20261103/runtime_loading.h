#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace av2::loading {
constexpr const char* runtime_path="/data/nxe/loading/current.png";
enum class ExternalStatus { Ready, Missing, IoError, TooLarge, InvalidPng, Unsupported, WrongDimensions, DecodeFailed, AllocationFailed };
struct ExternalPixels { ExternalStatus status=ExternalStatus::InvalidPng;uint32_t width=0,height=0;std::vector<uint8_t> rgba;bool ready()const{return status==ExternalStatus::Ready;} };
// Only Ready replaces the existing built-in neutral image. Never resizes.
ExternalPixels load_external_png(uint32_t target_width,uint32_t target_height,const char*path=runtime_path);
ExternalPixels decode_external_png(const uint8_t*data,size_t size,uint32_t target_width,uint32_t target_height);
}
