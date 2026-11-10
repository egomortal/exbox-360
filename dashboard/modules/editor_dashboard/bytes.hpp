// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace editor_dashboard::appearance {
// C++17 view; borrowing is explicit at the resource-owner boundary.
class ByteView {
    const std::uint8_t* data_=nullptr;
    std::size_t size_=0;
public:
    ByteView()=default;
    ByteView(const std::uint8_t* data,std::size_t size):data_(data),size_(size){}
    template<class Container> ByteView(const Container& bytes):data_(bytes.data()),size_(bytes.size()){}
    const std::uint8_t* data()const{return data_;}
    std::size_t size()const{return size_;}
    std::uint8_t operator[](std::size_t i)const{return data_[i];}
    ByteView subspan(std::size_t offset,std::size_t count=std::size_t(-1))const{
        if(offset>size_)return {};
        if(count==std::size_t(-1))count=size_-offset;
        if(count>size_-offset)return {};
        return {data_?data_+offset:nullptr,count};
    }
    ByteView first(std::size_t count)const{return subspan(0,count);}
};
}
