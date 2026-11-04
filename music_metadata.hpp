// Ex-Box addition: album edits live in a sidecar, never in the music files.
#pragma once
#include <array>
#include <cstdio>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

namespace musicmeta {
using Fields=std::array<std::string,3>; // 1025_EditAlbumInfo: Album, Artist, Genre
struct Edit {Fields fields{}; unsigned mask=0;};
using Edits=std::map<std::string,Edit>; // full song paths, including after an album rename
inline Fields apply(Fields fields,const Edit &edit) {
    for (unsigned i=0;i<3;i++) if (edit.mask&(1u<<i)) fields[i]=edit.fields[i];
    return fields;
}
inline Edits changed(Edits edits,const std::vector<std::string> &paths,const Fields &before,const Fields &after,unsigned touched=0) {
    unsigned mask=touched&7;
    for (unsigned i=0;i<3;i++) if (before[i]!=after[i]) mask|=1u<<i;
    if (mask) for (const auto &path:paths) if (!path.empty()) {
        auto &edit=edits[path];edit.mask|=mask;
        for (unsigned i=0;i<3;i++) if (mask&(1u<<i)) edit.fields[i]=after[i];
    }
    return edits;
}
inline std::string encode(const Edits &edits) {
    std::ostringstream out;out<<"EXBOX-ALBUM-1\n";
    for (const auto &row:edits) {
        out<<std::quoted(row.first)<<' '<<row.second.mask;
        for (const auto &field:row.second.fields) out<<' '<<std::quoted(field);
        out<<'\n';
    }
    return out.str();
}
inline bool decode(const std::string &data,Edits &edits) {
    std::istringstream in(data);std::string header,path;Edits parsed;
    if (!std::getline(in,header) || header!="EXBOX-ALBUM-1") return false;
    while (in>>std::ws && !in.eof()) {
        Edit edit;
        if (!(in>>std::quoted(path)>>edit.mask) || path.empty() || path.size()>4096 || edit.mask>7) return false;
        for (auto &field:edit.fields) if (!(in>>std::quoted(field)) || field.size()>4096) return false;
        parsed[path]=edit;
    }
    edits=std::move(parsed);return true;
}
// Commit only after the complete sidecar has been flushed and closed. On the
// console rename replaces atomically; a failed write leaves the old file intact.
inline bool save(const std::string &path,const Edits &edits) {
    const std::string staging=path+".pending",data=encode(edits);
    FILE *f=std::fopen(staging.c_str(),"wb");
    if (!f) return false;
    const bool written=std::fwrite(data.data(),1,data.size(),f)==data.size();
    const bool closed=std::fclose(f)==0;
    return written && closed && std::rename(staging.c_str(),path.c_str())==0;
}
} // namespace musicmeta
