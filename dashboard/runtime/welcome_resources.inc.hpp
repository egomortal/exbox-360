// Data generated from the owner's scenes stays in private/era-skins/<build>/welcome.
void load_welcome_resources() {
    const int build=model.era==0?(model.dashboard_build>=5759?6770:1888):
                    model.era==1?9199:model.era==2?13604:model.metro_v1()?15574:17559;
    static int loaded=-1;
    if (loaded==build) return;
    loaded=build;
    std::string base=std::string(root)+"era-skins/";
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_ERA_SKIN_ROOT")) base=std::string(override)+"/";
#endif
    base+=std::to_string(build)+"/welcome/";
    std::ifstream in(base+"pages.tsv");
    std::vector<std::string> lines;
    for (std::string line;std::getline(in,line);) lines.push_back(std::move(line));
    model.load_welcome_content(lines);
    for (auto &entry:model.welcome_content) if (!entry.second.art.empty()) entry.second.art=base+entry.second.art;
}
