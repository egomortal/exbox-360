// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/offline_hive.hpp"
#include <array>
#include <cassert>
using namespace exbox::editor;
int main() {
    assert(offline_hive_value("AvatarPhotoBoothEnabled", "ignored", 0)=="1");
    for (auto key : {"CatalogUriRoot", "CatalogCDNUriRoot"}) {
        assert(offline_hive_value(key,"localhost",8080)=="localhost");
        assert(offline_hive_value(key,"",8080).empty());
    }
    for (auto key : {"CatalogUriPort", "CatalogCDNUriPort"}) {
        assert(offline_hive_value(key,"ignored",0)=="0");
        assert(offline_hive_value(key,"ignored",65535)=="65535");
    }
    for (auto key : {"", "unknown", "AvatarMarketplaceEnabled", "AvatarAssetUriRoot", "AvatarAssetRefreshFrequency", "EpixUriRoot"})
        assert(offline_hive_value(key,"host",80).empty());
    for (std::string value : {std::string(),std::string("1"),std::string("localhost"),std::string("65535")})
    for (bool present : {false,true}) for (uint32_t cap=0;cap<32;++cap) {
        std::array<uint8_t,48> bytes;bytes.fill(0xcc);
        const auto status=copy_hive_value(value,present,cap,std::span(bytes).subspan(8,32));
        const bool success=!value.empty()&&present&&cap>value.size();
        assert(status==(value.empty()||!present||!cap?HiveStatus::not_found:success?HiveStatus::success:HiveStatus::insufficient_buffer));
        for(size_t i=0;i<bytes.size();++i) {
            if(success&&i>=8&&i<8+value.size())assert(bytes[i]==uint8_t(value[i-8]));
            else if(success&&i==8+value.size())assert(bytes[i]==0);
            else assert(bytes[i]==0xcc);
        }
    }
    std::array<uint8_t,1> short_buffer{0xcc};
    assert(copy_hive_value("1",true,2,short_buffer)==HiveStatus::invalid_span);
    assert(short_buffer[0]==0xcc);
}
