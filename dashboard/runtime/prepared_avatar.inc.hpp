// UI-owned actors; immutable, commit-checked resources are loaded by one worker.
namespace prepared_avatar {
using Actor=editor_dashboard::appearance::PreparedActor;
struct Slot {std::shared_ptr<Actor> actor;look3d::Canvas canvas;std::vector<avkit::M34> pose;avkit::Camera camera;Uint32 tick=0,emote_at=0;bool painted=false,waving=false,focused=false;};
std::array<Slot,4> slots;
struct Loaded {std::array<std::shared_ptr<Actor>,4> actors;std::array<std::string,4> errors;};
exbox::StartupJob<Loaded> job;
std::array<std::filesystem::path,4> folders;
std::array<std::vector<std::uint8_t>,4> commits;
std::string requested;Uint32 checked=0;
std::filesystem::path root(){return fs_prefix()+"/data/nxe/avatar";}
void release(Slot&s){s.canvas.free();s.actor.reset();s.pose.clear();s.tick=0;s.painted=s.waving=s.focused=false;}
void refresh(bool force=false){
 if(model.era!=3||model.metro_tab!=2||model.viewer!=dash::Viewer::None)for(auto&s:slots)s.focused=false;
 Loaded loaded;
 if(job.take(loaded))for(std::size_t i=0;i<slots.size();++i){
  std::vector<std::uint8_t> current;
  if(loaded.actors[i]&&Actor::read(folders[i]/"avatar_manifest.bin",1000,current)&&editor_dashboard::appearance::coherent(loaded.actors[i]->manifest(),current)){
   release(slots[i]);slots[i].actor=std::move(loaded.actors[i]);
  }else if(!loaded.errors[i].empty()&&std::filesystem::exists(folders[i]/"dashboard-live"))log(("Prepared avatar "+std::to_string(i)+": "+loaded.errors[i]+"; accepted image fallback").c_str());
 }
 const auto now=SDL_GetTicks();if(!force&&now-checked<1000)return;checked=now;
 if(job.active())return;
 std::array<std::filesystem::path,4> next;next[0]=root()/"editor";
 for(int i=1;i<4;++i){auto saved=root()/"friends"/std::to_string(i);next[std::size_t(i)]=std::filesystem::exists(saved/"avatar_manifest.bin")?saved:root()/"dashboard-friends"/std::to_string(i);}
 std::array<std::vector<std::uint8_t>,4> bytes;std::string signature;
 for(std::size_t i=0;i<slots.size();++i){
  Actor::read(next[i]/"avatar_manifest.bin",1000,bytes[i]);
  signature+=next[i].string()+":"+exbox::themes::sha256(std::string_view(reinterpret_cast<const char*>(bytes[i].data()),bytes[i].size()));
  std::error_code e;const auto stamp=std::filesystem::last_write_time(next[i]/"dashboard-live"/"face.bin",e);if(!e)signature+=":"+std::to_string(static_cast<long long>(stamp.time_since_epoch().count()));signature+='|';
  if(slots[i].actor&&!editor_dashboard::appearance::coherent(slots[i].actor->manifest(),bytes[i]))release(slots[i]);
 }
 if(signature==requested&&!force)return;requested=signature;folders=next;commits=bytes;
 job.start([next,bytes]{Loaded out;for(std::size_t i=0;i<out.actors.size();++i)if(bytes[i].size()==1000)out.actors[i]=Actor::load(next[i]/"dashboard-live",bytes[i],out.errors[i]);return out;});
}
Slot* find(SDL_Surface*surface){for(auto&s:slots)if(s.actor&&s.canvas.surface==surface)return &s;return nullptr;}
bool active(){return bool(slots[0].actor);}
bool emote(int index,Uint32 now){if(index<0||index>=int(slots.size()))return false;auto&s=slots[std::size_t(index)];if(!s.actor||!s.actor->has_emote())return false;if(!s.waving){s.waving=true;s.emote_at=now;s.pose.clear();}return true;}
void focus(int index,bool focused,Uint32 now){auto&s=slots[std::size_t(index)];if(focused&&!s.focused)emote(index,now);s.focused=focused;}
SDL_Surface* frame(int index,Uint32 now){
 if(index<0||index>=int(slots.size()))return nullptr;auto&s=slots[std::size_t(index)];if(!s.actor)return nullptr;
 const bool resized=s.canvas.img.w!=600*render_density::factor||s.canvas.img.h!=660*render_density::factor;
 auto*surface=s.canvas.ensure(600,660);if(!surface)return nullptr;if(resized)s.painted=false;
 if(s.tick!=now||s.pose.empty()){
  const double elapsed=double(now-s.emote_at)/1000.;if(s.waving&&elapsed>=s.actor->clip(true).duration())s.waving=false;
  s.pose=s.actor->pose(s.waving?elapsed:double(now)/1000.+double(index)*.71,s.waving);if(model.video_theater)theater_pose::sit(s.pose);s.camera=s.actor->camera();if(model.video_theater)s.camera.yaw=3.14159265f;s.tick=now;s.painted=false;
 }
 return surface;
}
void materialize(SDL_Surface*surface){if(auto*s=find(surface))if(!s->painted){s->actor->renderer().render(s->canvas.img,s->pose,s->camera,2);s->painted=true;}}
void close(){job.join();for(auto&s:slots)release(s);requested.clear();}
}
