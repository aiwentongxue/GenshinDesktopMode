#pragma once
#include "character_pointer_state.h"

// Native release also raycasts if no rotation was recorded. Begin natively
// only after crossing the drag threshold; short presses use the click route.
struct CodexPointerState {
    CharacterPointerState pointer;
    bool nativeDragging=false;
    struct Step {
        bool begin=false,update=false,end=false,click=false;
        float deltaX=0;
    };
    Step sample(bool down,bool available,bool mayBegin,uint64_t focus,
                float x,float y,float threshold) {
        auto event=pointer.sample(down,available,mayBegin,focus,x,y,threshold);
        Step out;
        if(event.motion.began) {nativeDragging=true;out.begin=true;}
        if(nativeDragging&&!pointer.motion.dragging) {
            nativeDragging=false;out.end=true;
        }
        out.update=nativeDragging;
        out.deltaX=event.motion.moved?event.motion.x:0;
        out.click=event.clicked;
        return out;
    }
    bool cancel() {
        pointer.cancel();
        bool end=nativeDragging;nativeDragging=false;return end;
    }
};
