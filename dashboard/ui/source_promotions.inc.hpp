// Included inside main.cpp's private namespace; see docs/CODE_MAP.md.
std::string promotion_directory() {
    std::string directory=std::string(root)+"promotions/";
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_PROMOTION_ROOT")) directory=std::string(override)+"/";
#endif
    return directory;
}
std::string ad_profiles_path() {
#ifndef __PROSPERO__
    if (const char *override=getenv("NXE_AD_PROFILE_PATH")) return override;
#endif
    return std::string(root)+"ad-profiles.tsv";
}
std::vector<std::string> ad_fields(const std::string &line) {
    std::vector<std::string> fields;size_t start=0;
    for (;;) {
        const size_t end=line.find('\t',start);
        fields.push_back(line.substr(start,end==std::string::npos?end:end-start));
        if (end==std::string::npos) return fields;
        start=end+1;
    }
}
bool ad_identity(const std::string &id) {
    return id.size()==64 && std::all_of(id.begin(),id.end(),[](char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f');
    });
}
bool ad_image_path(const std::string &name,const std::string &directory,std::string &path) {
    if (name.empty() || name.find("..")!=std::string::npos || name.find_first_of("/\\:")!=std::string::npos) return false;
    path=directory+"images/"+name;struct stat st;
    return stat(path.c_str(),&st)==0 && st.st_size>0 && st.st_size<(64<<20);
}
void load_ad_catalog() {
    model.ad_catalog.clear();const std::string directory=promotion_directory();
    std::ifstream input(directory+"catalog.tsv");std::string line;std::set<std::string> seen;
    while (model.ad_catalog.size()<4096 && std::getline(input,line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.size()>4096) continue;
        const auto fields=ad_fields(line);
        if (fields.size()<8 || fields.size()>12 || !ad_identity(fields[0]) || seen.count(fields[0])) continue;
        dash::AdEntry entry;
        if (!ad_image_path(fields[2],directory,entry.image)) continue;
        entry.id=fields[0];entry.title=fields[1];entry.preview=fields[3];entry.subtitle=fields[4];entry.classification=fields[5];
        entry.role=fields.size()>8?fields[8]:"unmapped-original-image";
        entry.auto_eligible=fields.size()<10 || fields[9]=="1" || fields[9]=="true";
        entry.preferred_fit=fields.size()>10?fields[10]:"contain";
        entry.caption_source=fields.size()>11?fields[11]:"unknown";
        entry.width=atoi(fields[6].c_str());entry.height=atoi(fields[7].c_str());
        if (entry.title.empty() || entry.width<=0 || entry.height<=0 || entry.width>16384 || entry.height>16384) continue;
        seen.insert(entry.id);model.ad_catalog.push_back(std::move(entry));
    }
}
void load_ad_profiles() {
    for (int era=0;era<6;era++) {model.ad_mode[era]=0;model.ad_selected[era].clear();model.ad_shuffle_seed[era]=0;}
    std::ifstream input(ad_profiles_path());std::string line;
    if (!std::getline(input,line)) return;
    if (!line.empty() && line.back()=='\r') line.pop_back();
    if (line!="EXBOX_AD_PROFILES\t1") return;
    int count=0;
    while (count++<25000 && std::getline(input,line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        const auto fields=ad_fields(line);
        if (fields.size()!=3 || fields[1].size()!=1 || fields[1][0]<'0' || fields[1][0]>'5') continue;
        const int era=fields[1][0]-'0';
        if (fields[0]=="mode" && fields[2].size()==1 && fields[2][0]>='0' && fields[2][0]<='3') model.ad_mode[era]=fields[2][0]-'0';
        else if (fields[0]=="seed" && !fields[2].empty() && fields[2].size()<=10 && std::all_of(fields[2].begin(),fields[2].end(),[](char c){return c>='0' && c<='9';})) {
            char *end=nullptr;const unsigned long long seed=strtoull(fields[2].c_str(),&end,10);
            if (end && !*end && seed<=0xffffffffULL) model.ad_shuffle_seed[era]=unsigned(seed);
        } else if (fields[0]=="pick" && ad_identity(fields[2])) {
            auto &selected=model.ad_selected[era];
            if (selected.size()<4096 && std::find(selected.begin(),selected.end(),fields[2])==selected.end()) selected.push_back(fields[2]);
        }
    }
}
void save_ad_profiles() {
    const std::string path=ad_profiles_path(),temporary=path+".tmp";FILE *file=fopen(temporary.c_str(),"w");
    if (!file) return;
    bool okay=fprintf(file,"EXBOX_AD_PROFILES\t1\n")>0;
    for (int era=0;era<6;era++) {
        okay=fprintf(file,"mode\t%d\t%d\nseed\t%d\t%u\n",era,std::clamp(model.ad_mode[era],0,3),era,model.ad_shuffle_seed[era])>0 && okay;
        std::set<std::string> seen;
        for (const auto &id:model.ad_selected[era]) if (ad_identity(id) && seen.insert(id).second)
            okay=fprintf(file,"pick\t%d\t%s\n",era,id.c_str())>0 && okay;
    }
    okay=fclose(file)==0 && okay;
    if (okay) rename(temporary.c_str(),path.c_str());
}
void load_promotions() {
    const std::string directory=promotion_directory();model.promo_cards.clear();
    auto add=[&](const std::string &title,const std::string &image,const std::string &preview,const std::string &subtitle) {
        dash::Card card;
        card.title=title;card.icon=dash::IconVideo;card.goal=dash::Goal::ToVideos;
        card.image=image;card.preview=preview;card.subtitle=subtitle;
        card.promo=int(model.promo_cards.size());model.promo_cards.push_back(std::move(card));
    };
    const int profile=model.current_ad_profile();
    if (model.ad_mode[profile]==0) {
        const std::string playlist=directory+"build-"+std::to_string(model.dashboard_build)+".tsv";
        for (const auto &line:read_lines(playlist.c_str(),32)) {
            const auto fields=ad_fields(line);if (fields.size()<3) continue;
            std::string image;
            if (ad_image_path(fields[1],directory,image)) add(fields[0],image,fields[2],fields.size()>3?fields[3]:"");
        }
    } else if (model.ad_mode[profile]!=3) {
        std::vector<const dash::AdEntry *> selected;
        for (const auto &id:model.ad_selected[profile]) {
            auto entry=std::find_if(model.ad_catalog.begin(),model.ad_catalog.end(),[&](const dash::AdEntry &ad){return ad.id==id;});
            if (entry!=model.ad_catalog.end()) selected.push_back(&*entry);
        }
        if (model.ad_mode[profile]==2) {
            size_t first_index=selected.empty()?0:model.ad_shuffle_seed[profile]%selected.size();
            if (selected.size()>1 && model.ad_shuffle_seed[profile]==0xffffffffu && first_index==0) first_index=1;
            const dash::AdEntry *first=selected.empty()?nullptr:selected[first_index];
            unsigned state=model.ad_shuffle_seed[profile]?model.ad_shuffle_seed[profile]:0x9e3779b9u+unsigned(profile);
            for (size_t count=selected.size();count>1;count--) {
                state^=state<<13;state^=state>>17;state^=state<<5;
                std::swap(selected[count-1],selected[size_t(state)%count]);
            }
            if (first) std::iter_swap(selected.begin(),std::find(selected.begin(),selected.end(),first));
        }
        for (const auto *entry:selected) add(entry->title,entry->image,entry->preview,entry->subtitle);
    }
    for (auto &selected:model.selected) selected=std::max(0,selected);
    model.selected[model.channel]=std::clamp(model.current(),0,std::max(0,model.count()-1));
    if (model.era==3 && model.metro_focus>=0 && model.metro_focus<dash::kMetroPins)
        model.metro_focus=model.current()%model.metro_page_size();
}
