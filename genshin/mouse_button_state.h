#pragma once
#include <cstdint>

// A click belongs to the game only if its press starts in game content.
// Hover, a held button used to activate the app, web views and window chrome
// cannot manufacture a new game press. A drag keeps ownership until release.
struct DesktopMouseButtons {
    struct Frame {uint32_t held=0,down=0,up=0;};
    uint32_t physical=0,owned=0,pendingUp=0;
    bool focused=false;

    void cancel() {pendingUp|=owned;owned=0;}
    Frame sample(uint32_t mask,bool eligible,bool foreground) {
        // One authority at Unity's native input update. Alternating AppKit
        // events with a physical snapshot can replay a held button as clicks.
        mask&=0x7f;uint32_t down=0;
        if(!foreground) {cancel();physical=mask;focused=false;}
        else if(!focused) {physical=mask;focused=true;} // require a fresh press
        else {
            pendingUp|=owned&~mask;
            down=eligible?(mask&~physical):0;
            owned=(owned&mask)|down;physical=mask;
        }
        Frame frame{owned,down,pendingUp};pendingUp=0;
        return frame;
    }
};
