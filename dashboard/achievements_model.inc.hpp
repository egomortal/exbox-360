// Included inside namespace dash. gamer/PlayedGamePanelScene (9199), games/402_Achievements.
std::vector<PlayedGame> Model::achievement_games() const
{
    std::vector<PlayedGame> out;
    std::set<std::string> seen;
    for (const auto &key:recent) {
        if (!seen.insert(key).second) continue;
        if (key.rfind("title:",0)==0) {
            for (const auto &game:games) if (game_key(game)==key) {
                out.push_back({key,game.title,game.icon});break;
            }
        } else {
            for (const auto &app:apps) if (app.executable==key) {
                out.push_back({key,app.title,app.icon});break;
            }
        }
    }
    return out;
}
const TitleAchievements *Model::achievement_title(const std::string &key) const
{
    const auto found=achievement_source.find(key);
    return found==achievement_source.end() || !found->second.known?nullptr:&found->second;
}
AchievementProgress Model::achievement_progress(const std::string &key) const
{
    AchievementProgress out;
    const auto *title=achievement_title(key);
    if (!title) return out;
    out.known=true;out.total=int(title->entries.size());
    for (const auto &entry:title->entries) {
        out.possible+=trophy_points(entry.grade);
        if (entry.earned) {++out.earned;out.points+=trophy_points(entry.grade);}
    }
    return out;
}
std::string Model::achievement_counts(const std::string &key,bool points) const
{
    const auto progress=achievement_progress(key);
    if (!progress.known) return "-- / --"; // Ex-Box addition: no 360 original; unknown, not zero.
    return std::to_string(points?progress.points:progress.earned)+" / "+
           std::to_string(points?progress.possible:progress.total);
}
int Model::achievement_count() const
{
    if (achievement_game.empty()) return int(achievement_games().size());
    const auto *title=achievement_title(achievement_game);
    return title?int(title->entries.size()):0;
}
Effect Model::open_achievements()
{
    if (viewer==Viewer::Achievements) return {};
    achievement_previous_viewer=viewer;achievement_previous_item=viewer_item;
    achievement_previous_full=viewer_full;
    achievement_game.clear();achievement_list_item=0;
    guide=false;
    return open_viewer(Viewer::Achievements);
}
Effect Model::press_achievements(xb::Button button)
{
    if (button==xb::Guide || button==xb::Start) {open_guide();return {};}
    const int n=achievement_count(),last=std::max(0,n-1);
    viewer_item=std::clamp(viewer_item,0,last);
    switch (button) {
    case xb::Up: viewer_item=std::max(0,viewer_item-1);break;
    case xb::Down: viewer_item=std::min(last,viewer_item+1);break;
    case xb::LB: viewer_item=std::max(0,viewer_item-6);break;
    case xb::RB: viewer_item=std::min(last,viewer_item+6);break;
    case xb::LT: viewer_item=0;break;
    case xb::RT: viewer_item=last;break;
    case xb::A:
        if (achievement_game.empty() && n>0) {
            achievement_list_item=viewer_item;
            achievement_game=achievement_games()[std::size_t(viewer_item)].key;
            viewer_item=0;
        }
        break;
    case xb::B:
        if (!achievement_game.empty()) {
            achievement_game.clear();
            viewer_item=std::clamp(achievement_list_item,0,std::max(0,achievement_count()-1));
        } else {
            viewer=achievement_previous_viewer;viewer_item=achievement_previous_item;
            viewer_full=achievement_previous_full;
        }
        break;
    default: break;
    }
    return {};
}
