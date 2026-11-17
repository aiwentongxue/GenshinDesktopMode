#pragma once
#include "mouse_drag_state.h"

// A 3D preview press is also the game's relic-picking gesture. Keep a click
// distinct from a rotation, including focus loss and crossing a UI boundary.
struct CharacterPointerState {
    DesktopMouseDragState motion;
    bool nativePressed=false;
    struct Step {
        DesktopMouseDragState::Sample motion;
        bool pressed=false,clicked=false,cancelled=false;
    };
    Step sample(bool down,bool available,bool mayBegin,uint64_t focus,
                float x,float y,float threshold) {
        Step out;
        bool wasHeld=motion.held,wasDragging=motion.dragging;
        bool sameFocus=focus==motion.revision;
        bool finite=std::isfinite(x)&&std::isfinite(y)&&
            std::isfinite(threshold)&&threshold>0;
        float dx=x-motion.pressX,dy=y-motion.pressY;
        bool clickDistance=finite&&dx*dx+dy*dy<threshold*threshold;
        out.motion=motion.sample(down,available&&finite,mayBegin,focus,x,y,threshold);
        if(down&&!wasHeld&&motion.armed) {
            nativePressed=true;out.pressed=true;
        }
        if(nativePressed&&!motion.armed) {
            out.clicked=wasHeld&&!down&&available&&sameFocus&&
                !wasDragging&&clickDistance;
            out.cancelled=!out.clicked&&!out.motion.ended;
            nativePressed=false;
        }
        return out;
    }
    bool cancel(void) {
        bool owned=nativePressed;
        motion.cancel();nativePressed=false;
        return owned;
    }
};
