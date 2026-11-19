#pragma once
#include "drag_geometry.h"
#include <cstdint>

// Map dragging measures the dead zone in window points. The camera receives
// unscaled Unity pixel deltas all the way through the release position.
struct DesktopMapMouseGesture {
    struct Step { bool begin=false,end=false,click=false; float dx=0,dy=0; };
    bool held=false,armed=false,panning=false;
    DesktopDragGeometry viewport;
    float startX=0,startY=0,lastX=0,lastY=0;
    uint64_t revision=0;

    bool cancel() {
        bool ending=panning;
        armed=panning=false;
        return ending;
    }
    Step sample(bool down,bool available,bool mayBegin,uint64_t focus,float x,float y) {
        Step step;
        bool fresh=down&&!held,release=!down&&held;
        held=down;
        if(!available || !std::isfinite(x) || !std::isfinite(y) || (armed && revision!=focus)) {
            step.end=cancel();
            return step;
        }
        if(fresh) {
            armed=mayBegin;
            revision=focus;
            startX=lastX=x;startY=lastY=y;
        }
        if(!armed) return step;
        double dx=(x-startX)*viewport.pointsWidth/viewport.width;
        double dy=(y-startY)*viewport.pointsHeight/viewport.height;
        constexpr double thresholdPoints=4.0;
        if(!panning && dx*dx+dy*dy>=thresholdPoints*thresholdPoints) {
            panning=true;
            step.begin=true;
        }
        if(panning) {
            step.dx=x-lastX;step.dy=y-lastY;
            lastX=x;lastY=y;
        }
        if(release) {
            step.click=!panning;
            step.end=cancel();
        }
        return step;
    }
};
