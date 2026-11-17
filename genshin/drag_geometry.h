#pragma once
#include <cmath>

// A captured drag cannot cross a change of logical/pixel coordinate systems.
// Reject it until the real button is released; never turn cancellation into a
// click or apply a resize jump to the native camera/model.
struct DesktopDragGeometry {
    int width=0,height=0;
    double pointsWidth=0,pointsHeight=0;
    bool capture(int w,int h,double pw,double ph) {
        width=w;height=h;pointsWidth=pw;pointsHeight=ph;
        return w>0&&h>0&&std::isfinite(pw)&&std::isfinite(ph)&&pw>0&&ph>0;
    }
    bool matches(int w,int h,double pw,double ph)const {
        return w>0&&h>0&&w==width&&h==height&&std::isfinite(pw)&&std::isfinite(ph)&&
            std::fabs(pw-pointsWidth)<.01&&std::fabs(ph-pointsHeight)<.01;
    }
};
