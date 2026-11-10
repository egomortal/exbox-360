#pragma once
#include "dash_model.hpp"

namespace dash {
struct TileBounds { int x, y, w, h; };
// Template1HD/Template6HD source units at the current 1.5 display scale.
// Round absolute edges once, not an already-rounded pitch repeatedly. The
// existing capture-derived origin and channel/template mapping remain provisional.
inline constexpr int metro_hd_pixel(int source) { return (source*3+1)/2; }
inline constexpr TileBounds metro_hd_rect(int x,int y,int w,int h,int ox,int oy) {
    return {ox+metro_hd_pixel(x),oy+metro_hd_pixel(y),
            metro_hd_pixel(x+w)-metro_hd_pixel(x),metro_hd_pixel(y+h)-metro_hd_pixel(y)};
}
// Ex-Box addition: separate Home avatar focus node; no original card ID is replaced.
inline constexpr TileBounds metro_avatar_bounds(bool v1) {return v1?TileBounds{1579,326,296,222}:TileBounds{1584,270,261,195};}
inline constexpr TileBounds kMetroTiles[] = {
    metro_hd_rect(178,0,466,264,345,270), metro_hd_rect(0,0,174,130,345,270), metro_hd_rect(648,0,174,130,345,270),
    metro_hd_rect(648,134,174,130,345,270), metro_hd_rect(178,268,231,130,345,270), metro_hd_rect(412,268,231,130,345,270), metro_hd_rect(648,268,174,130,345,270),
    metro_hd_rect(0,134,174,130,345,270), metro_hd_rect(0,268,174,130,345,270)
};
inline constexpr TileBounds metro_settings_bounds(int slot,bool v1,int offset=0) {
    return v1?TileBounds{369+(slot%4)*304+offset,326+(slot/4)*304,296,296}:
              metro_hd_rect((slot%4)*201,(slot/4)*201,197,197,369+offset,270);
}
inline constexpr int kMetroNeighbors[4][9] = {
    {7,-1,0,0,8,4,5,-1,-1},
    {2,0,-1,-1,5,6,-1,0,4},
    {-1,-1,-1,2,0,0,3,1,7},
    {4,7,3,6,-1,-1,-1,8,-1}
};
inline constexpr TileBounds kMetroV1HomeTiles[] = {
    {671,326,600,450},{369,326,296,222},{1277,326,296,222},{1277,554,296,222},
    {},{},{},{369,554,296,222},{}
};
inline constexpr TileBounds kMetroV1Tiles[] = {
    {671,326,600,450},{369,326,296,222},{1277,326,296,222},{1277,554,296,222},
    {369,554,296,222},{},{},{},{}
};
inline constexpr int kMetroV1Neighbors[4][9] = {
    {1,-1,0,0,-1,-1,-1,-1,-1},
    {2,0,-1,-1,0,-1,-1,-1,-1},
    {-1,-1,-1,2,1,-1,-1,-1,-1},
    {4,4,3,-1,-1,-1,-1,-1,-1}
};
inline constexpr int kMetroV1HomeNeighbors[4][9] = {
    {7,-1,0,0,-1,-1,-1,-1,-1},
    {2,0,-1,-1,-1,-1,-1,0,-1},
    {-1,-1,-1,2,-1,-1,-1,1,-1},
    {-1,7,3,-1,-1,-1,-1,-1,-1}
};
inline constexpr const char *kMetroTabs[] = {
    "bing", "home", "social", "games", "tv & movies", "music", "apps", "settings"
};
inline constexpr int kBladesChannels[] = {Apps, Friends, Games, Media, MyXbox};
}
