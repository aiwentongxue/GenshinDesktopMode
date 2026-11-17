#pragma once
#include <cstdint>

// Keep plain Escape as game input in both windowed and fullscreen modes.
// AppKit otherwise handles windowed Escape as a cancel action that may beep.
// Own both edges of a consumed press, including when the window changes mode
// before key-up. Modifier shortcuts retain the normal system route.
struct DesktopEscapeGuard {
    struct Decision {bool consume=false,pressed=false;};
    bool ownsPress=false;
    void reset() {ownsPress=false;}
    Decision event(unsigned type,unsigned code,uint64_t modifiers,bool gameWindow,bool foreground) {
        if(code!=53 || (type!=10 && type!=11)) return {};
        if(type==11 && ownsPress) {ownsPress=false;return {true,false};}
        constexpr uint64_t shortcuts=(1ULL<<17)|(1ULL<<18)|(1ULL<<19)|(1ULL<<20);
        if(type==10 && foreground && (ownsPress || (gameWindow && !(modifiers&shortcuts)))) {
            ownsPress=true;return {true,true};
        }
        return {};
    }
};
