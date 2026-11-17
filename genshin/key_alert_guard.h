#pragma once
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <cstdint>

namespace DesktopKeyAlerts {
static bool IsSilentGameKey(unsigned eventType,uint64_t modifiers) {
    // Control and Option/Alt are gameplay modifiers. Command chords retain
    // AppKit's normal shortcut handling and fallback behavior.
    return eventType==10 && !(modifiers&(1ULL<<20));
}
static void (*original)(id,SEL,SEL);
static bool (*contextAllowsSuppression)();
static void Handle(id responder,SEL selector,SEL eventSelector) {
    if(eventSelector==sel_registerName("keyDown:") && contextAllowsSuppression &&
       contextAllowsSuppression()) return;
    original(responder,selector,eventSelector);
}
static bool Install(bool (*context)()) {
    if(original) return true;
    Method method=class_getInstanceMethod(NSClassFromString(@"NSResponder"),sel_registerName("noResponderFor:"));
    if(!method) return false;
    contextAllowsSuppression=context;
    original=(void (*)(id,SEL,SEL))method_setImplementation(method,(IMP)Handle);
    return original!=nullptr;
}
}
