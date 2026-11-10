// Included inside main.cpp's private namespace; dashboard-only Experimental preview.
void draw_rom_game_guide_preview() {
    const auto rows=model.rom_game_guide_preview.rows();
    const int item=std::clamp(int(model.rom_game_guide_preview.selected()),0,std::max(0,int(rows.size())-1));
    shade(0,0,W,H,0x000000,190);
    constexpr int px=540,py=245,pw=840,ph=590,row_h=68;
    rounded(px,py,pw,ph,12,0xa9b0ba,0x8c939d);
    gradient(px+3,py+3,pw-6,ph-6,0xe1e5e5,0xb8c0c2);
    gradient(px+3,py+3,pw-6,10,0x8fd23c,0x4f9217);
    text(exbox_rom_guide::preview_title,px+38,py+34,large,{30,34,34,255});
    text(exbox_rom_guide::preview_fallback,px+40,py+94,small,{72,78,78,255});
    for(std::size_t i=0;i<rows.size();++i){
        const int y=py+140+int(i)*row_h;const bool on=int(i)==item;
        if(on)gradient(px+24,y,pw-48,row_h-6,0x6eb010,0x3f7f05);
        else shade(px+24,y+row_h-7,pw-48,1,0x687276,90);
        const SDL_Color ink=on?SDL_Color{246,248,244,255}:SDL_Color{42,46,46,255};
        text(rows[i].label,px+48,y+13,regular,ink);
        if(!rows[i].enabled){
            const std::string reason=rows[i].unavailable_reason;
            text(fit(reason,small,310),px+pw-350,y+18,small,on?SDL_Color{225,235,219,255}:SDL_Color{102,108,108,255});
        }
    }
    text(rows.empty()||!rows[std::size_t(item)].enabled?"Unavailable":"A Select",px+40,py+ph-42,small,{52,58,58,255});
    text("B / Guide Close",px+pw-220,py+ph-42,small,{52,58,58,255});
}
