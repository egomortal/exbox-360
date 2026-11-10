// Optional developer page. Included inside main.cpp's private namespace.
// It uses the existing dashboard primitives and never enables diagnostics.
void draw_developer_diagnostics() {
    const auto snapshot=dashboard_diagnostics.snapshot();
    namespace sk=media_skin;
    sk::scene_background();
    constexpr int x=180,y=125,w=1560,h=830;
    sk::background_panel(x,y,w,h);
    const bool light=sk::light();const SDL_Color ink=light?sk::kRowSel:sk::kInk;
    text("Developer Diagnostics",x+55,y+38,large,ink);
    text(snapshot.enabled?"Collection enabled":"Collection disabled",x+w-420,y+50,regular,
         snapshot.enabled?SDL_Color{115,190,35,255}:SDL_Color{180,180,180,255});
    const auto fixed=[](double value){char out[32];std::snprintf(out,sizeof out,"%.3f ms",value);return std::string(out);};
    const auto mib=[](std::uint64_t bytes){char out[48];std::snprintf(out,sizeof out,"%.2f MiB",double(bytes)/(1024.0*1024.0));return std::string(out);};
    const auto unpublished=[](bool published,const std::string&value){return published?value:std::string("Not published");};
    struct Line{const char*label;std::string value;};
    const std::vector<Line> lines={
        {"Frame samples",std::to_string(snapshot.frame_samples)},
        {"Render work p50",snapshot.frame_samples?fixed(snapshot.p50_ms):"Not sampled"},
        {"Render work p99",snapshot.frame_samples?fixed(snapshot.p99_ms):"Not sampled"},
        {"Render work maximum",snapshot.frame_samples?fixed(snapshot.max_ms):"Not sampled"},
        {"Frames over 16.667 ms",std::to_string(snapshot.frames_over_16_667)},
        {"GPU command batches",std::to_string(snapshot.command_batches)},
        {"GPU source uploads",unpublished(snapshot.uploads_published,std::to_string(snapshot.uploads)+" / "+mib(snapshot.upload_bytes))},
        {"CPU fallbacks",std::to_string(snapshot.fallbacks)},
        {"Tracked GPU memory",unpublished(snapshot.gpu_memory_published,mib(snapshot.gpu_current_bytes))},
        {"Tracked GPU peak",unpublished(snapshot.gpu_memory_published,mib(snapshot.gpu_peak_bytes)+(snapshot.over_budget?"  OVER 240 MiB":""))},
        {"Missing local assets",unpublished(snapshot.missing_assets_published,std::to_string(snapshot.missing_assets))},
        {"Unsupported media formats",unpublished(snapshot.unsupported_formats_published,std::to_string(snapshot.unsupported_formats))},
    };
    constexpr int first_y=y+145,pitch=48,left=x+80,value=x+880;
    for(std::size_t i=0;i<lines.size();++i){const int py=first_y+int(i)*pitch;if(i%2==0)shade(x+45,py-5,w-90,pitch,light?0xffffff:0x000000,light?25:35);text(lines[i].label,left,py,regular,ink);text(lines[i].value,value,py,regular,ink);}
    wrapped_text("This page is read-only. It does not relax the 240 MiB ledger or prove native 4K60. Console frame logs and the owner's TV remain the final performance gate.",x+75,y+h-100,w-150,2,small,ink);
}
