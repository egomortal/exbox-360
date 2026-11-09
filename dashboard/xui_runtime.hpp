#pragma once
// Timeline, quaternion and anchor behavior informed by Tom Clay's BSD-3-Clause
// XUI runtime. See third_party/xui-runtime/LICENSE and docs/XUI_RUNTIME.md.
#include "xui_composite.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>

namespace xui {
enum Field {Position,Scale,Rotation,Pivot,Width,Height,Opacity,Show,Anchor,Blend,Visual,
    FillColor,FillScale,FillTranslation,FillRotation,StrokeWidth,StrokeColor,TextColor,
    ShadowColor,Text,PointSize,TextStyle,Texture,Enabled,StopPosition,StopColor,StopCount,File,
    Font,LineSpacing,TextScale,ImageMode,FieldCount};
struct Value {float v[4]{};uint32_t token=0;};
struct Node {int32_t parent,kind,branch_owner,branch_visual,id,point_first,point_count,
    stop_first,stop_count,fill_type,clip,closed;float design_w,design_h,path_w,path_h;
    Value p[FieldCount];};
struct Animation {int32_t owner,range_first,range_count,track_first,track_count;float last;int32_t automatic;};
struct Track {int32_t animation,target,field,index,key_first,key_count;};
struct Key {float frame;int32_t interpolation;float ease_in,ease_out,ease_scale;Value value;};
struct NamedFrame {uint32_t name;float frame;int32_t command;uint32_t target;};
struct Point {float x=0,y=0;};
struct Stop {float position;uint32_t color;};
static_assert(sizeof(Value)==20 && sizeof(Node)==64+20*FieldCount,"XUI pack layout");
static_assert(sizeof(Animation)==28 && sizeof(Track)==24 && sizeof(Key)==40 && sizeof(NamedFrame)==16,"XUI timeline layout");
struct Matrix {
    float a[4][4]{};
    static Matrix identity() {Matrix m;for (int i=0;i<4;i++) m.a[i][i]=1;return m;}
    static Matrix translate(float x,float y,float z=0) {auto m=identity();m.a[0][3]=x;m.a[1][3]=y;m.a[2][3]=z;return m;}
    static Matrix scale(float x,float y,float z=1) {auto m=identity();m.a[0][0]=x;m.a[1][1]=y;m.a[2][2]=z;return m;}
    Point point(float x,float y) const {return {a[0][0]*x+a[0][1]*y+a[0][3],a[1][0]*x+a[1][1]*y+a[1][3]};}
    Matrix operator*(const Matrix &b) const {
        Matrix r;
        for (int y=0;y<4;y++) for (int x=0;x<4;x++) for (int k=0;k<4;k++) r.a[y][x]+=a[y][k]*b.a[k][x];
        return r;
    }
};
inline Matrix quaternion(const Value &value) {
    const auto *v=value.v;float length=0;for (int i=0;i<4;i++) length+=v[i]*v[i];
    if (length<1e-12f) return Matrix::identity();
    const float inv=1/std::sqrt(length),x=v[0]*inv,y=v[1]*inv,z=v[2]*inv,w=v[3]*inv;
    auto m=Matrix::identity();
    m.a[0][0]=1-2*(y*y+z*z);m.a[0][1]=2*(x*y-z*w);m.a[0][2]=2*(x*z+y*w);
    m.a[1][0]=2*(x*y+z*w);m.a[1][1]=1-2*(x*x+z*z);m.a[1][2]=2*(y*z-x*w);
    m.a[2][0]=2*(x*z-y*w);m.a[2][1]=2*(y*z+x*w);m.a[2][2]=1-2*(x*x+y*y);
    return m;
}
inline Matrix local(const Value *p,float x,float y) {
    const auto &pivot=p[Pivot];const auto &s=p[Scale];
    return Matrix::translate(x,y,p[Position].v[2])*Matrix::translate(pivot.v[0],pivot.v[1],pivot.v[2])*
        quaternion(p[Rotation])*Matrix::scale(s.v[0],s.v[1],s.v[2])*
        Matrix::translate(-pivot.v[0],-pivot.v[1],-pivot.v[2]);
}
inline float ease(float t,float in,float out) {
    const float p1=(1-in/100)/3,p2=1-(1-out/100)/3,u=1-t;
    return 3*u*u*t*p1+3*u*t*t*p2+t*t*t;
}
inline bool color_field(int field) {return field==FillColor || field==StrokeColor || field==TextColor || field==ShadowColor || field==StopColor;}
inline bool discrete_field(int field) {return field==Visual || field==Text || field==Texture || field==File || field==Font || field==Show || field==Enabled || field==Anchor || field==Blend || field==TextStyle || field==ImageMode || field==StopCount;}
inline Value interpolate(const Key &a,const Key &b,float frame,int field) {
    if (frame<=a.frame || b.frame<=a.frame || a.interpolation==1 || discrete_field(field)) return a.value;
    float t=std::clamp((frame-a.frame)/(b.frame-a.frame),0.f,1.f);
    if (a.interpolation==2) t=ease(t,a.ease_in,a.ease_out);
    Value out=a.value;
    if (color_field(field)) {
        out.token=0;
        for (int shift=0;shift<32;shift+=8) {
            const float from=float((a.value.token>>shift)&255),to=float((b.value.token>>shift)&255);
            out.token|=uint32_t(std::clamp(int(std::lround(from+(to-from)*t)),0,255))<<shift;
        }
    } else {
        float sign=1;
        if (field==Rotation) {
            float dot=0;for (int i=0;i<4;i++) dot+=a.value.v[i]*b.value.v[i];
            if (dot<0) sign=-1;
        }
        float length=0;
        for (int i=0;i<4;i++) {out.v[i]=a.value.v[i]+(b.value.v[i]*sign-a.value.v[i])*t;length+=out.v[i]*out.v[i];}
        if (field==Rotation && length>1e-12f) for (float &v:out.v) v/=std::sqrt(length);
    }
    return out;
}
struct World {Matrix matrix;float width=0,height=0,opacity=0;int blend=Normal;bool visible=false;};
struct Playback {float frame=0;bool playing=false;};
struct SoundEvent {int node;uint32_t file;float frame;};
class Scene {
    void collect_events(size_t animation,float from,float to,bool include_from=false) {
        const size_t first=sound_events.size();
        const auto &a=animations[animation];
        for (int t=a.track_first;t<a.track_first+a.track_count;t++) if (tracks[t].field==File) {
            const auto &track=tracks[t];
            for (int k=track.key_first;k<track.key_first+track.key_count;k++) {
                const auto &key=keys[k];
                if ((key.frame>from || (include_from && key.frame==from)) && key.frame<=to && *str(key.value.token))
                    sound_events.push_back({track.target,key.value.token,key.frame});
            }
        }
        std::stable_sort(sound_events.begin()+first,sound_events.end(),[](const SoundEvent &a,const SoundEvent &b){return a.frame<b.frame;});
    }
    template<typename T> static bool read(FILE *file,std::vector<T> &values,uint32_t count,uint32_t limit) {
        if (count>limit) return false;
        values.resize(count);return !count || std::fread(values.data(),sizeof(T),count,file)==count;
    }
public:
    std::vector<Node> nodes;
    std::vector<Animation> animations;
    std::vector<Track> tracks;
    std::vector<Key> keys;
    std::vector<NamedFrame> ranges;
    std::vector<Point> points;
    std::vector<Stop> original_stops,stops;
    std::vector<char> strings;
    std::vector<std::array<Value,FieldCount>> state;
    std::vector<World> world;
    std::vector<Playback> playback;
    std::vector<SoundEvent> sound_events;
    std::map<std::pair<int,int>,Value> host_bindings;
    std::string error;
    bool valid=false;
    const char *str(uint32_t at) const {return at<strings.size()?strings.data()+at:"";}
    bool bind_string(int node,int field,const std::string &text) {
        if (node<0 || size_t(node)>=nodes.size() || (field!=Texture && field!=Text && field!=Font)) return false;
        auto &value=host_bindings[{node,field}];
        if (!std::strcmp(str(value.token),text.c_str())) return true;
        value.token=uint32_t(strings.size());strings.insert(strings.end(),text.begin(),text.end());strings.push_back(0);return true;
    }
    // Host-chosen image placement (a substituted bitmap fitted to the node).
    bool bind_discrete(int node,int field,unsigned v) {
        if (node<0 || size_t(node)>=nodes.size()) return false;
        Value value;value.v[0]=float(v);value.token=v;host_bindings[{node,field}]=value;return true;
    }
    bool bind_image_mode(int node,unsigned mode) {return bind_discrete(node,ImageMode,mode);}
    // Back to the scene's own value.
    void unbind(int node,int field) {host_bindings.erase({node,field});}
    // Host-forced visibility (the Guide shows one of its alternative rows).
    bool bind_show(int node,bool show) {
        if (node<0 || size_t(node)>=nodes.size()) return false;
        Value value;value.v[0]=show?1.f:0.f;value.token=show;host_bindings[{node,Show}]=value;return true;
    }
    // Host-placed geometry (the NXE legend packs its labels by their widths).
    bool bind_position(int node,float x,float y) {
        if (node<0 || size_t(node)>=nodes.size()) return false;
        Value value;value.v[0]=x;value.v[1]=y;host_bindings[{node,Position}]=value;return true;
    }
    bool bind_width(int node,float width) {
        if (node<0 || size_t(node)>=nodes.size()) return false;
        Value value;value.v[0]=width;host_bindings[{node,Width}]=value;return true;
    }
    int find(const char *id,int ancestor=-1) const {
        for (size_t i=0;i<nodes.size();i++) if (std::strcmp(str(nodes[i].id),id)==0) {
            if (ancestor<0) return int(i);
            for (int p=int(i);p>=0;p=nodes[p].parent) if (p==ancestor) return int(i);
        }
        return -1;
    }
    bool load(const char *path) {
        valid=false;error.clear();sound_events.clear();host_bindings.clear();FILE *file=std::fopen(path,"rb");
        if (!file) {error="Cannot open XUI pack";return false;}
        char magic[8]{};uint32_t n[8]{};
        bool ok=std::fread(magic,1,8,file)==8 && !std::memcmp(magic,"NXUI0002",8) && std::fread(n,4,8,file)==8;
        ok=ok && read(file,nodes,n[0],20000) && read(file,animations,n[1],2000) && read(file,tracks,n[2],50000) &&
            read(file,keys,n[3],1000000) && read(file,ranges,n[4],10000) && read(file,points,n[5],2000000) &&
            read(file,original_stops,n[6],100000) && read(file,strings,n[7],4000000);
        ok=ok && std::fgetc(file)==EOF;std::fclose(file);
        if (!ok || nodes.empty() || strings.empty() || strings.back()!=0) {error="Invalid XUI pack length";return false;}
        auto span=[](int first,int count,size_t size){return first>=0 && count>=0 && size_t(first)<=size && size_t(count)<=size-size_t(first);};
        auto token=[&](uint32_t at){return at<strings.size();};
        for (size_t i=0;i<nodes.size();i++) {
            const Node &node=nodes[i];
            ok=ok && node.parent>=-1 && node.parent<int(i) && node.branch_owner>=-1 && node.branch_owner<int(i) && token(node.id) &&
                span(node.point_first,node.point_count,points.size()) && span(node.stop_first,node.stop_count,original_stops.size());
            for (int f:{Visual,Text,Texture,File,Font}) ok=ok && token(node.p[f].token);
        }
        for (const auto &a:animations) ok=ok && a.owner>=0 && size_t(a.owner)<nodes.size() && span(a.range_first,a.range_count,ranges.size()) && span(a.track_first,a.track_count,tracks.size());
        for (const auto &t:tracks) {
            ok=ok && t.target>=0 && size_t(t.target)<nodes.size() && t.animation>=0 && size_t(t.animation)<animations.size() &&
                t.field>=0 && t.field<FieldCount && span(t.key_first,t.key_count,keys.size());
            if (ok && (t.field==StopPosition || t.field==StopColor)) ok=t.index>=0 && t.index<nodes[t.target].stop_count;
            float last=-1;
            if (ok) for (int k=t.key_first;k<t.key_first+t.key_count;k++) {
                ok=ok && std::isfinite(keys[k].frame) && keys[k].frame>=last && keys[k].interpolation>=0 && keys[k].interpolation<=2 && keys[k].ease_scale==50;
                if (t.field==Visual || t.field==Text || t.field==Texture || t.field==File || t.field==Font) ok=ok && token(keys[k].value.token);
                last=keys[k].frame;
            }
        }
        for (const auto &r:ranges) ok=ok && token(r.name) && token(r.target) && r.command>=0 && r.command<=4;
        if (!ok) {error="Invalid XUI hierarchy or timeline reference";return false;}
        state.resize(nodes.size());world.resize(nodes.size());playback.assign(animations.size(),{});stops=original_stops;
        for (size_t i=0;i<animations.size();i++) playback[i].playing=animations[i].automatic!=0;
        valid=true;evaluate();return true;
    }
    bool seek_name(int owner,const std::string &name,bool play=false) {
        bool found=false;
        for (size_t i=0;i<animations.size();i++) if (animations[i].owner==owner) {
            const auto &a=animations[i];
            for (int f=a.range_first;f<a.range_first+a.range_count;f++) if (name==str(ranges[f].name)) {
                playback[i]={ranges[f].frame,play && ranges[f].command!=1 && ranges[f].command!=4};found=true;
                if (playback[i].playing) collect_events(i,ranges[f].frame,ranges[f].frame,true);
            }
        }
        return found;
    }
    void seek(int owner,float frame) {sound_events.clear();for (size_t i=0;i<animations.size();i++) if (animations[i].owner==owner) playback[i]={frame,false};}
    void advance(float frames) {
        frames=std::max(frames,0.f);
        for (size_t i=0;i<animations.size();i++) {
            auto &p=playback[i];const auto &a=animations[i];
            if (!p.playing) continue;
            if (a.automatic) {
                if (a.last<=0) {p.frame=0;continue;}
                float left=frames;
                for (int loop=0;loop<32 && left>0;loop++) {
                    const float next=std::min(a.last,p.frame+left);collect_events(i,p.frame,next);
                    left-=next-p.frame;p.frame=next;
                    if (p.frame>=a.last) {p.frame=0;collect_events(i,0,0,true);}
                }
                continue;
            }
            float remaining=frames;
            for (int jump=0;jump<32 && p.playing && remaining>0;jump++) {
                const NamedFrame *hit=nullptr;
                for (int f=a.range_first;f<a.range_first+a.range_count;f++) {
                    const auto &r=ranges[f];
                    if (r.command && r.frame>p.frame && r.frame<=p.frame+remaining && (!hit || r.frame<hit->frame)) hit=&r;
                }
                if (!hit) {const float next=std::min(p.frame+remaining,a.last);collect_events(i,p.frame,next);p.frame=next;if (p.frame>=a.last) p.playing=false;break;}
                collect_events(i,p.frame,hit->frame);remaining-=hit->frame-p.frame;p.frame=hit->frame;
                if (hit->command==1) {p.playing=false;break;}
                bool target_found=false;
                for (int f=a.range_first;f<a.range_first+a.range_count;f++) if (!std::strcmp(str(ranges[f].name),str(hit->target))) {
                    p.frame=ranges[f].frame;p.playing=hit->command!=4;target_found=true;
                    if (p.playing) collect_events(i,p.frame,p.frame,true);
                    break;
                }
                if (!target_found) {p.playing=false;break;}
            }
        }
    }
    void evaluate() {
        if (!valid) return;
        for (size_t i=0;i<nodes.size();i++) std::copy(std::begin(nodes[i].p),std::end(nodes[i].p),state[i].begin());
        std::copy(original_stops.begin(),original_stops.end(),stops.begin());
        for (const auto &track:tracks) {
            if (!track.key_count) continue;
            const float frame=playback[track.animation].frame;
            int k=track.key_first;
            while (k+1<track.key_first+track.key_count && keys[k+1].frame<=frame) k++;
            const auto value=k+1<track.key_first+track.key_count?interpolate(keys[k],keys[k+1],frame,track.field):keys[k].value;
            if (track.field==StopPosition) stops[nodes[track.target].stop_first+track.index].position=value.v[0];
            else if (track.field==StopColor) stops[nodes[track.target].stop_first+track.index].color=value.token;
            else state[track.target][track.field]=value;
        }
        for (const auto &binding:host_bindings) state[binding.first.first][binding.first.second]=binding.second;
        for (size_t i=0;i<nodes.size();i++) {
            const Node &n=nodes[i];const auto *p=state[i].data();auto &w=world[i];
            w.width=std::max(0.f,p[Width].v[0]);w.height=std::max(0.f,p[Height].v[0]);
            float x=p[Position].v[0],y=p[Position].v[1];const int flags=int(p[Anchor].v[0]);
            const World *parent=n.parent>=0?&world[n.parent]:nullptr;
            if (parent) {
                if (n.branch_owner==n.parent) {w.width=parent->width;w.height=parent->height;}
                else {
                    const float dw=parent->width-n.design_w,dh=parent->height-n.design_h;
                    if ((flags&5)==5) w.width=std::max(0.f,w.width+dw);
                    else if ((flags&64) && n.design_w>0) w.width*=parent->width/n.design_w;
                    if ((flags&10)==10) w.height=std::max(0.f,w.height+dh);
                    else if ((flags&128) && n.design_h>0) w.height*=parent->height/n.design_h;
                    if ((flags&4) && !(flags&1)) x+=dw;
                    else if (flags&16) x+=dw/2;
                    else if ((flags&64) && n.design_w>0) x*=parent->width/n.design_w;
                    if ((flags&8) && !(flags&2)) y+=dh;
                    else if (flags&32) y+=dh/2;
                    else if ((flags&128) && n.design_h>0) y*=parent->height/n.design_h;
                }
            }
            w.matrix=(parent?parent->matrix:Matrix::identity())*local(p,x,y);
            w.opacity=std::clamp(p[Opacity].v[0],0.f,1.f)*(parent?parent->opacity:1);
            w.blend=int(p[Blend].v[0]);if (!w.blend) w.blend=parent?parent->blend:Normal;
            w.visible=p[Show].v[0]!=0 && (!parent || parent->visible) && (w.opacity>0 || w.blend!=Normal);
            if (n.branch_owner>=0 && state[n.branch_owner][Visual].token!=uint32_t(n.branch_visual)) w.visible=false;
        }
    }
};
}
