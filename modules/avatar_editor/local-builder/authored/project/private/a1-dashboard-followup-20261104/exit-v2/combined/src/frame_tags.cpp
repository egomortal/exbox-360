#include "frame_tags.h"
namespace {av2::legend::FrameTags tags;}
extern "C" void av2_legend_publish(uint64_t frame,bool root){tags.publish(frame,root);}
extern "C" bool av2_legend_root(uint64_t frame){bool root=false;return tags.read(frame,root)&&root;}
