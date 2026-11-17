#pragma once
#include <cmath>
#include <cstdint>

// A desktop button capture. Positions are the same absolute pixels used by
// Unity Mouse and UI hit testing; there is no touch identity or acceleration.
struct DesktopMouseDragState {
    struct Sample {bool began=false,moved=false,ended=false;float x=0,y=0;};
    bool held=false,armed=false,dragging=false;
    uint64_t revision=0;
    float pressX=0,pressY=0,lastX=0,lastY=0;
    Sample sample(bool down,bool available,bool mayBegin,uint64_t focus,
                  float x,float y,float threshold=10) {
        Sample out;
        bool fresh=down&&!held;
        held=down;
        if(!available || !std::isfinite(x)||!std::isfinite(y)||
           !std::isfinite(threshold)||threshold<=0||focus!=revision || !down) {
            out.ended=dragging;
            armed=dragging=false;
            revision=focus;
            return out;
        }
        if(fresh) {
            armed=mayBegin;
            pressX=lastX=x;pressY=lastY=y;
        }
        if(!armed) return out;
        float dx=x-lastX,dy=y-lastY;
        if(!dragging) {
            float px=x-pressX,py=y-pressY;
            if(px*px+py*py<threshold*threshold) return out;
            dragging=true;out.began=true;
            dx=x-pressX;dy=y-pressY;
        }
        lastX=x;lastY=y;
        out.x=dx;out.y=dy;
        out.moved=std::isfinite(dx)&&std::isfinite(dy)&&(dx!=0||dy!=0);
        return out;
    }
    Sample cancel(void) {
        Sample out;out.ended=dragging;armed=dragging=false;return out;
    }
};
