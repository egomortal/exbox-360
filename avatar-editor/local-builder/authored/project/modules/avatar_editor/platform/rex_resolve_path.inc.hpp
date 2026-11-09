Entry* HostPathDevice::ResolvePath(const std::string_view path) {
  auto lock=global_critical_region_.Acquire();
  if (path.size()>4096) return nullptr;
  auto* current=static_cast<HostPathEntry*>(root_entry_.get());
  for (const auto& part : rex::string::utf8_split_path(path)) {
    if (part.empty()) continue;
    if (part=="." || part==".." || part.find(':')!=std::string_view::npos) return nullptr;
    if (auto* cached=current->GetChild(part)) { current=static_cast<HostPathEntry*>(cached); continue; }
    av2::platform::DirectoryPage page;
    if (page.open(current->host_path().c_str(),"")!=0) return nullptr;
    std::array<av2::platform::DirectoryPage::Entry,32> batch{};
    std::string matched;
    for (;;) {
      size_t count=0;
      if (page.next(batch,count)!=0) return nullptr;
      if (!count) break;
      for (size_t i=0;i<count;++i) {
        if (!rex::string::utf8_equal_case(batch[i].name,part)) continue;
        if (!matched.empty()) return nullptr; // reject ambiguous case folds
        matched=batch[i].name;
      }
    }
    if (matched.empty()) return nullptr;
    auto full=current->host_path()/rex::to_path(matched);
    std::error_code error;
    auto status=std::filesystem::symlink_status(full,error);
    if (error || std::filesystem::is_symlink(status)) return nullptr;
    FileInfo info;
    if (!GetInfo(full,&info)) return nullptr;
    auto* child=HostPathEntry::Create(this,current,full,info);
    if (!child) return nullptr;
    current->children_.push_back(std::unique_ptr<Entry>(child));
    current=child;
  }
  return current;
}
