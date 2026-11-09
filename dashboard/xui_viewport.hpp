#pragma once
#include "xui_runtime.hpp"
namespace xui {
// Owner's 1888 dashboard, 0x920a41e0. The 960-wide path is its 4:3 mode.
inline Matrix viewport(int width,int height) {
    if (width==960) return Matrix::scale(6.f/7,72.f/77);
    auto view=Matrix::scale(float(width)/1280*(8.f/7),float(height)/720*(12.f/11));
    view.a[1][3]=-64.f*float(height)/720;
    return view;
}
inline Matrix inverse_viewport(int width,int height) {
    const auto view=viewport(width,height);auto inverse=Matrix::scale(1/view.a[0][0],1/view.a[1][1]);
    inverse.a[1][3]=-view.a[1][3]/view.a[1][1];return inverse;
}
}
