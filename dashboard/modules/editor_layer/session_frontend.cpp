// SPDX-License-Identifier: GPL-3.0-or-later
// Local process supervisor. It observes save files and emits our existing ticket;
// it neither edits guest saves nor supplies a fabricated guest cancellation.
#include "session_outcome.hpp"
#include "../avatar_editor/native/ticket_file.h"
#include <nlohmann/json.hpp>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace {
std::optional<std::string> read_file(const std::filesystem::path& path,bool& valid) {
    const int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0){if(errno!=ENOENT)valid=false;return {};}
    struct stat st{};
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || st.st_size>65536) {
        valid=false;close(fd);return {};
    }
    std::string bytes(static_cast<size_t>(st.st_size),'\0');size_t at=0;
    while(at<bytes.size()) {
        const auto n=read(fd,bytes.data()+at,bytes.size()-at);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){valid=false;close(fd);return {};}
        at+=static_cast<size_t>(n);
    }
    char extra;ssize_t tail;
    do{tail=read(fd,&extra,1);}while(tail<0 && errno==EINTR);
    if(tail!=0)valid=false;
    close(fd);return bytes;
}
exbox::editor::SaveSnapshot snapshot(const std::filesystem::path& root) {
    exbox::editor::SaveSnapshot result;
    result.manifest=read_file(root/"avatar_manifest.bin",result.readable);
    result.metadata=read_file(root/"avatar.json",result.readable);
    return result;
}
bool complete(const exbox::editor::SaveSnapshot& value) {
    if(!value.metadata)return false;
    const auto json=nlohmann::json::parse(*value.metadata,nullptr,false);
    return json.is_object() && json.contains("schema") && json["schema"]==1 &&
        json.contains("complete") && json["complete"].is_boolean() && json["complete"]==true &&
        json.contains("manifest") && json["manifest"]=="avatar_manifest.bin";
}
}
int main(int argc,char** argv) {
    if(argc<6 || std::string_view(argv[4])!="--") {
        std::fprintf(stderr,"usage: session-frontend SAVE_ROOT CONTEXT_TICKET RETURN_TICKET -- COMMAND [ARGS...]\n");return 2;
    }
    av2::native::ReturnTicket ticket;
    if(!av2::native::read_return_ticket(argv[2],ticket))return 3;
    const auto before=snapshot(argv[1]);
    if(!before.readable)return 4;
    const pid_t child=fork();
    if(child<0)return 5;
    if(child==0){execvp(argv[5],argv+5);_exit(127);}
    int status=0;pid_t waited;
    do{waited=waitpid(child,&status,0);}while(waited<0 && errno==EINTR);
    const bool normal=waited==child && WIFEXITED(status) && WEXITSTATUS(status)==0;
    const auto after=snapshot(argv[1]);
    ticket.result=exbox::editor::session_outcome(before,after,normal,complete(after));
    if(av2::native::write_return_ticket(argv[3],ticket)==av2::native::TicketWrite::failed)return 6;
    return ticket.result==av2::native::ExitResult::failed?7:0;
}
