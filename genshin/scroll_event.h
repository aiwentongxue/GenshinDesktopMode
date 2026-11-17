#pragma once
#include "scroll_state.h"
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <array>
#include <dlfcn.h>

// eventNumber is not valid for wheel events. Never use the mouse-drag event
// key for scrolling, and never discard a whole Mos stream by its timestamp.
class DesktopScrollEventSequence {
    std::array<__strong id,32> recent{};
    unsigned next=0;
public:
    bool first(id event) {
        for(id old:recent) if(old==event) return false;
        recent[next++%recent.size()]=event;return true;
    }
};
static DesktopScrollDispatchScope::Key DesktopScrollKey(id event) {
    auto scalar=[&](const char *name) {return ((double (*)(id,SEL))objc_msgSend)(event,sel_registerName(name));};
    auto integer=[&](const char *name) {return ((NSUInteger (*)(id,SEL))objc_msgSend)(event,sel_registerName(name));};
    bool precise=((BOOL (*)(id,SEL))objc_msgSend)(event,sel_registerName("hasPreciseScrollingDeltas"));
    return {scalar("timestamp"),scalar("scrollingDeltaX"),scalar("scrollingDeltaY"),
        (uint32_t)integer("phase"),(uint32_t)integer("momentumPhase"),precise};
}
static bool DesktopScrollIsMos(id event) {
    static auto field=(int64_t (*)(CFTypeRef,uint32_t))dlsym(RTLD_DEFAULT,"CGEventGetIntegerValueField");
    CFTypeRef cg=((CFTypeRef (*)(id,SEL))objc_msgSend)(event,sel_registerName("CGEvent"));
    // Public marker used by Mos 4.x ScrollUtils.markSyntheticSmoothEvent.
    return field && cg && (uint64_t)field(cg,42)==0x4D4F53534D4F4F54ULL;
}
